#include <applications/services/audio/audio.h>
#include <applications/services/audio/audio_playback.h>

#include <furi.h>
#include <furi_hal.h>
#include <furi_hal_usb_audio.h>

#include <drivers/cs43131.h>
#include <drivers/es8327.h>

#define TAG "AudioSrv"

#define AUDIO_IDLE_TIMEOUT_MS              30000
#define AUDIO_OSCILLATOR_SWITCH_TURN_ON_US 200
#define AUDIO_OSCILLATOR_STARTUP_MS        5
#define AUDIO_PDN_DONE_POLL_PERIOD_MS      1

#define AUDIO_SPEAKER_USB_VOLUME(attenuation) \
    ((int16_t)(-(attenuation) * FURI_HAL_USB_AUDIO_VOLUME_PER_DB / CS43131_VOLUME_STEPS_PER_DB))

#define AUDIO_SPEAKER_ATTENUATION(usb_volume) \
    ((uint8_t)(-(usb_volume) * CS43131_VOLUME_STEPS_PER_DB / FURI_HAL_USB_AUDIO_VOLUME_PER_DB))

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

static const FuriHalUsbAudioStreamConfig* const audio_usb_configs[FuriHalUsbAudioStreamCount] = {
    [FuriHalUsbAudioStreamSpeaker] = &audio_speaker_usb_config,
    [FuriHalUsbAudioStreamMic] = &audio_mic_usb_config,
};

typedef enum {
    AudioThreadFlagCs43131Interrupt = (1 << 0),
    AudioThreadFlagPlaybackError = (1 << 1),
    AudioThreadFlagSpeakerStream = (1 << 2),
    AudioThreadFlagSpeakerVolume = (1 << 3),
    AudioThreadFlagSpeakerMute = (1 << 4),
    AudioThreadFlagMicStream = (1 << 5),
} AudioThreadFlag;

static const uint32_t
    audio_usb_change_flags[FuriHalUsbAudioStreamCount][FuriHalUsbAudioChangeCount] = {
        [FuriHalUsbAudioStreamSpeaker] =
            {
                [FuriHalUsbAudioChangeStream] = AudioThreadFlagSpeakerStream,
                [FuriHalUsbAudioChangeVolume] = AudioThreadFlagSpeakerVolume,
                [FuriHalUsbAudioChangeMute] = AudioThreadFlagSpeakerMute,
            },
        // TODO: mic volume and mute
        [FuriHalUsbAudioStreamMic] =
            {
                [FuriHalUsbAudioChangeStream] = AudioThreadFlagMicStream,
            },
};

typedef bool (*AudioCodecStep)(const FuriHalI2cBusHandle* handle);

typedef struct {
    bool active;
    FuriHalUsbAudioFormat format;
} AudioStream;

typedef struct {
    FuriThreadId thread_id;
    FuriEventLoop* event_loop;
    FuriEventLoopTimer* idle_timer;
    FuriPubSub* headset_events;
    AudioPlayback* playback;
    bool headphone_plugged;
    AudioStream streams[FuriHalUsbAudioStreamCount];
    uint32_t clock_users;
    Cs43131MclkSource mclk_source;
    bool headphone_output_powered;
    Cs43131SampleRate sample_rate;
    Cs43131BitDepth bit_depth;
} Audio;

static void audio_idle_timer_callback(void* context);

static void audio_playback_error_callback(void* context) {
    Audio* audio = context;
    furi_thread_flags_set(audio->thread_id, AudioThreadFlagPlaybackError);
}

static Audio* audio_alloc(void) {
    Audio* audio = malloc(sizeof(Audio));

    audio->thread_id = furi_thread_get_current_id();
    audio->event_loop = furi_event_loop_alloc();
    audio->idle_timer = furi_event_loop_timer_alloc(
        audio->event_loop, audio_idle_timer_callback, FuriEventLoopTimerTypeOnce, audio);
    audio->headset_events = furi_pubsub_alloc();
    audio->playback = audio_playback_alloc(audio_playback_error_callback, audio);
    audio->headphone_plugged = false;
    for(size_t stream = 0; stream < FuriHalUsbAudioStreamCount; stream++) {
        audio->streams[stream].active = false;
    }
    audio->clock_users = 0;
    audio->mclk_source = Cs43131MclkSourceRco;
    audio->headphone_output_powered = false;

    return audio;
}

static void audio_free(Audio* audio) {
    audio_playback_free(audio->playback);
    furi_pubsub_free(audio->headset_events);
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
        furi_delay_ms(CS43131_HP_POWER_UP_DELAY_MS);
        if(!audio_run_codec_step(cs43131_power_up_end)) break;
        success = true;
    } while(0);

    if(!success) {
        FURI_LOG_E(
            TAG,
            "Headphone output start failed, %lu Hz, sample format %d",
            format->sample_rate,
            format->sample_format);
        audio_power_down_headphone_output(audio);
        audio_release_clock(audio, FuriHalUsbAudioStreamSpeaker);
        return;
    }

    audio->sample_rate = sample_rate;
    audio->bit_depth = bit_depth;
    FURI_LOG_I(
        TAG,
        "Headphone output started, %lu Hz, sample format %d",
        format->sample_rate,
        format->sample_format);
}

static void audio_start_mic(Audio* audio, const FuriHalUsbAudioFormat* format) {
    if(!audio_acquire_clock(
           audio, FuriHalUsbAudioStreamMic, audio_get_mclk_source(format->sample_rate))) {
        return;
    }

    // TODO: ES8327 capture
    FURI_LOG_I(
        TAG,
        "Mic clock acquired, %lu Hz, sample format %d",
        format->sample_rate,
        format->sample_format);
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

    if(audio_is_clock_user(audio, stream)) {
        // TODO: ES8327 capture stop for the mic
        audio_release_clock(audio, stream);
    }

    audio_stream->active = active;
    if(active) {
        audio_stream->format = format;
    }

    audio_start_pending_streams(audio);
}

static void audio_idle_timer_callback(void* context) {
    Audio* audio = context;

    if(!audio_is_clock_user(audio, FuriHalUsbAudioStreamSpeaker) &&
       audio->headphone_output_powered) {
        audio_power_down_headphone_output(audio);
        FURI_LOG_I(TAG, "Headphone output stopped");
    }

    if(audio->clock_users == 0 && !audio_set_mclk_source(audio, Cs43131MclkSourceRco)) {
        FURI_LOG_E(TAG, "CS43131 RCO select failed");
    }
}

static void audio_handle_playback_error(Audio* audio) {
    if(!audio_playback_is_running(audio->playback)) {
        return;
    }

    FURI_LOG_W(TAG, "Headphone I2S error, restart");
    audio_playback_restart(audio->playback);
}

static void audio_handle_speaker_volume(void) {
    const int16_t volume = furi_hal_usb_audio_get_volume(FuriHalUsbAudioStreamSpeaker);

    furi_hal_i2c_acquire(&furi_hal_i2c_handle_internal);
    const bool success = cs43131_set_volume_attenuation(
        &furi_hal_i2c_handle_internal, AUDIO_SPEAKER_ATTENUATION(volume));
    furi_hal_i2c_release(&furi_hal_i2c_handle_internal);

    if(!success) {
        FURI_LOG_E(TAG, "CS43131 volume write failed, %d", volume);
    }
}

static void audio_handle_speaker_mute(void) {
    const bool muted = furi_hal_usb_audio_is_muted(FuriHalUsbAudioStreamSpeaker);

    furi_hal_i2c_acquire(&furi_hal_i2c_handle_internal);
    const bool success = cs43131_set_muted(&furi_hal_i2c_handle_internal, muted);
    furi_hal_i2c_release(&furi_hal_i2c_handle_internal);

    if(!success) {
        FURI_LOG_E(TAG, "CS43131 mute write failed, %d", muted);
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

static void audio_thread_flags_callback(void* context) {
    Audio* audio = context;

    const uint32_t flags = furi_thread_flags_get();
    furi_thread_flags_clear(flags);

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
        audio_handle_speaker_volume();
    }

    if(flags & AudioThreadFlagSpeakerMute) {
        audio_handle_speaker_mute();
    }
}

static void audio_init(Audio* audio) {
    furi_record_create(RECORD_HEADSET_EVENTS, audio->headset_events);

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
        audio_handle_speaker_volume();
        audio_handle_speaker_mute();
    } else {
        FURI_LOG_E(TAG, "CS43131 init failed");
    }

    if(!es8327_ready) {
        FURI_LOG_E(TAG, "ES8327 init failed");
    }

    // TODO: enable only in audio mode
    furi_hal_usb_enable();
}

static void audio_deinit(Audio* audio) {
    furi_hal_usb_audio_set_callback(NULL, NULL);
    furi_hal_gpio_remove_int_callback(&gpio_cs43131_int);
    furi_event_loop_unsubscribe_thread_flags(audio->event_loop);
    furi_event_loop_timer_stop(audio->idle_timer);

    if(audio_playback_is_running(audio->playback)) {
        audio_playback_stop(audio->playback);
    }

    furi_hal_gpio_write(&gpio_cs43131_reset_n, false);
    audio_set_oscillator(Cs43131MclkSourceRco);

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
