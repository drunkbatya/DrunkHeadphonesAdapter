#include <applications/services/audio/audio.h>

#include <furi.h>
#include <furi_hal.h>
#include <furi_hal_usb_audio.h>

#include <drivers/cs43131.h>
#include <drivers/es8327.h>

#define TAG "AudioSrv"

#define AUDIO_SPEAKER_USB_VOLUME(attenuation) \
    ((int16_t)(-(attenuation) * FURI_HAL_USB_AUDIO_VOLUME_PER_DB / CS43131_VOLUME_STEPS_PER_DB))

#define AUDIO_MIC_USB_VOLUME(adc_volume)                                                   \
    ((int16_t)(((adc_volume) - ES8327_ADC_VOLUME_0DB) * FURI_HAL_USB_AUDIO_VOLUME_PER_DB / \
               ES8327_ADC_VOLUME_STEPS_PER_DB))

static const uint32_t audio_speaker_sample_rates[] = {44100, 48000, 88200, 96000};
static const uint32_t audio_mic_sample_rates[] = {44100, 48000};

static const FuriHalUsbAudioStreamConfig audio_speaker_usb_config = {
    .sample_rates = audio_speaker_sample_rates,
    .sample_rate_count = COUNT_OF(audio_speaker_sample_rates),
    .volume_range =
        {
            .min = AUDIO_SPEAKER_USB_VOLUME(CS43131_VOLUME_ATTENUATION_MAX),
            .max = AUDIO_SPEAKER_USB_VOLUME(CS43131_VOLUME_ATTENUATION_MIN),
            .resolution = FURI_HAL_USB_AUDIO_VOLUME_PER_DB / CS43131_VOLUME_STEPS_PER_DB,
        },
};

static const FuriHalUsbAudioStreamConfig audio_mic_usb_config = {
    .sample_rates = audio_mic_sample_rates,
    .sample_rate_count = COUNT_OF(audio_mic_sample_rates),
    .volume_range =
        {
            .min = AUDIO_MIC_USB_VOLUME(ES8327_ADC_VOLUME_MIN),
            .max = AUDIO_MIC_USB_VOLUME(ES8327_ADC_VOLUME_MAX),
            .resolution = FURI_HAL_USB_AUDIO_VOLUME_PER_DB / ES8327_ADC_VOLUME_STEPS_PER_DB,
        },
};

typedef enum {
    AudioThreadFlagCs43131Interrupt = (1 << 0),
} AudioThreadFlag;

typedef struct {
    FuriThreadId thread_id;
    FuriEventLoop* event_loop;
    FuriPubSub* headset_events;
    bool headphone_plugged;
} Audio;

static Audio* audio_alloc(void) {
    Audio* audio = malloc(sizeof(Audio));

    audio->thread_id = furi_thread_get_current_id();
    audio->event_loop = furi_event_loop_alloc();
    audio->headset_events = furi_pubsub_alloc();
    audio->headphone_plugged = false;

    return audio;
}

static void audio_free(Audio* audio) {
    furi_pubsub_free(audio->headset_events);
    furi_event_loop_free(audio->event_loop);

    free(audio);
}

static void audio_cs43131_interrupt_callback(void* context) {
    Audio* audio = context;
    furi_thread_flags_set(audio->thread_id, AudioThreadFlagCs43131Interrupt);
}

static void audio_cs43131_reset(void) {
    furi_hal_gpio_write(&gpio_cs43131_reset_n, false);
    furi_delay_us(CS43131_RESET_PULSE_WIDTH_US);
    furi_hal_gpio_write(&gpio_cs43131_reset_n, true);
    furi_delay_us(CS43131_POWER_UP_DELAY_US);
}

static void audio_handle_cs43131_interrupt(Audio* audio) {
    uint8_t interrupt_status;
    bool headphone_plugged;

    furi_hal_i2c_acquire(&furi_hal_i2c_handle_internal);
    bool success = false;
    do {
        if(!cs43131_read_interrupt_status(&furi_hal_i2c_handle_internal, &interrupt_status)) break;
        if(!cs43131_is_headphone_plugged(&furi_hal_i2c_handle_internal, &headphone_plugged)) break;
        success = true;
    } while(0);
    furi_hal_i2c_release(&furi_hal_i2c_handle_internal);

    if(!success) {
        FURI_LOG_E(TAG, "CS43131 status read failed");
        return;
    }

    FURI_LOG_D(TAG, "CS43131 interrupt status 0x%02X", interrupt_status);

    if(headphone_plugged == audio->headphone_plugged) {
        return;
    }

    audio->headphone_plugged = headphone_plugged;
    FURI_LOG_I(TAG, "Headphones %s", headphone_plugged ? "plugged" : "unplugged");

    HeadsetEvent event = headphone_plugged ? HeadsetEventPlugged : HeadsetEventUnplugged;
    furi_pubsub_publish(audio->headset_events, &event);
}

static void audio_thread_flags_callback(void* context) {
    Audio* audio = context;

    const uint32_t flags = furi_thread_flags_get();
    furi_thread_flags_clear(flags);

    if(flags & AudioThreadFlagCs43131Interrupt) {
        audio_handle_cs43131_interrupt(audio);
    }
}

static void audio_init(Audio* audio) {
    furi_record_create(RECORD_HEADSET_EVENTS, audio->headset_events);

    furi_event_loop_subscribe_thread_flags(audio->event_loop, audio_thread_flags_callback, audio);
    furi_hal_gpio_add_int_callback(&gpio_cs43131_int, audio_cs43131_interrupt_callback, audio);

    audio_cs43131_reset();

    furi_hal_i2c_acquire(&furi_hal_i2c_handle_internal);
    const bool cs43131_ready = cs43131_init(&furi_hal_i2c_handle_internal);
    const bool es8327_ready = es8327_init(&furi_hal_i2c_handle_internal);
    furi_hal_i2c_release(&furi_hal_i2c_handle_internal);

    if(cs43131_ready) {
        audio_handle_cs43131_interrupt(audio);
    } else {
        FURI_LOG_E(TAG, "CS43131 init failed");
    }

    if(!es8327_ready) {
        FURI_LOG_E(TAG, "ES8327 init failed");
    }

    furi_hal_usb_audio_set_config(&audio_speaker_usb_config, &audio_mic_usb_config);
    // TODO: enable only in audio mode
    furi_hal_usb_enable();
}

static void audio_deinit(Audio* audio) {
    furi_hal_gpio_remove_int_callback(&gpio_cs43131_int);
    furi_event_loop_unsubscribe_thread_flags(audio->event_loop);

    furi_hal_gpio_write(&gpio_cs43131_reset_n, false);

    furi_record_destroy(RECORD_HEADSET_EVENTS);
}

int32_t audio_srv(void* context) {
    UNUSED(context);

    Audio* audio = audio_alloc();
    audio_init(audio);

    furi_event_loop_run(audio->event_loop);

    audio_deinit(audio);
    audio_free(audio);

    return 0;
}
