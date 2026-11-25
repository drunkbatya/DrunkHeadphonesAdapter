#include <applications/services/audio/audio.h>
#include <applications/services/audio/audio_capture.h>
#include <applications/services/audio/audio_playback.h>
#include <applications/services/settings/settings_store.h>

#include <furi.h>
#include <furi_hal.h>
#include <furi_hal_usb_audio.h>

#include <drivers/cs43131.h>
#include <drivers/es8327.h>
#include <toolbox/api_lock.h>

#define TAG "AudioSrv"

#define AUDIO_IDLE_TIMEOUT_MS               30000
#define AUDIO_PLAYBACK_STATS_PERIOD_MS      1000
#define AUDIO_OSCILLATOR_SWITCH_TURN_ON_US  200
#define AUDIO_OSCILLATOR_STARTUP_MS         5
#define AUDIO_PDN_DONE_POLL_PERIOD_MS       1
#define AUDIO_VOLUME_LOG_FRACTION_SCALE     100
#define AUDIO_MIC_DETECT_DELAY_MS           300
#define AUDIO_MIC_DETECT_SAMPLE_COUNT       8
#define AUDIO_MIC_DETECT_SAMPLE_PERIOD_MS   1
#define AUDIO_MIC_PRESENT_SLEEVE_MIN_MV     200
#define AUDIO_SPEAKER_VOLUME_OFFSET_STEP_DB 1

#define AUDIO_SPEAKER_USB_VOLUME(attenuation) \
    ((int16_t)(-(attenuation) * FURI_HAL_USB_AUDIO_VOLUME_PER_DB / CS43131_VOLUME_STEPS_PER_DB))

#define AUDIO_SPEAKER_ATTENUATION(usb_volume) \
    ((uint8_t)(-(usb_volume) * CS43131_VOLUME_STEPS_PER_DB / FURI_HAL_USB_AUDIO_VOLUME_PER_DB))

#define AUDIO_MIC_ADC_VOLUME(usb_volume) \
    ((uint8_t)(ES8327_ADC_VOLUME_0DB +   \
               (usb_volume) * ES8327_ADC_VOLUME_STEPS_PER_DB / FURI_HAL_USB_AUDIO_VOLUME_PER_DB))

#define AUDIO_MIC_USB_VOLUME(adc_volume)                                                   \
    ((int16_t)(((adc_volume) - ES8327_ADC_VOLUME_0DB) * FURI_HAL_USB_AUDIO_VOLUME_PER_DB / \
               ES8327_ADC_VOLUME_STEPS_PER_DB))

// back this values after pcb rev2 prod and implementing self-feedback via ajustable oscilators
//static const uint32_t audio_speaker_sample_rates[] = {44100, 48000, 88200, 96000};
//static const uint32_t audio_mic_sample_rates[] = {44100, 48000}};

// now limiting to 48k only because my Pixel accepts feedback only on 48k.. fucking why?..
static const uint32_t audio_speaker_sample_rates[] = {48000};
static const uint32_t audio_mic_sample_rates[] = {48000};

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

static const FuriHalUsbAudioStreamConfig* const audio_usb_configs[FuriHalUsbAudioStreamCount] = {
    [FuriHalUsbAudioStreamSpeaker] = &audio_speaker_usb_config,
    [FuriHalUsbAudioStreamMic] = &audio_mic_usb_config,
};

static const char* const audio_i2s_error_names[] = {
    [FuriHalI2sEventUnderrun] = "underrun",
    [FuriHalI2sEventOverrun] = "overrun",
    [FuriHalI2sEventFrameError] = "frame error",
    [FuriHalI2sEventDmaError] = "DMA error",
};

static const char* const audio_stream_names[FuriHalUsbAudioStreamCount] = {
    [FuriHalUsbAudioStreamSpeaker] = "Speaker",
    [FuriHalUsbAudioStreamMic] = "Mic",
};

typedef enum {
    AudioThreadFlagCs43131Interrupt = (1 << 0),
    AudioThreadFlagPlaybackError = (1 << 1),
    AudioThreadFlagSpeakerStream = (1 << 2),
    AudioThreadFlagSpeakerVolume = (1 << 3),
    AudioThreadFlagSpeakerMute = (1 << 4),
    AudioThreadFlagMicStream = (1 << 5),
    AudioThreadFlagMicVolume = (1 << 6),
    AudioThreadFlagMicMute = (1 << 7),
    AudioThreadFlagCaptureError = (1 << 8),
    AudioThreadFlagShutdown = (1 << 9),
    AudioThreadFlagSettings = (1 << 10),
    AudioThreadFlagPause = (1 << 11),
    AudioThreadFlagResume = (1 << 12),
} AudioThreadFlag;

static const uint32_t
    audio_usb_change_flags[FuriHalUsbAudioStreamCount][FuriHalUsbAudioChangeCount] = {
        [FuriHalUsbAudioStreamSpeaker] =
            {
                [FuriHalUsbAudioChangeStream] = AudioThreadFlagSpeakerStream,
                [FuriHalUsbAudioChangeVolume] = AudioThreadFlagSpeakerVolume,
                [FuriHalUsbAudioChangeMute] = AudioThreadFlagSpeakerMute,
            },
        [FuriHalUsbAudioStreamMic] =
            {
                [FuriHalUsbAudioChangeStream] = AudioThreadFlagMicStream,
                [FuriHalUsbAudioChangeVolume] = AudioThreadFlagMicVolume,
                [FuriHalUsbAudioChangeMute] = AudioThreadFlagMicMute,
            },
};

typedef bool (*AudioCodecStep)(const FuriHalI2cBusHandle* handle);

typedef struct {
    bool active;
    FuriHalUsbAudioFormat format;
} AudioStream;

typedef struct {
    bool volume_reported;
    int16_t volume;
    bool mute_reported;
    bool muted;
} AudioControls;

struct Audio {
    FuriThreadId thread_id;
    FuriEventLoop* event_loop;
    FuriEventLoopTimer* idle_timer;
    FuriEventLoopTimer* mic_detect_timer;
    FuriEventLoopTimer* playback_stats_timer;
    FuriPubSub* headset_events;
    SettingsStore* settings_store;
    FuriPubSubSubscription* settings_subscription;
    int8_t speaker_volume_offset;
    uint8_t mic_gain;
    AudioPlayback* playback;
    AudioCapture* capture;
    AudioPlaybackError playback_error;
    volatile FuriHalI2sEvent capture_error;
    uint32_t i2s_error_counts[FuriHalUsbAudioStreamCount];
    bool headphone_plugged;
    bool headset_mic_present;
    bool mic_bias_enabled;
    AudioStream streams[FuriHalUsbAudioStreamCount];
    AudioControls controls[FuriHalUsbAudioStreamCount];
    uint32_t clock_users;
    Cs43131MclkSource mclk_source;
    bool headphone_output_powered;
    Cs43131SampleRate sample_rate;
    Cs43131BitDepth bit_depth;
    FuriApiLock shutdown_lock;
    FuriApiLock pause_lock;
    bool is_paused;
    bool is_shut_down;
    FuriMutex* status_mutex;
    AudioStatus status;
};

static void audio_idle_timer_callback(void* context);
static void audio_mic_detect_timer_callback(void* context);
static void audio_playback_stats_timer_callback(void* context);
static void audio_update_status(Audio* audio);

static void audio_playback_error_callback(const AudioPlaybackError* error, void* context) {
    Audio* audio = context;
    audio->playback_error = *error;
    furi_thread_flags_set(audio->thread_id, AudioThreadFlagPlaybackError);
}

static void audio_capture_error_callback(FuriHalI2sEvent event, void* context) {
    Audio* audio = context;
    audio->capture_error = event;
    furi_thread_flags_set(audio->thread_id, AudioThreadFlagCaptureError);
}

static Audio* audio_alloc(void) {
    Audio* audio = malloc(sizeof(Audio));

    audio->thread_id = furi_thread_get_current_id();
    audio->event_loop = furi_event_loop_alloc();
    audio->idle_timer = furi_event_loop_timer_alloc(
        audio->event_loop, audio_idle_timer_callback, FuriEventLoopTimerTypeOnce, audio);
    audio->mic_detect_timer = furi_event_loop_timer_alloc(
        audio->event_loop, audio_mic_detect_timer_callback, FuriEventLoopTimerTypeOnce, audio);
    audio->playback_stats_timer = furi_event_loop_timer_alloc(
        audio->event_loop,
        audio_playback_stats_timer_callback,
        FuriEventLoopTimerTypePeriodic,
        audio);
    audio->headset_events = furi_pubsub_alloc();
    audio->playback = audio_playback_alloc(audio_playback_error_callback, audio);
    audio->capture = audio_capture_alloc(audio_capture_error_callback, audio);
    audio->mclk_source = Cs43131MclkSourceRco;
    audio->status_mutex = furi_mutex_alloc(FuriMutexTypeNormal);

    return audio;
}

static void audio_free(Audio* audio) {
    furi_mutex_free(audio->status_mutex);
    audio_capture_free(audio->capture);
    audio_playback_free(audio->playback);
    furi_pubsub_free(audio->headset_events);
    furi_event_loop_timer_free(audio->playback_stats_timer);
    furi_event_loop_timer_free(audio->mic_detect_timer);
    furi_event_loop_timer_free(audio->idle_timer);
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

static void audio_set_mic_bias_enabled(Audio* audio, bool enabled) {
    if(audio->mic_bias_enabled == enabled) {
        return;
    }

    furi_hal_i2c_acquire(&furi_hal_i2c_handle_internal);
    const bool success = es8327_set_mic_bias_enabled(&furi_hal_i2c_handle_internal, enabled);
    furi_hal_i2c_release(&furi_hal_i2c_handle_internal);

    if(!success) {
        FURI_LOG_E(TAG, "ES8327 mic bias write failed, %d", enabled);
        return;
    }
    audio->mic_bias_enabled = enabled;
}

static void audio_set_headset_mic_present(Audio* audio, bool present) {
    audio->headset_mic_present = present;
    furi_hal_usb_audio_set_jack_connected(FuriHalUsbAudioStreamMic, present);
}

static uint32_t audio_read_headset_sleeve_mv(void) {
    uint32_t sleeve_mv = 0;

    furi_hal_adc_start();
    for(size_t i = 0; i < AUDIO_MIC_DETECT_SAMPLE_COUNT; i++) {
        furi_delay_ms(AUDIO_MIC_DETECT_SAMPLE_PERIOD_MS);
        sleeve_mv = MAX(sleeve_mv, furi_hal_adc_read_mv(FuriHalAdcChannelHeadsetButtons));
    }
    furi_hal_adc_stop();

    return sleeve_mv;
}

static void audio_detect_headset_mic(Audio* audio) {
    if(!audio->headphone_plugged) {
        return;
    }

    const uint32_t sleeve_mv = audio_read_headset_sleeve_mv();
    const bool present = sleeve_mv >= AUDIO_MIC_PRESENT_SLEEVE_MIN_MV;
    FURI_LOG_I(
        TAG, "Headset mic %s, sleeve %lu mV", present ? "detected" : "not detected", sleeve_mv);

    if(!present) {
        audio_set_mic_bias_enabled(audio, false);
    }
    audio_set_headset_mic_present(audio, present);
}

static void audio_mic_detect_timer_callback(void* context) {
    Audio* audio = context;

    audio_detect_headset_mic(audio);
    audio_update_status(audio);
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
    furi_hal_usb_audio_set_jack_connected(FuriHalUsbAudioStreamSpeaker, headphone_plugged);

    if(headphone_plugged) {
        audio_set_mic_bias_enabled(audio, true);
        furi_event_loop_timer_start(
            audio->mic_detect_timer, furi_ms_to_ticks(AUDIO_MIC_DETECT_DELAY_MS));
    } else {
        furi_event_loop_timer_stop(audio->mic_detect_timer);
        audio_set_mic_bias_enabled(audio, false);
        audio_set_headset_mic_present(audio, false);
    }

    HeadsetEvent event = headphone_plugged ? HeadsetEventPlugged : HeadsetEventUnplugged;
    furi_pubsub_publish(audio->headset_events, &event);
}

static Cs43131SampleRate audio_get_cs43131_sample_rate(uint32_t sample_rate) {
    switch(sample_rate) {
    case 44100:
        return Cs43131SampleRate44100;
    case 48000:
        return Cs43131SampleRate48000;
    case 88200:
        return Cs43131SampleRate88200;
    case 96000:
        return Cs43131SampleRate96000;
    default:
        furi_crash();
    }
}

static Cs43131BitDepth audio_get_cs43131_bit_depth(FuriHalUsbAudioSampleFormat sample_format) {
    switch(sample_format) {
    case FuriHalUsbAudioSampleFormat16Bit:
        return Cs43131BitDepth16;
    case FuriHalUsbAudioSampleFormat24Bit:
        return Cs43131BitDepth24;
    default:
        furi_crash();
    }
}

static uint8_t audio_get_sample_bits(FuriHalUsbAudioSampleFormat sample_format) {
    switch(sample_format) {
    case FuriHalUsbAudioSampleFormat16Bit:
        return 16;
    case FuriHalUsbAudioSampleFormat24Bit:
        return 24;
    default:
        furi_crash();
    }
}

static void audio_set_oscillator(Cs43131MclkSource source) {
    furi_hal_gpio_write(&gpio_oscillator_48k_en, source == Cs43131MclkSourceExternal24576);
    furi_hal_gpio_write(&gpio_oscillator_44k1_en, source == Cs43131MclkSourceExternal22579);
}

static bool audio_run_codec_step(AudioCodecStep step) {
    furi_hal_i2c_acquire(&furi_hal_i2c_handle_internal);
    const bool success = step(&furi_hal_i2c_handle_internal);
    furi_hal_i2c_release(&furi_hal_i2c_handle_internal);
    return success;
}

static bool audio_select_cs43131_rco(const FuriHalI2cBusHandle* handle) {
    if(!cs43131_set_clkout_enabled(handle, false)) {
        return false;
    }
    return cs43131_set_mclk_source(handle, Cs43131MclkSourceRco);
}

static bool audio_select_rco(Audio* audio) {
    if(!audio_run_codec_step(audio_select_cs43131_rco)) {
        return false;
    }
    audio_set_oscillator(Cs43131MclkSourceRco);
    audio->mclk_source = Cs43131MclkSourceRco;
    return true;
}

static bool audio_select_oscillator(Audio* audio, Cs43131MclkSource source) {
    audio_set_oscillator(source);
    furi_delay_us(AUDIO_OSCILLATOR_SWITCH_TURN_ON_US);
    furi_delay_ms(AUDIO_OSCILLATOR_STARTUP_MS);

    furi_hal_i2c_acquire(&furi_hal_i2c_handle_internal);
    bool success = false;
    do {
        if(!cs43131_set_mclk_source(&furi_hal_i2c_handle_internal, source)) break;
        audio->mclk_source = source;
        if(!cs43131_set_clkout_enabled(&furi_hal_i2c_handle_internal, true)) break;
        success = true;
    } while(0);
    furi_hal_i2c_release(&furi_hal_i2c_handle_internal);

    return success;
}

static bool audio_set_mclk_source(Audio* audio, Cs43131MclkSource source) {
    if(audio->mclk_source == source) {
        return true;
    }
    if(audio->mclk_source != Cs43131MclkSourceRco && !audio_select_rco(audio)) {
        return false;
    }
    if(source == Cs43131MclkSourceRco) {
        return true;
    }
    return audio_select_oscillator(audio, source);
}

static bool audio_wait_cs43131_pdn_done(uint8_t* interrupt_status) {
    for(uint32_t elapsed_ms = 0; elapsed_ms < CS43131_PDN_DONE_TIMEOUT_MS;
        elapsed_ms += AUDIO_PDN_DONE_POLL_PERIOD_MS) {
        furi_delay_ms(AUDIO_PDN_DONE_POLL_PERIOD_MS);

        uint8_t status;
        furi_hal_i2c_acquire(&furi_hal_i2c_handle_internal);
        const bool success = cs43131_read_interrupt_status(&furi_hal_i2c_handle_internal, &status);
        furi_hal_i2c_release(&furi_hal_i2c_handle_internal);

        if(!success) {
            return false;
        }
        *interrupt_status |= status;
        if(status & CS43131_INTERRUPT_PDN_DONE) {
            return true;
        }
    }
    return false;
}

static void audio_power_down_headphone_output(Audio* audio) {
    if(!audio->headphone_output_powered) {
        return;
    }

    if(audio_playback_is_running(audio->playback)) {
        furi_event_loop_timer_stop(audio->playback_stats_timer);
        audio_playback_stop(audio->playback);
    }

    uint8_t interrupt_status = 0;
    bool success = false;
    do {
        if(!audio_run_codec_step(cs43131_power_down_begin)) break;
        if(!audio_wait_cs43131_pdn_done(&interrupt_status)) break;
        if(!audio_run_codec_step(cs43131_power_down_end)) break;
        success = true;
    } while(0);

    audio->headphone_output_powered = false;

    if(!success) {
        FURI_LOG_E(TAG, "CS43131 power down failed");
    }

    if(interrupt_status & (CS43131_INTERRUPT_HP_PLUG | CS43131_INTERRUPT_HP_UNPLUG)) {
        audio_handle_cs43131_interrupt(audio);
    }
}

static bool
    audio_configure_headphone_output(Cs43131SampleRate sample_rate, Cs43131BitDepth bit_depth) {
    furi_hal_i2c_acquire(&furi_hal_i2c_handle_internal);
    bool success = false;
    do {
        if(!cs43131_configure_asp(&furi_hal_i2c_handle_internal, sample_rate, bit_depth)) break;
        if(!cs43131_power_up_begin(&furi_hal_i2c_handle_internal)) break;
        success = true;
    } while(0);
    furi_hal_i2c_release(&furi_hal_i2c_handle_internal);

    return success;
}

static uint32_t audio_get_stream_mask(FuriHalUsbAudioStream stream) {
    return 1UL << stream;
}

static bool audio_is_clock_user(const Audio* audio, FuriHalUsbAudioStream stream) {
    return (audio->clock_users & audio_get_stream_mask(stream)) != 0;
}

static bool audio_is_clock_held_by_other(const Audio* audio, FuriHalUsbAudioStream stream) {
    return (audio->clock_users & ~audio_get_stream_mask(stream)) != 0;
}

static Cs43131MclkSource audio_get_mclk_source(uint32_t sample_rate) {
    return cs43131_get_mclk_source(audio_get_cs43131_sample_rate(sample_rate));
}

static void audio_update_valid_sample_rates(const Audio* audio) {
    for(size_t stream = 0; stream < FuriHalUsbAudioStreamCount; stream++) {
        const FuriHalUsbAudioStreamConfig* config = audio_usb_configs[stream];
        const bool clock_held_by_other = audio_is_clock_held_by_other(audio, stream);
        uint32_t sample_rate_mask = 0;
        for(size_t i = 0; i < config->sample_rate_count; i++) {
            if(!clock_held_by_other ||
               audio_get_mclk_source(config->sample_rates[i]) == audio->mclk_source) {
                sample_rate_mask |= 1UL << i;
            }
        }
        furi_hal_usb_audio_set_valid_sample_rates(stream, sample_rate_mask);
    }
}

static bool
    audio_acquire_clock(Audio* audio, FuriHalUsbAudioStream stream, Cs43131MclkSource source) {
    if(audio_is_clock_held_by_other(audio, stream) && audio->mclk_source != source) {
        FURI_LOG_W(
            TAG,
            "Stream %d clock source %d unavailable, source %d in use",
            stream,
            source,
            audio->mclk_source);
        return false;
    }

    if(audio->mclk_source != source) {
        audio_power_down_headphone_output(audio);
        if(!audio_set_mclk_source(audio, source)) {
            FURI_LOG_E(TAG, "CS43131 clock source %d select failed", source);
            return false;
        }
    }

    audio->clock_users |= audio_get_stream_mask(stream);
    audio_update_valid_sample_rates(audio);
    return true;
}

static void audio_release_clock(Audio* audio, FuriHalUsbAudioStream stream) {
    audio->clock_users &= ~audio_get_stream_mask(stream);
    audio_update_valid_sample_rates(audio);
    furi_event_loop_timer_start(audio->idle_timer, furi_ms_to_ticks(AUDIO_IDLE_TIMEOUT_MS));
}

static void audio_start_speaker(Audio* audio, const FuriHalUsbAudioFormat* format) {
    const Cs43131SampleRate sample_rate = audio_get_cs43131_sample_rate(format->sample_rate);
    const Cs43131BitDepth bit_depth = audio_get_cs43131_bit_depth(format->sample_format);

    if(!audio_acquire_clock(
           audio, FuriHalUsbAudioStreamSpeaker, cs43131_get_mclk_source(sample_rate))) {
        return;
    }

    if(audio->headphone_output_powered && audio->sample_rate == sample_rate &&
       audio->bit_depth == bit_depth) {
        return;
    }

    audio_power_down_headphone_output(audio);

    bool success = false;
    do {
        if(!audio_configure_headphone_output(sample_rate, bit_depth)) break;
        audio->headphone_output_powered = true;
        audio_playback_start(audio->playback, format->sample_rate, format->sample_format);
        furi_event_loop_timer_start(
            audio->playback_stats_timer, furi_ms_to_ticks(AUDIO_PLAYBACK_STATS_PERIOD_MS));
        furi_delay_ms(CS43131_HP_POWER_UP_DELAY_MS);
        if(!audio_run_codec_step(cs43131_power_up_end)) break;
        success = true;
    } while(0);

    if(!success) {
        FURI_LOG_E(
            TAG,
            "Headphone output start failed, %lu Hz, %u bit",
            format->sample_rate,
            audio_get_sample_bits(format->sample_format));
        audio_power_down_headphone_output(audio);
        audio_release_clock(audio, FuriHalUsbAudioStreamSpeaker);
        return;
    }

    audio->sample_rate = sample_rate;
    audio->bit_depth = bit_depth;
    FURI_LOG_D(
        TAG,
        "Headphone output started, %lu Hz, %u bit",
        format->sample_rate,
        audio_get_sample_bits(format->sample_format));
}

static Es8327SampleRate audio_get_es8327_sample_rate(uint32_t sample_rate) {
    switch(sample_rate) {
    case 44100:
        return Es8327SampleRate44100;
    case 48000:
        return Es8327SampleRate48000;
    default:
        furi_crash();
    }
}

static Es8327WordLength audio_get_es8327_word_length(FuriHalUsbAudioSampleFormat sample_format) {
    switch(sample_format) {
    case FuriHalUsbAudioSampleFormat16Bit:
        return Es8327WordLength16;
    case FuriHalUsbAudioSampleFormat24Bit:
        return Es8327WordLength24;
    default:
        furi_crash();
    }
}

static bool audio_power_up_mic(const FuriHalUsbAudioFormat* format) {
    furi_hal_i2c_acquire(&furi_hal_i2c_handle_internal);
    bool success = false;
    do {
        if(!es8327_configure(
               &furi_hal_i2c_handle_internal,
               audio_get_es8327_sample_rate(format->sample_rate),
               audio_get_es8327_word_length(format->sample_format)))
            break;
        if(!es8327_power_up(&furi_hal_i2c_handle_internal)) break;
        success = true;
    } while(0);
    furi_hal_i2c_release(&furi_hal_i2c_handle_internal);

    return success;
}

static bool audio_set_mic_muted(bool muted) {
    furi_hal_i2c_acquire(&furi_hal_i2c_handle_internal);
    const bool success = es8327_set_adc_muted(&furi_hal_i2c_handle_internal, muted);
    furi_hal_i2c_release(&furi_hal_i2c_handle_internal);
    return success;
}

static void audio_power_down_mic(Audio* audio) {
    if(audio_capture_is_running(audio->capture)) {
        audio_capture_stop(audio->capture);
    }

    if(!audio_run_codec_step(es8327_power_down)) {
        FURI_LOG_E(TAG, "ES8327 power down failed");
    }
}

static void audio_start_mic(Audio* audio, const FuriHalUsbAudioFormat* format) {
    if(!audio_acquire_clock(
           audio, FuriHalUsbAudioStreamMic, audio_get_mclk_source(format->sample_rate))) {
        return;
    }

    bool success = false;
    do {
        if(!audio_power_up_mic(format)) break;
        if(!audio_set_mic_muted(furi_hal_usb_audio_is_muted(FuriHalUsbAudioStreamMic))) break;
        audio_capture_start(audio->capture, format->sample_rate, format->sample_format);
        success = true;
    } while(0);

    if(!success) {
        FURI_LOG_E(
            TAG,
            "Mic start failed, %lu Hz, %u bit",
            format->sample_rate,
            audio_get_sample_bits(format->sample_format));
        audio_power_down_mic(audio);
        audio_release_clock(audio, FuriHalUsbAudioStreamMic);
        return;
    }

    FURI_LOG_D(
        TAG,
        "Mic started, %lu Hz, %u bit",
        format->sample_rate,
        audio_get_sample_bits(format->sample_format));
}

static void audio_start_stream(Audio* audio, FuriHalUsbAudioStream stream) {
    const FuriHalUsbAudioFormat* format = &audio->streams[stream].format;

    switch(stream) {
    case FuriHalUsbAudioStreamSpeaker:
        audio_start_speaker(audio, format);
        break;
    case FuriHalUsbAudioStreamMic:
        audio_start_mic(audio, format);
        break;
    default:
        furi_crash();
    }
}

static void audio_start_pending_streams(Audio* audio) {
    if(audio->is_paused) {
        return;
    }
    for(size_t stream = 0; stream < FuriHalUsbAudioStreamCount; stream++) {
        if(audio->streams[stream].active && !audio_is_clock_user(audio, stream)) {
            audio_start_stream(audio, stream);
        }
    }
}

static bool audio_is_stream_unchanged(
    const AudioStream* audio_stream,
    bool active,
    const FuriHalUsbAudioFormat* format) {
    if(audio_stream->active != active) {
        return false;
    }
    if(!active) {
        return true;
    }
    return audio_stream->format.sample_rate == format->sample_rate &&
           audio_stream->format.sample_format == format->sample_format;
}

static void audio_handle_stream_change(Audio* audio, FuriHalUsbAudioStream stream) {
    AudioStream* audio_stream = &audio->streams[stream];
    FuriHalUsbAudioFormat format;
    const bool active = furi_hal_usb_audio_get_stream_format(stream, &format);

    if(audio_is_stream_unchanged(audio_stream, active, &format)) {
        return;
    }

    if(active) {
        FURI_LOG_I(
            TAG,
            "%s stream started, %lu Hz, %u bit",
            audio_stream_names[stream],
            format.sample_rate,
            audio_get_sample_bits(format.sample_format));
    } else {
        FURI_LOG_I(TAG, "%s stream stopped", audio_stream_names[stream]);
    }

    if(audio_is_clock_user(audio, stream)) {
        if(stream == FuriHalUsbAudioStreamMic) {
            audio_power_down_mic(audio);
            FURI_LOG_D(TAG, "Mic stopped");
        }
        audio_release_clock(audio, stream);
    }

    audio_stream->active = active;
    if(active) {
        audio_stream->format = format;
    }

    audio_start_pending_streams(audio);
}

static void audio_update_status(Audio* audio) {
    AudioStatus status = {
        .headphone_plugged = audio->headphone_plugged,
        .headset_mic_present = audio->headset_mic_present,
        .headphone_output_powered = audio->headphone_output_powered,
    };

    for(size_t stream = 0; stream < FuriHalUsbAudioStreamCount; stream++) {
        const AudioStream* audio_stream = &audio->streams[stream];
        AudioStreamStatus* stream_status = &status.streams[stream];
        stream_status->active = audio_stream->active;
        if(audio_stream->active) {
            stream_status->sample_rate = audio_stream->format.sample_rate;
            stream_status->sample_bits = audio_get_sample_bits(audio_stream->format.sample_format);
        }
        stream_status->volume = furi_hal_usb_audio_get_volume(stream);
        stream_status->muted = furi_hal_usb_audio_is_muted(stream);
        stream_status->i2s_error_count = audio->i2s_error_counts[stream];
    }

    furi_check(furi_mutex_acquire(audio->status_mutex, FuriWaitForever) == FuriStatusOk);
    audio->status = status;
    furi_mutex_release(audio->status_mutex);
}

static void audio_playback_stats_timer_callback(void* context) {
    Audio* audio = context;
    AudioPlaybackStats playback_stats;
    FuriHalUsbAudioSpeakerStats usb_stats;

    if(!audio_playback_is_running(audio->playback)) {
        return;
    }
    audio_playback_take_stats(audio->playback, &playback_stats);
    furi_hal_usb_audio_speaker_take_stats(&usb_stats);
    FURI_LOG_D(
        TAG,
        "Speaker FIFO read %u-%u B, SOF %u-%u B, packet %u-%u B, half %u B, underruns %lu, "
        "feedback 0x%06lX-0x%06lX, feedback resets %lu, SOF missed %lu",
        usb_stats.fifo_read_count_min,
        usb_stats.fifo_read_count_max,
        usb_stats.fifo_sof_count_min,
        usb_stats.fifo_sof_count_max,
        usb_stats.packet_min,
        usb_stats.packet_max,
        playback_stats.half_size,
        playback_stats.underruns,
        usb_stats.feedback_min,
        usb_stats.feedback_max,
        usb_stats.feedback_resets,
        usb_stats.sof_missed);
}

static void audio_idle_timer_callback(void* context) {
    Audio* audio = context;

    if(!audio_is_clock_user(audio, FuriHalUsbAudioStreamSpeaker) &&
       audio->headphone_output_powered) {
        audio_power_down_headphone_output(audio);
        FURI_LOG_D(TAG, "Headphone output stopped");
    }

    if(audio->clock_users == 0 && !audio_set_mclk_source(audio, Cs43131MclkSourceRco)) {
        FURI_LOG_E(TAG, "CS43131 RCO select failed");
    }

    audio_update_status(audio);
}

static void audio_stop_streams(Audio* audio) {
    if(audio_is_clock_user(audio, FuriHalUsbAudioStreamMic)) {
        audio_power_down_mic(audio);
        audio_release_clock(audio, FuriHalUsbAudioStreamMic);
    }
    if(audio_is_clock_user(audio, FuriHalUsbAudioStreamSpeaker)) {
        audio_power_down_headphone_output(audio);
        audio_release_clock(audio, FuriHalUsbAudioStreamSpeaker);
    }
}

static void audio_handle_pause(Audio* audio) {
    audio->is_paused = true;
    if(!audio->is_shut_down) {
        audio_stop_streams(audio);
        FURI_LOG_I(TAG, "Paused");
    }
    api_lock_unlock(audio->pause_lock);
}

static void audio_handle_resume(Audio* audio) {
    audio->is_paused = false;
    if(!audio->is_shut_down) {
        audio_start_pending_streams(audio);
        FURI_LOG_I(TAG, "Resumed");
    }
    api_lock_unlock(audio->pause_lock);
}

static void audio_handle_playback_error(Audio* audio) {
    if(!audio_playback_is_running(audio->playback)) {
        return;
    }

    const AudioPlaybackError* error = &audio->playback_error;
    audio->i2s_error_counts[FuriHalUsbAudioStreamSpeaker]++;
    FURI_LOG_W(TAG, "Headphone I2S %s, restart", audio_i2s_error_names[error->event]);
    FURI_LOG_D(
        TAG,
        "Headphone I2S error after %lu DMA halves, DMA position %u of %u",
        error->transferred_halves,
        error->dma_position,
        error->dma_length);
    audio_playback_restart(audio->playback);
}

static void audio_handle_capture_error(Audio* audio) {
    if(!audio_capture_is_running(audio->capture)) {
        return;
    }

    audio->i2s_error_counts[FuriHalUsbAudioStreamMic]++;
    FURI_LOG_W(TAG, "Mic I2S %s, restart", audio_i2s_error_names[audio->capture_error]);
    audio_capture_restart(audio->capture);
}

static void audio_log_volume(Audio* audio, FuriHalUsbAudioStream stream, int16_t volume) {
    AudioControls* controls = &audio->controls[stream];
    if(controls->volume_reported && controls->volume == volume) {
        return;
    }
    controls->volume_reported = true;
    controls->volume = volume;

    const int32_t volume_signed = volume;
    const uint32_t magnitude = volume_signed < 0 ? -volume_signed : volume_signed;

    FURI_LOG_I(
        TAG,
        "%s volume %s%lu.%02lu dB",
        audio_stream_names[stream],
        volume_signed < 0 ? "-" : "",
        magnitude / FURI_HAL_USB_AUDIO_VOLUME_PER_DB,
        magnitude % FURI_HAL_USB_AUDIO_VOLUME_PER_DB * AUDIO_VOLUME_LOG_FRACTION_SCALE /
            FURI_HAL_USB_AUDIO_VOLUME_PER_DB);
}

static void audio_log_mute(Audio* audio, FuriHalUsbAudioStream stream, bool muted) {
    AudioControls* controls = &audio->controls[stream];
    if(controls->mute_reported && controls->muted == muted) {
        return;
    }
    controls->mute_reported = true;
    controls->muted = muted;

    FURI_LOG_I(TAG, "%s %s", audio_stream_names[stream], muted ? "muted" : "unmuted");
}

static uint8_t audio_get_speaker_attenuation(int16_t usb_volume, int8_t volume_offset) {
    const FuriHalUsbAudioVolumeRange* range = &audio_speaker_usb_config.volume_range;
    const int32_t offset_volume =
        volume_offset * AUDIO_SPEAKER_VOLUME_OFFSET_STEP_DB * FURI_HAL_USB_AUDIO_VOLUME_PER_DB;
    const int32_t volume = CLAMP((int32_t)usb_volume + offset_volume, range->max, range->min);
    return AUDIO_SPEAKER_ATTENUATION(volume);
}

static void audio_handle_speaker_volume(Audio* audio) {
    const int16_t volume = furi_hal_usb_audio_get_volume(FuriHalUsbAudioStreamSpeaker);
    audio_log_volume(audio, FuriHalUsbAudioStreamSpeaker, volume);

    furi_hal_i2c_acquire(&furi_hal_i2c_handle_internal);
    const bool success = cs43131_set_volume_attenuation(
        &furi_hal_i2c_handle_internal,
        audio_get_speaker_attenuation(volume, audio->speaker_volume_offset));
    furi_hal_i2c_release(&furi_hal_i2c_handle_internal);

    if(!success) {
        FURI_LOG_E(
            TAG,
            "CS43131 volume write failed, %d, offset %d",
            volume,
            audio->speaker_volume_offset);
    }
}

static void audio_handle_mic_gain(Audio* audio) {
    furi_hal_i2c_acquire(&furi_hal_i2c_handle_internal);
    const bool success = es8327_set_mic_gain(&furi_hal_i2c_handle_internal, audio->mic_gain);
    furi_hal_i2c_release(&furi_hal_i2c_handle_internal);

    if(!success) {
        FURI_LOG_E(TAG, "ES8327 mic gain write failed, %u", audio->mic_gain);
    }
}

static void audio_load_settings(Audio* audio) {
    Settings settings;
    settings_store_get(audio->settings_store, &settings);

    if(audio->speaker_volume_offset != settings.speaker_volume_offset) {
        audio->speaker_volume_offset = settings.speaker_volume_offset;
        FURI_LOG_I(
            TAG,
            "Speaker volume offset %d dB",
            audio->speaker_volume_offset * AUDIO_SPEAKER_VOLUME_OFFSET_STEP_DB);
    }

    if(audio->mic_gain != settings.mic_gain) {
        audio->mic_gain = settings.mic_gain;
        FURI_LOG_I(
            TAG,
            "Mic gain %u dB",
            ES8327_MIC_GAIN_SE_MODE_DB + audio->mic_gain * ES8327_MIC_GAIN_STEP_DB);
    }
}

static void audio_handle_settings(Audio* audio) {
    const int8_t speaker_volume_offset = audio->speaker_volume_offset;
    const uint8_t mic_gain = audio->mic_gain;

    audio_load_settings(audio);

    if(audio->speaker_volume_offset != speaker_volume_offset) {
        audio_handle_speaker_volume(audio);
    }
    if(audio->mic_gain != mic_gain) {
        audio_handle_mic_gain(audio);
    }
}

static void audio_settings_callback(const void* message, void* context) {
    UNUSED(message);
    Audio* audio = context;
    furi_thread_flags_set(audio->thread_id, AudioThreadFlagSettings);
}

static void audio_handle_speaker_mute(Audio* audio) {
    const bool muted = furi_hal_usb_audio_is_muted(FuriHalUsbAudioStreamSpeaker);
    audio_log_mute(audio, FuriHalUsbAudioStreamSpeaker, muted);

    furi_hal_i2c_acquire(&furi_hal_i2c_handle_internal);
    const bool success = cs43131_set_muted(&furi_hal_i2c_handle_internal, muted);
    furi_hal_i2c_release(&furi_hal_i2c_handle_internal);

    if(!success) {
        FURI_LOG_E(TAG, "CS43131 mute write failed, %d", muted);
    }
}

static void audio_handle_mic_volume(Audio* audio) {
    const int16_t volume = furi_hal_usb_audio_get_volume(FuriHalUsbAudioStreamMic);
    audio_log_volume(audio, FuriHalUsbAudioStreamMic, volume);

    furi_hal_i2c_acquire(&furi_hal_i2c_handle_internal);
    const bool success =
        es8327_set_adc_volume(&furi_hal_i2c_handle_internal, AUDIO_MIC_ADC_VOLUME(volume));
    furi_hal_i2c_release(&furi_hal_i2c_handle_internal);

    if(!success) {
        FURI_LOG_E(TAG, "ES8327 volume write failed, %d", volume);
    }
}

static void audio_handle_mic_mute(Audio* audio) {
    const bool muted = furi_hal_usb_audio_is_muted(FuriHalUsbAudioStreamMic);
    audio_log_mute(audio, FuriHalUsbAudioStreamMic, muted);

    if(!audio_capture_is_running(audio->capture)) {
        return;
    }

    if(!audio_set_mic_muted(muted)) {
        FURI_LOG_E(TAG, "ES8327 mute write failed, %d", muted);
    }
}

static void audio_usb_change_callback(
    FuriHalUsbAudioStream stream,
    FuriHalUsbAudioChange change,
    void* context) {
    Audio* audio = context;
    const uint32_t flag = audio_usb_change_flags[stream][change];
    if(flag != 0) {
        furi_thread_flags_set(audio->thread_id, flag);
    }
}

static void audio_power_off(Audio* audio) {
    furi_event_loop_timer_stop(audio->idle_timer);
    furi_event_loop_timer_stop(audio->mic_detect_timer);
    audio_power_down_headphone_output(audio);
    if(audio_is_clock_user(audio, FuriHalUsbAudioStreamMic)) {
        audio_power_down_mic(audio);
    }
    audio->is_shut_down = true;
    FURI_LOG_I(TAG, "Shut down");
}

static void audio_thread_flags_callback(void* context) {
    Audio* audio = context;

    const uint32_t flags = furi_thread_flags_get();
    furi_thread_flags_clear(flags);

    if(flags & AudioThreadFlagShutdown) {
        if(!audio->is_shut_down) {
            audio_power_off(audio);
        }
        api_lock_unlock(audio->shutdown_lock);
        return;
    }

    if(flags & AudioThreadFlagPause) {
        audio_handle_pause(audio);
    }

    if(flags & AudioThreadFlagResume) {
        audio_handle_resume(audio);
    }

    if(audio->is_shut_down) {
        return;
    }

    if(flags & AudioThreadFlagCs43131Interrupt) {
        audio_handle_cs43131_interrupt(audio);
    }

    if(flags & AudioThreadFlagPlaybackError) {
        audio_handle_playback_error(audio);
    }

    if(flags & AudioThreadFlagSpeakerStream) {
        audio_handle_stream_change(audio, FuriHalUsbAudioStreamSpeaker);
    }

    if(flags & AudioThreadFlagMicStream) {
        audio_handle_stream_change(audio, FuriHalUsbAudioStreamMic);
    }

    if(flags & AudioThreadFlagSpeakerVolume) {
        audio_handle_speaker_volume(audio);
    }

    if(flags & AudioThreadFlagSettings) {
        audio_handle_settings(audio);
    }

    if(flags & AudioThreadFlagSpeakerMute) {
        audio_handle_speaker_mute(audio);
    }

    if(flags & AudioThreadFlagCaptureError) {
        audio_handle_capture_error(audio);
    }

    if(flags & AudioThreadFlagMicVolume) {
        audio_handle_mic_volume(audio);
    }

    if(flags & AudioThreadFlagMicMute) {
        audio_handle_mic_mute(audio);
    }

    audio_update_status(audio);
}

static void audio_init(Audio* audio) {
    furi_record_create(RECORD_HEADSET_EVENTS, audio->headset_events);
    furi_record_create(RECORD_AUDIO, audio);

    audio->settings_store = furi_record_open(RECORD_SETTINGS_STORE);
    audio->settings_subscription = furi_pubsub_subscribe(
        settings_store_get_pubsub(audio->settings_store), audio_settings_callback, audio);
    audio_load_settings(audio);

    furi_event_loop_subscribe_thread_flags(audio->event_loop, audio_thread_flags_callback, audio);
    furi_hal_gpio_add_int_callback(&gpio_cs43131_int, audio_cs43131_interrupt_callback, audio);

    furi_hal_usb_audio_set_config(&audio_speaker_usb_config, &audio_mic_usb_config);
    furi_hal_usb_audio_set_callback(audio_usb_change_callback, audio);

    audio_cs43131_reset();

    furi_hal_i2c_acquire(&furi_hal_i2c_handle_internal);
    const bool cs43131_ready = cs43131_init(&furi_hal_i2c_handle_internal);
    const bool es8327_ready = es8327_init(&furi_hal_i2c_handle_internal);
    furi_hal_i2c_release(&furi_hal_i2c_handle_internal);

    if(cs43131_ready) {
        audio_handle_cs43131_interrupt(audio);
        audio_handle_speaker_volume(audio);
        audio_handle_speaker_mute(audio);
    } else {
        FURI_LOG_E(TAG, "CS43131 init failed");
    }

    if(es8327_ready) {
        audio_handle_mic_gain(audio);
        audio_handle_mic_volume(audio);
    } else {
        FURI_LOG_E(TAG, "ES8327 init failed");
    }

    audio_update_status(audio);
}

static void audio_deinit(Audio* audio) {
    furi_hal_usb_audio_set_callback(NULL, NULL);
    furi_hal_gpio_remove_int_callback(&gpio_cs43131_int);
    furi_event_loop_unsubscribe_thread_flags(audio->event_loop);
    furi_event_loop_timer_stop(audio->idle_timer);
    furi_event_loop_timer_stop(audio->mic_detect_timer);
    furi_event_loop_timer_stop(audio->playback_stats_timer);
    furi_pubsub_unsubscribe(
        settings_store_get_pubsub(audio->settings_store), audio->settings_subscription);
    furi_record_close(RECORD_SETTINGS_STORE);

    if(audio_playback_is_running(audio->playback)) {
        audio_playback_stop(audio->playback);
    }

    if(audio_capture_is_running(audio->capture)) {
        audio_capture_stop(audio->capture);
    }

    furi_hal_gpio_write(&gpio_cs43131_reset_n, false);
    audio_set_oscillator(Cs43131MclkSourceRco);

    furi_record_destroy(RECORD_AUDIO);
    furi_record_destroy(RECORD_HEADSET_EVENTS);
}

void audio_shutdown(Audio* audio) {
    furi_check(audio);

    audio->shutdown_lock = api_lock_alloc_locked();
    furi_thread_flags_set(audio->thread_id, AudioThreadFlagShutdown);
    api_lock_wait_unlock_and_free(audio->shutdown_lock);
}

static void audio_send_and_wait(Audio* audio, AudioThreadFlag flag) {
    audio->pause_lock = api_lock_alloc_locked();
    furi_thread_flags_set(audio->thread_id, flag);
    api_lock_wait_unlock_and_free(audio->pause_lock);
}

void audio_pause(Audio* audio) {
    furi_check(audio);

    audio_send_and_wait(audio, AudioThreadFlagPause);
}

void audio_resume(Audio* audio) {
    furi_check(audio);

    audio_send_and_wait(audio, AudioThreadFlagResume);
}

void audio_get_status(Audio* audio, AudioStatus* status) {
    furi_check(audio);
    furi_check(status);

    furi_check(furi_mutex_acquire(audio->status_mutex, FuriWaitForever) == FuriStatusOk);
    *status = audio->status;
    furi_mutex_release(audio->status_mutex);
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
