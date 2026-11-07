#include <applications/services/audio/audio.h>

#include <furi.h>
#include <furi_hal_usb.h>
#include <furi_hal_usb_audio.h>

#include <drivers/cs43131.h>
#include <drivers/es8327.h>

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

typedef struct {
    FuriEventLoop* event_loop;
} Audio;

static Audio* audio_alloc(void) {
    Audio* audio = malloc(sizeof(Audio));

    audio->event_loop = furi_event_loop_alloc();

    return audio;
}

static void audio_free(Audio* audio) {
    furi_event_loop_free(audio->event_loop);

    free(audio);
}

static void audio_init(Audio* audio) {
    UNUSED(audio);
    furi_hal_usb_audio_set_config(&audio_speaker_usb_config, &audio_mic_usb_config);
    // TODO: enable only in audio mode
    furi_hal_usb_enable();
}

static void audio_deinit(Audio* audio) {
    UNUSED(audio);
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
