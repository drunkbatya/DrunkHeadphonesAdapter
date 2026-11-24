#include <furi_hal_usb_audio.h>
#include <furi_hal_usb_i.h>

#include <furi.h>

#include <tusb.h>
#include <device/dcd.h>

#define FURI_HAL_USB_AUDIO_SAMPLE_RATE_RESOLUTION_DISCRETE 0
#define FURI_HAL_USB_AUDIO_VOLUME_DEFAULT                  0
#define FURI_HAL_USB_AUDIO_MASTER_CHANNEL                  0
#define FURI_HAL_USB_AUDIO_VOLUME_RANGE_COUNT              1
#define FURI_HAL_USB_AUDIO_INTERRUPT_INFO_CLASS_INTERFACE  0x00
#define FURI_HAL_USB_AUDIO_CHANNEL_NAMES_NONE              0

#define FURI_HAL_USB_AUDIO_USB_FRAMES_PER_SECOND           1000
#define FURI_HAL_USB_AUDIO_FEEDBACK_FRACTION_BITS          16
#define FURI_HAL_USB_AUDIO_FEEDBACK_STEP_FRAMES            16
#define FURI_HAL_USB_AUDIO_FEEDBACK_STEP_COUNT             64
#define FURI_HAL_USB_AUDIO_FEEDBACK_WINDOW_SHIFT           10
#define FURI_HAL_USB_AUDIO_FEEDBACK_LEVEL_TARGET_MS        2
#define FURI_HAL_USB_AUDIO_FEEDBACK_LEVEL_CORRECTION_SHIFT 10
#define FURI_HAL_USB_AUDIO_FEEDBACK_DEVIATION_MAX          (1 << FURI_HAL_USB_AUDIO_FEEDBACK_FRACTION_BITS)
#define FURI_HAL_USB_AUDIO_FRAME_NUMBER_MASK               0x7FF

_Static_assert(
    FURI_HAL_USB_AUDIO_FEEDBACK_STEP_FRAMES* FURI_HAL_USB_AUDIO_FEEDBACK_STEP_COUNT ==
        (1 << FURI_HAL_USB_AUDIO_FEEDBACK_WINDOW_SHIFT),
    "Feedback window != STEP_FRAMES * STEP_COUNT");

typedef struct {
    const uint32_t* sample_rates;
    size_t sample_rate_count;
    uint32_t sample_rate;
    uint32_t valid_sample_rate_mask;
} FuriHalUsbAudioClock;

typedef struct {
    FuriHalUsbAudioVolumeRange volume_range;
    int16_t volume;
    bool muted;
} FuriHalUsbAudioFeatureUnit;

typedef struct {
    FuriHalUsbAudioClock clock;
    FuriHalUsbAudioFeatureUnit feature_unit;
    FuriHalUsbAudioAltSetting alt_setting;
} FuriHalUsbAudioStreamState;

typedef struct {
    uint8_t entity_id;
    uint8_t channel_count;
    uint32_t channel_config;
} FuriHalUsbAudioJackTerminal;

static const FuriHalUsbAudioJackTerminal
    furi_hal_usb_audio_jack_terminals[FuriHalUsbAudioStreamCount] = {
        [FuriHalUsbAudioStreamSpeaker] =
            {
                .entity_id = FuriHalUsbAudioEntitySpeakerOutputTerminal,
                .channel_count = FURI_HAL_USB_AUDIO_SPEAKER_CHANNELS,
                .channel_config = FURI_HAL_USB_AUDIO_SPEAKER_CHANNEL_CONFIG,
            },
        [FuriHalUsbAudioStreamMic] =
            {
                .entity_id = FuriHalUsbAudioEntityMicInputTerminal,
                .channel_count = FURI_HAL_USB_AUDIO_MIC_CHANNELS,
                .channel_config = FURI_HAL_USB_AUDIO_MIC_CHANNEL_CONFIG,
            },
};

typedef struct {
    uint32_t reader_generation;
    uint32_t played_frames[FURI_HAL_USB_AUDIO_FEEDBACK_STEP_COUNT];
    size_t step_index;
    size_t step_count;
    size_t step_frame_count;
    uint32_t fifo_level_sum;
    bool frame_number_valid;
    uint32_t frame_number;
    uint16_t fifo_write_index;
    FuriHalUsbAudioSpeakerStats stats;
} FuriHalUsbAudioFeedback;

typedef struct {
    bool configured;
    FuriHalUsbAudioCallback callback;
    void* callback_context;
    FuriHalUsbAudioStreamState streams[FuriHalUsbAudioStreamCount];
    bool speaker_attached;
    FuriHalUsbAudioFormat speaker_reader_format;
    FuriHalUsbAudioSpeakerPlayedCallback speaker_played_callback;
    void* speaker_played_context;
    uint32_t speaker_reader_generation;
    bool mic_attached;
    FuriHalUsbAudioFormat mic_writer_format;
    volatile bool bus_reset_pending;
    volatile bool speaker_readable;
    volatile bool mic_writable;
    volatile bool jack_connected[FuriHalUsbAudioStreamCount];
    volatile uint32_t jack_notification_pending;
    FuriHalUsbAudioFeedback feedback;
} FuriHalUsbAudio;

static FuriHalUsbAudio usb_audio = {0};

static uint32_t furi_hal_usb_audio_get_nominal_feedback(uint32_t sample_rate);

static void furi_hal_usb_audio_set_nominal_feedback(void) {
    tud_audio_n_fb_set(
        0,
        furi_hal_usb_audio_get_nominal_feedback(
            usb_audio.streams[FuriHalUsbAudioStreamSpeaker].clock.sample_rate));
}

static uint16_t furi_hal_usb_audio_get_speaker_fifo_count(void) {
    const tu_fifo_t* speaker_fifo = tud_audio_get_ep_out_ff();
    return tu_ff_overflow_count(speaker_fifo->depth, speaker_fifo->wr_idx, speaker_fifo->rd_idx);
}

static void furi_hal_usb_audio_check_sample_rates(
    const FuriHalUsbAudioStreamConfig* config,
    uint32_t max_sample_rate) {
    furi_check(config->sample_rates);
    furi_check(config->sample_rate_count > 0);
    furi_check(config->sample_rate_count <= FURI_HAL_USB_AUDIO_SAMPLE_RATE_MAX_COUNT);

    for(size_t i = 0; i < config->sample_rate_count; i++) {
        furi_check(config->sample_rates[i] <= max_sample_rate);
        if(i > 0) {
            furi_check(config->sample_rates[i] > config->sample_rates[i - 1]);
        }
    }
}

static void furi_hal_usb_audio_check_volume_range(const FuriHalUsbAudioVolumeRange* volume_range) {
    furi_check(volume_range->min < volume_range->max);
    furi_check(volume_range->resolution > 0);
}

static void furi_hal_usb_audio_apply_config(
    const FuriHalUsbAudioStreamConfig* config,
    FuriHalUsbAudioClock* clock,
    FuriHalUsbAudioFeatureUnit* feature_unit) {
    clock->sample_rates = config->sample_rates;
    clock->sample_rate_count = config->sample_rate_count;
    clock->sample_rate = config->sample_rates[0];
    clock->valid_sample_rate_mask = (1UL << config->sample_rate_count) - 1;

    feature_unit->volume_range = config->volume_range;
    feature_unit->volume = CLAMP(
        FURI_HAL_USB_AUDIO_VOLUME_DEFAULT, config->volume_range.max, config->volume_range.min);
    feature_unit->muted = false;
}

void furi_hal_usb_audio_set_config(
    const FuriHalUsbAudioStreamConfig* speaker,
    const FuriHalUsbAudioStreamConfig* mic) {
    furi_check(speaker);
    furi_check(mic);
    furi_check(!usb_audio.configured);

    furi_hal_usb_audio_check_sample_rates(speaker, FURI_HAL_USB_AUDIO_SPEAKER_MAX_SAMPLE_RATE);
    furi_hal_usb_audio_check_sample_rates(mic, FURI_HAL_USB_AUDIO_MIC_MAX_SAMPLE_RATE);
    furi_hal_usb_audio_check_volume_range(&speaker->volume_range);
    furi_hal_usb_audio_check_volume_range(&mic->volume_range);

    FuriHalUsbAudioStreamState* speaker_state = &usb_audio.streams[FuriHalUsbAudioStreamSpeaker];
    FuriHalUsbAudioStreamState* mic_state = &usb_audio.streams[FuriHalUsbAudioStreamMic];
    furi_hal_usb_audio_apply_config(speaker, &speaker_state->clock, &speaker_state->feature_unit);
    furi_hal_usb_audio_apply_config(mic, &mic_state->clock, &mic_state->feature_unit);
    usb_audio.configured = true;
    furi_hal_usb_audio_request_enable_update();
}

bool furi_hal_usb_audio_is_configured(void) {
    return usb_audio.configured;
}

void furi_hal_usb_audio_set_callback(FuriHalUsbAudioCallback callback, void* context) {
    usb_audio.callback = callback;
    usb_audio.callback_context = context;
}

int16_t furi_hal_usb_audio_get_volume(FuriHalUsbAudioStream stream) {
    furi_check(stream < FuriHalUsbAudioStreamCount);
    return usb_audio.streams[stream].feature_unit.volume;
}

bool furi_hal_usb_audio_is_muted(FuriHalUsbAudioStream stream) {
    furi_check(stream < FuriHalUsbAudioStreamCount);
    return usb_audio.streams[stream].feature_unit.muted;
}

void furi_hal_usb_audio_set_valid_sample_rates(
    FuriHalUsbAudioStream stream,
    uint32_t sample_rate_mask) {
    furi_check(stream < FuriHalUsbAudioStreamCount);
    FuriHalUsbAudioClock* clock = &usb_audio.streams[stream].clock;
    furi_check((sample_rate_mask >> clock->sample_rate_count) == 0);
    clock->valid_sample_rate_mask = sample_rate_mask;
}

static FuriHalUsbAudioSampleFormat
    furi_hal_usb_audio_get_sample_format(FuriHalUsbAudioAltSetting alt_setting) {
    switch(alt_setting) {
    case FuriHalUsbAudioAltSetting16Bit:
        return FuriHalUsbAudioSampleFormat16Bit;
    case FuriHalUsbAudioAltSetting24Bit:
        return FuriHalUsbAudioSampleFormat24Bit;
    default:
        furi_crash();
    }
}

bool furi_hal_usb_audio_get_stream_format(
    FuriHalUsbAudioStream stream,
    FuriHalUsbAudioFormat* format) {
    furi_check(stream < FuriHalUsbAudioStreamCount);
    furi_check(format);

    const FuriHalUsbAudioStreamState* state = &usb_audio.streams[stream];
    bool streaming = false;

    FURI_CRITICAL_ENTER();
    if(state->alt_setting != FuriHalUsbAudioAltSettingIdle) {
        format->sample_rate = state->clock.sample_rate;
        format->sample_format = furi_hal_usb_audio_get_sample_format(state->alt_setting);
        streaming = true;
    }
    FURI_CRITICAL_EXIT();

    return streaming;
}

static bool furi_hal_usb_audio_is_streaming(
    FuriHalUsbAudioStream stream,
    const FuriHalUsbAudioFormat* format) {
    const FuriHalUsbAudioStreamState* state = &usb_audio.streams[stream];
    return state->alt_setting != FuriHalUsbAudioAltSettingIdle &&
           state->clock.sample_rate == format->sample_rate &&
           furi_hal_usb_audio_get_sample_format(state->alt_setting) == format->sample_format;
}

static void furi_hal_usb_audio_update_access(void) {
    usb_audio.speaker_readable =
        usb_audio.speaker_attached && !usb_audio.bus_reset_pending &&
        furi_hal_usb_audio_is_streaming(
            FuriHalUsbAudioStreamSpeaker, &usb_audio.speaker_reader_format);
    usb_audio.mic_writable =
        usb_audio.mic_attached && !usb_audio.bus_reset_pending &&
        furi_hal_usb_audio_is_streaming(FuriHalUsbAudioStreamMic, &usb_audio.mic_writer_format);
}

void furi_hal_usb_audio_speaker_attach(
    const FuriHalUsbAudioFormat* format,
    FuriHalUsbAudioSpeakerPlayedCallback played_callback,
    void* context) {
    furi_check(format);
    furi_check(played_callback);

    FURI_CRITICAL_ENTER();
    usb_audio.speaker_reader_format = *format;
    usb_audio.speaker_played_callback = played_callback;
    usb_audio.speaker_played_context = context;
    usb_audio.speaker_reader_generation++;
    usb_audio.speaker_attached = true;
    furi_hal_usb_audio_update_access();
    FURI_CRITICAL_EXIT();
}

void furi_hal_usb_audio_speaker_detach(void) {
    FURI_CRITICAL_ENTER();
    usb_audio.speaker_attached = false;
    furi_hal_usb_audio_update_access();
    FURI_CRITICAL_EXIT();
}

size_t furi_hal_usb_audio_speaker_get_available(void) {
    furi_check(FURI_IS_IRQ_MODE());

    if(!usb_audio.speaker_readable) {
        return 0;
    }
    return tud_audio_available();
}

size_t furi_hal_usb_audio_speaker_read(void* data, size_t size) {
    furi_check(FURI_IS_IRQ_MODE());
    furi_check(data);
    furi_check(size <= UINT16_MAX);

    if(!usb_audio.speaker_readable) {
        return 0;
    }
    FuriHalUsbAudioSpeakerStats* stats = &usb_audio.feedback.stats;
    const uint16_t count = furi_hal_usb_audio_get_speaker_fifo_count();
    stats->fifo_read_count_min = MIN(stats->fifo_read_count_min, count);
    stats->fifo_read_count_max = MAX(stats->fifo_read_count_max, count);
    return tud_audio_read(data, (uint16_t)size);
}

void furi_hal_usb_audio_mic_attach(const FuriHalUsbAudioFormat* format) {
    furi_check(format);

    FURI_CRITICAL_ENTER();
    usb_audio.mic_writer_format = *format;
    usb_audio.mic_attached = true;
    furi_hal_usb_audio_update_access();
    FURI_CRITICAL_EXIT();
}

void furi_hal_usb_audio_mic_detach(void) {
    FURI_CRITICAL_ENTER();
    usb_audio.mic_attached = false;
    furi_hal_usb_audio_update_access();
    FURI_CRITICAL_EXIT();
}

size_t furi_hal_usb_audio_mic_write(const void* data, size_t size) {
    furi_check(FURI_IS_IRQ_MODE());
    furi_check(data);
    furi_check(size <= UINT16_MAX);

    if(!usb_audio.mic_writable) {
        return 0;
    }
    return tud_audio_write(data, (uint16_t)size);
}

static void furi_hal_usb_audio_notify(FuriHalUsbAudioStream stream, FuriHalUsbAudioChange change) {
    if(usb_audio.callback != NULL) {
        usb_audio.callback(stream, change, usb_audio.callback_context);
    }
}

static void furi_hal_usb_audio_set_alt_setting(
    FuriHalUsbAudioStream stream,
    FuriHalUsbAudioAltSetting alt_setting) {
    FuriHalUsbAudioStreamState* state = &usb_audio.streams[stream];
    if(state->alt_setting == alt_setting) {
        return;
    }

    FURI_CRITICAL_ENTER();
    state->alt_setting = alt_setting;
    furi_hal_usb_audio_update_access();
    FURI_CRITICAL_EXIT();

    if(stream == FuriHalUsbAudioStreamSpeaker) {
        dcd_sof_enable(FURI_HAL_USB_ROOT_PORT, alt_setting != FuriHalUsbAudioAltSettingIdle);
        if(alt_setting == FuriHalUsbAudioAltSettingIdle) {
            furi_hal_usb_audio_set_nominal_feedback();
        }
    }

    furi_hal_usb_audio_notify(stream, FuriHalUsbAudioChangeStream);
}

void furi_hal_usb_audio_reset(void) {
    for(size_t stream = 0; stream < FuriHalUsbAudioStreamCount; stream++) {
        furi_hal_usb_audio_set_alt_setting(stream, FuriHalUsbAudioAltSettingIdle);
    }

    FURI_CRITICAL_ENTER();
    usb_audio.jack_notification_pending = 0;
    usb_audio.bus_reset_pending = false;
    furi_hal_usb_audio_update_access();
    FURI_CRITICAL_EXIT();
}

void furi_hal_usb_audio_set_jack_connected(FuriHalUsbAudioStream stream, bool connected) {
    furi_check(stream < FuriHalUsbAudioStreamCount);

    FURI_CRITICAL_ENTER();
    const bool changed = usb_audio.jack_connected[stream] != connected;
    usb_audio.jack_connected[stream] = connected;
    if(changed) {
        usb_audio.jack_notification_pending |= 1UL << stream;
    }
    FURI_CRITICAL_EXIT();

    if(changed) {
        furi_hal_usb_audio_request_notification();
    }
}

static bool furi_hal_usb_audio_take_jack_notification(size_t* terminal_index) {
    bool taken = false;

    FURI_CRITICAL_ENTER();
    for(size_t i = 0; i < COUNT_OF(furi_hal_usb_audio_jack_terminals); i++) {
        if(usb_audio.jack_notification_pending & (1UL << i)) {
            usb_audio.jack_notification_pending &= ~(1UL << i);
            *terminal_index = i;
            taken = true;
            break;
        }
    }
    FURI_CRITICAL_EXIT();

    return taken;
}

static void furi_hal_usb_audio_restore_jack_notification(size_t terminal_index) {
    FURI_CRITICAL_ENTER();
    usb_audio.jack_notification_pending |= 1UL << terminal_index;
    FURI_CRITICAL_EXIT();
}

void furi_hal_usb_audio_send_notifications(void) {
    if(!tud_audio_mounted() || usb_audio.jack_notification_pending == 0) {
        return;
    }

    if(tud_suspended()) {
        furi_hal_usb_remote_wakeup();
        return;
    }

    size_t terminal_index;
    if(!furi_hal_usb_audio_take_jack_notification(&terminal_index)) {
        return;
    }

    const audio_interrupt_data_t message = {
        .v2 =
            {
                .bInfo = FURI_HAL_USB_AUDIO_INTERRUPT_INFO_CLASS_INTERFACE,
                .bAttribute = AUDIO20_CS_REQ_CUR,
                .wValue_cn_or_mcn = FURI_HAL_USB_AUDIO_MASTER_CHANNEL,
                .wValue_cs = AUDIO20_TE_CTRL_CONNECTOR,
                .wIndex_ep_or_int = FuriHalUsbInterfaceAudioControl,
                .wIndex_entity_id = furi_hal_usb_audio_jack_terminals[terminal_index].entity_id,
            },
    };

    if(!tud_audio_int_write(&message)) {
        furi_hal_usb_audio_restore_jack_notification(terminal_index);
    }
}

void tud_audio_int_done_cb(uint8_t rhport) {
    UNUSED(rhport);
    furi_hal_usb_audio_send_notifications();
}

void furi_hal_usb_audio_begin_bus_reset(void) {
    usb_audio.bus_reset_pending = true;
    usb_audio.speaker_readable = false;
    usb_audio.mic_writable = false;
}

void furi_hal_usb_audio_finish_bus_reset(void) {
    if(usb_audio.bus_reset_pending) {
        furi_hal_usb_audio_reset();
    }
}

static bool furi_hal_usb_audio_get_clock_stream(uint8_t entity_id, FuriHalUsbAudioStream* stream) {
    switch(entity_id) {
    case FuriHalUsbAudioEntitySpeakerClock:
        *stream = FuriHalUsbAudioStreamSpeaker;
        return true;
    case FuriHalUsbAudioEntityMicClock:
        *stream = FuriHalUsbAudioStreamMic;
        return true;
    default:
        return false;
    }
}

static bool
    furi_hal_usb_audio_get_feature_unit_stream(uint8_t entity_id, FuriHalUsbAudioStream* stream) {
    switch(entity_id) {
    case FuriHalUsbAudioEntitySpeakerFeatureUnit:
        *stream = FuriHalUsbAudioStreamSpeaker;
        return true;
    case FuriHalUsbAudioEntityMicFeatureUnit:
        *stream = FuriHalUsbAudioStreamMic;
        return true;
    default:
        return false;
    }
}

static bool
    furi_hal_usb_audio_get_interface_stream(uint8_t interface, FuriHalUsbAudioStream* stream) {
    switch(interface) {
    case FuriHalUsbInterfaceAudioSpeaker:
        *stream = FuriHalUsbAudioStreamSpeaker;
        return true;
    case FuriHalUsbInterfaceAudioMic:
        *stream = FuriHalUsbAudioStreamMic;
        return true;
    default:
        return false;
    }
}

static bool furi_hal_usb_audio_is_clock_valid(const FuriHalUsbAudioClock* clock) {
    for(size_t i = 0; i < clock->sample_rate_count; i++) {
        if(clock->sample_rates[i] == clock->sample_rate) {
            return (clock->valid_sample_rate_mask & (1UL << i)) != 0;
        }
    }
    furi_crash();
}

static bool
    furi_hal_usb_audio_get_jack_terminal_stream(uint8_t entity_id, FuriHalUsbAudioStream* stream) {
    for(size_t i = 0; i < COUNT_OF(furi_hal_usb_audio_jack_terminals); i++) {
        if(furi_hal_usb_audio_jack_terminals[i].entity_id == entity_id) {
            *stream = i;
            return true;
        }
    }
    return false;
}

static bool furi_hal_usb_audio_get_jack_request(
    uint8_t rhport,
    const tusb_control_request_t* request,
    FuriHalUsbAudioStream stream) {
    const FuriHalUsbAudioJackTerminal* terminal = &furi_hal_usb_audio_jack_terminals[stream];
    const uint8_t control_selector = TU_U16_HIGH(request->wValue);

    if(control_selector != AUDIO20_TE_CTRL_CONNECTOR || request->bRequest != AUDIO20_CS_REQ_CUR) {
        return false;
    }

    const bool connected = usb_audio.jack_connected[stream];
    audio20_desc_channel_cluster_t connected_channels = {
        .bNrChannels = connected ? terminal->channel_count : 0,
        .bmChannelConfig = connected ? terminal->channel_config : 0,
        .iChannelNames = FURI_HAL_USB_AUDIO_CHANNEL_NAMES_NONE,
    };
    return tud_audio_buffer_and_schedule_control_xfer(
        rhport, request, &connected_channels, sizeof(connected_channels));
}

static bool furi_hal_usb_audio_get_clock_request(
    uint8_t rhport,
    const tusb_control_request_t* request,
    const FuriHalUsbAudioClock* clock) {
    const uint8_t control_selector = TU_U16_HIGH(request->wValue);

    if(control_selector == AUDIO20_CS_CTRL_SAM_FREQ && request->bRequest == AUDIO20_CS_REQ_CUR) {
        audio20_control_cur_4_t sample_rate = {.bCur = (int32_t)clock->sample_rate};
        return tud_audio_buffer_and_schedule_control_xfer(
            rhport, request, &sample_rate, sizeof(sample_rate));
    }

    if(control_selector == AUDIO20_CS_CTRL_SAM_FREQ && request->bRequest == AUDIO20_CS_REQ_RANGE) {
        audio20_control_range_4_n_t(FURI_HAL_USB_AUDIO_SAMPLE_RATE_MAX_COUNT) sample_rate_range;
        sample_rate_range.wNumSubRanges = (uint16_t)clock->sample_rate_count;
        for(size_t i = 0; i < clock->sample_rate_count; i++) {
            sample_rate_range.subrange[i].bMin = (int32_t)clock->sample_rates[i];
            sample_rate_range.subrange[i].bMax = (int32_t)clock->sample_rates[i];
            sample_rate_range.subrange[i].bRes =
                FURI_HAL_USB_AUDIO_SAMPLE_RATE_RESOLUTION_DISCRETE;
        }
        const uint16_t length = sizeof(sample_rate_range.wNumSubRanges) +
                                clock->sample_rate_count * sizeof(sample_rate_range.subrange[0]);
        return tud_audio_buffer_and_schedule_control_xfer(
            rhport, request, &sample_rate_range, length);
    }

    if(control_selector == AUDIO20_CS_CTRL_CLK_VALID && request->bRequest == AUDIO20_CS_REQ_CUR) {
        audio20_control_cur_1_t clock_valid = {.bCur = furi_hal_usb_audio_is_clock_valid(clock)};
        return tud_audio_buffer_and_schedule_control_xfer(
            rhport, request, &clock_valid, sizeof(clock_valid));
    }

    return false;
}

static bool furi_hal_usb_audio_set_clock_request(
    const tusb_control_request_t* request,
    const uint8_t* buffer,
    FuriHalUsbAudioStream stream) {
    FuriHalUsbAudioStreamState* state = &usb_audio.streams[stream];
    const uint8_t control_selector = TU_U16_HIGH(request->wValue);

    if(control_selector != AUDIO20_CS_CTRL_SAM_FREQ || request->bRequest != AUDIO20_CS_REQ_CUR ||
       request->wLength != sizeof(audio20_control_cur_4_t)) {
        return false;
    }

    const uint32_t sample_rate = tu_unaligned_read32(buffer);
    for(size_t i = 0; i < state->clock.sample_rate_count; i++) {
        if(state->clock.sample_rates[i] != sample_rate) {
            continue;
        }
        if(state->clock.sample_rate == sample_rate) {
            return true;
        }
        FURI_CRITICAL_ENTER();
        state->clock.sample_rate = sample_rate;
        furi_hal_usb_audio_update_access();
        FURI_CRITICAL_EXIT();
        if(stream == FuriHalUsbAudioStreamSpeaker) {
            furi_hal_usb_audio_set_nominal_feedback();
        }
        if(state->alt_setting != FuriHalUsbAudioAltSettingIdle) {
            furi_hal_usb_audio_notify(stream, FuriHalUsbAudioChangeStream);
        }
        return true;
    }

    return false;
}

static bool furi_hal_usb_audio_get_feature_unit_request(
    uint8_t rhport,
    const tusb_control_request_t* request,
    const FuriHalUsbAudioFeatureUnit* feature_unit) {
    const uint8_t control_selector = TU_U16_HIGH(request->wValue);
    const uint8_t channel = TU_U16_LOW(request->wValue);

    if(channel != FURI_HAL_USB_AUDIO_MASTER_CHANNEL) {
        return false;
    }

    if(control_selector == AUDIO20_FU_CTRL_MUTE && request->bRequest == AUDIO20_CS_REQ_CUR) {
        audio20_control_cur_1_t muted = {.bCur = feature_unit->muted};
        return tud_audio_buffer_and_schedule_control_xfer(rhport, request, &muted, sizeof(muted));
    }

    if(control_selector == AUDIO20_FU_CTRL_VOLUME && request->bRequest == AUDIO20_CS_REQ_CUR) {
        audio20_control_cur_2_t volume = {.bCur = feature_unit->volume};
        return tud_audio_buffer_and_schedule_control_xfer(
            rhport, request, &volume, sizeof(volume));
    }

    if(control_selector == AUDIO20_FU_CTRL_VOLUME && request->bRequest == AUDIO20_CS_REQ_RANGE) {
        audio20_control_range_2_n_t(FURI_HAL_USB_AUDIO_VOLUME_RANGE_COUNT) volume_range = {
            .wNumSubRanges = FURI_HAL_USB_AUDIO_VOLUME_RANGE_COUNT,
            .subrange[0] =
                {
                    .bMin = feature_unit->volume_range.min,
                    .bMax = feature_unit->volume_range.max,
                    .bRes = feature_unit->volume_range.resolution,
                },
        };
        return tud_audio_buffer_and_schedule_control_xfer(
            rhport, request, &volume_range, sizeof(volume_range));
    }

    return false;
}

static bool furi_hal_usb_audio_set_feature_unit_request(
    const tusb_control_request_t* request,
    const uint8_t* buffer,
    FuriHalUsbAudioStream stream) {
    FuriHalUsbAudioFeatureUnit* feature_unit = &usb_audio.streams[stream].feature_unit;
    const uint8_t control_selector = TU_U16_HIGH(request->wValue);
    const uint8_t channel = TU_U16_LOW(request->wValue);

    if(channel != FURI_HAL_USB_AUDIO_MASTER_CHANNEL || request->bRequest != AUDIO20_CS_REQ_CUR) {
        return false;
    }

    if(control_selector == AUDIO20_FU_CTRL_MUTE &&
       request->wLength == sizeof(audio20_control_cur_1_t)) {
        feature_unit->muted = buffer[0] != 0;
        furi_hal_usb_audio_notify(stream, FuriHalUsbAudioChangeMute);
        return true;
    }

    if(control_selector == AUDIO20_FU_CTRL_VOLUME &&
       request->wLength == sizeof(audio20_control_cur_2_t)) {
        const int16_t volume = (int16_t)tu_unaligned_read16(buffer);
        feature_unit->volume =
            CLAMP(volume, feature_unit->volume_range.max, feature_unit->volume_range.min);
        furi_hal_usb_audio_notify(stream, FuriHalUsbAudioChangeVolume);
        return true;
    }

    return false;
}

bool tud_audio_get_req_entity_cb(uint8_t rhport, tusb_control_request_t const* p_request) {
    const uint8_t entity_id = TU_U16_HIGH(p_request->wIndex);
    FuriHalUsbAudioStream stream;

    if(furi_hal_usb_audio_get_clock_stream(entity_id, &stream)) {
        return furi_hal_usb_audio_get_clock_request(
            rhport, p_request, &usb_audio.streams[stream].clock);
    }

    if(furi_hal_usb_audio_get_feature_unit_stream(entity_id, &stream)) {
        return furi_hal_usb_audio_get_feature_unit_request(
            rhport, p_request, &usb_audio.streams[stream].feature_unit);
    }

    if(furi_hal_usb_audio_get_jack_terminal_stream(entity_id, &stream)) {
        return furi_hal_usb_audio_get_jack_request(rhport, p_request, stream);
    }

    return false;
}

bool tud_audio_set_req_entity_cb(
    uint8_t rhport,
    tusb_control_request_t const* p_request,
    uint8_t* pBuff) {
    UNUSED(rhport);
    const uint8_t entity_id = TU_U16_HIGH(p_request->wIndex);
    FuriHalUsbAudioStream stream;

    if(furi_hal_usb_audio_get_clock_stream(entity_id, &stream)) {
        return furi_hal_usb_audio_set_clock_request(p_request, pBuff, stream);
    }

    if(furi_hal_usb_audio_get_feature_unit_stream(entity_id, &stream)) {
        return furi_hal_usb_audio_set_feature_unit_request(p_request, pBuff, stream);
    }

    return false;
}

bool tud_audio_set_itf_cb(uint8_t rhport, tusb_control_request_t const* p_request) {
    UNUSED(rhport);
    const uint8_t interface = TU_U16_LOW(p_request->wIndex);
    const uint8_t alt_setting = TU_U16_LOW(p_request->wValue);
    FuriHalUsbAudioStream stream;

    if(!furi_hal_usb_audio_get_interface_stream(interface, &stream)) {
        return false;
    }

    furi_hal_usb_audio_set_alt_setting(stream, alt_setting);
    return true;
}

bool tud_audio_set_itf_close_ep_cb(uint8_t rhport, tusb_control_request_t const* p_request) {
    UNUSED(rhport);
    const uint8_t interface = TU_U16_LOW(p_request->wIndex);
    FuriHalUsbAudioStream stream;

    if(!furi_hal_usb_audio_get_interface_stream(interface, &stream)) {
        return false;
    }

    furi_hal_usb_audio_set_alt_setting(stream, FuriHalUsbAudioAltSettingIdle);
    return true;
}

static uint32_t furi_hal_usb_audio_get_nominal_feedback(uint32_t sample_rate) {
    return (uint32_t)((((uint64_t)sample_rate << FURI_HAL_USB_AUDIO_FEEDBACK_FRACTION_BITS) +
                       FURI_HAL_USB_AUDIO_USB_FRAMES_PER_SECOND / 2) /
                      FURI_HAL_USB_AUDIO_USB_FRAMES_PER_SECOND);
}

static size_t
    furi_hal_usb_audio_get_speaker_frame_size(FuriHalUsbAudioSampleFormat sample_format) {
    switch(sample_format) {
    case FuriHalUsbAudioSampleFormat16Bit:
        return FURI_HAL_USB_AUDIO_SPEAKER_CHANNELS * FURI_HAL_USB_AUDIO_16BIT_SUBSLOT_SIZE;
    case FuriHalUsbAudioSampleFormat24Bit:
        return FURI_HAL_USB_AUDIO_SPEAKER_CHANNELS * FURI_HAL_USB_AUDIO_24BIT_SUBSLOT_SIZE;
    default:
        furi_crash();
    }
}

static void furi_hal_usb_audio_feedback_stats_reset(FuriHalUsbAudioSpeakerStats* stats) {
    stats->feedback_min = UINT32_MAX;
    stats->feedback_max = 0;
    stats->feedback_resets = 0;
    stats->sof_missed = 0;
    stats->packet_min = UINT16_MAX;
    stats->packet_max = 0;
    stats->fifo_read_count_min = UINT16_MAX;
    stats->fifo_read_count_max = 0;
    stats->fifo_sof_count_min = UINT16_MAX;
    stats->fifo_sof_count_max = 0;
}

void furi_hal_usb_audio_speaker_take_stats(FuriHalUsbAudioSpeakerStats* stats) {
    furi_check(stats);

    FURI_CRITICAL_ENTER();
    *stats = usb_audio.feedback.stats;
    furi_hal_usb_audio_feedback_stats_reset(&usb_audio.feedback.stats);
    FURI_CRITICAL_EXIT();
}

static void furi_hal_usb_audio_feedback_set(
    FuriHalUsbAudioFeedback* feedback,
    uint8_t func_id,
    uint32_t value) {
    feedback->stats.feedback_min = MIN(feedback->stats.feedback_min, value);
    feedback->stats.feedback_max = MAX(feedback->stats.feedback_max, value);
    tud_audio_n_fb_set(func_id, value);
}

static void furi_hal_usb_audio_feedback_count_sof(
    FuriHalUsbAudioFeedback* feedback,
    uint32_t frame_number) {
    const tu_fifo_t* speaker_fifo = tud_audio_get_ep_out_ff();
    const uint16_t write_index = speaker_fifo->wr_idx;
    if(feedback->frame_number_valid) {
        const uint32_t frame_delta = (frame_number - feedback->frame_number) &
                                     FURI_HAL_USB_AUDIO_FRAME_NUMBER_MASK;
        if(frame_delta > 1) {
            feedback->stats.sof_missed += frame_delta - 1;
        }
        const uint16_t packet_size =
            (uint16_t)((write_index + 2 * speaker_fifo->depth - feedback->fifo_write_index) %
                       (2 * speaker_fifo->depth));
        feedback->stats.packet_min = MIN(feedback->stats.packet_min, packet_size);
        feedback->stats.packet_max = MAX(feedback->stats.packet_max, packet_size);
    }
    feedback->fifo_write_index = write_index;
    feedback->frame_number = frame_number;
    feedback->frame_number_valid = true;
}

static void furi_hal_usb_audio_feedback_reset(FuriHalUsbAudioFeedback* feedback) {
    feedback->reader_generation = usb_audio.speaker_reader_generation;
    feedback->step_index = 0;
    feedback->step_count = 0;
    feedback->step_frame_count = 0;
    feedback->fifo_level_sum = 0;
}

static int32_t furi_hal_usb_audio_get_fifo_level_correction(
    const FuriHalUsbAudioFeedback* feedback,
    const FuriHalUsbAudioFormat* format) {
    const int32_t frame_size =
        (int32_t)furi_hal_usb_audio_get_speaker_frame_size(format->sample_format);
    const int32_t level_frames =
        (int32_t)(feedback->fifo_level_sum / FURI_HAL_USB_AUDIO_FEEDBACK_STEP_FRAMES) / frame_size;
    const int32_t target_frames =
        (int32_t)(format->sample_rate * FURI_HAL_USB_AUDIO_FEEDBACK_LEVEL_TARGET_MS /
                  FURI_HAL_USB_AUDIO_USB_FRAMES_PER_SECOND);
    return (target_frames - level_frames) *
           (1
            << (FURI_HAL_USB_AUDIO_FEEDBACK_FRACTION_BITS -
                FURI_HAL_USB_AUDIO_FEEDBACK_LEVEL_CORRECTION_SHIFT));
}

static bool furi_hal_usb_audio_feedback_step(
    FuriHalUsbAudioFeedback* feedback,
    uint32_t played_frames,
    uint32_t nominal,
    uint32_t* value) {
    if(feedback->step_count > 0) {
        const size_t last_index =
            (feedback->step_index + FURI_HAL_USB_AUDIO_FEEDBACK_STEP_COUNT - 1) %
            FURI_HAL_USB_AUDIO_FEEDBACK_STEP_COUNT;
        if(feedback->played_frames[last_index] == played_frames) {
            return false;
        }
    }

    uint32_t rate = nominal;
    if(feedback->step_count == FURI_HAL_USB_AUDIO_FEEDBACK_STEP_COUNT) {
        const uint32_t window_frames =
            played_frames - feedback->played_frames[feedback->step_index];
        rate = window_frames
               << (FURI_HAL_USB_AUDIO_FEEDBACK_FRACTION_BITS -
                   FURI_HAL_USB_AUDIO_FEEDBACK_WINDOW_SHIFT);
    } else {
        feedback->step_count++;
    }
    feedback->played_frames[feedback->step_index] = played_frames;
    feedback->step_index = (feedback->step_index + 1) % FURI_HAL_USB_AUDIO_FEEDBACK_STEP_COUNT;

    const int32_t correction =
        furi_hal_usb_audio_get_fifo_level_correction(feedback, &usb_audio.speaker_reader_format);
    const int32_t corrected = (int32_t)rate + correction;
    *value = (uint32_t)CLAMP(
        corrected,
        (int32_t)nominal + FURI_HAL_USB_AUDIO_FEEDBACK_DEVIATION_MAX,
        (int32_t)nominal - FURI_HAL_USB_AUDIO_FEEDBACK_DEVIATION_MAX);
    return true;
}

void tud_audio_feedback_interval_isr(
    uint8_t func_id,
    uint32_t frame_number,
    uint8_t interval_shift) {
    UNUSED(interval_shift);

    FuriHalUsbAudioFeedback* feedback = &usb_audio.feedback;
    const uint32_t nominal = furi_hal_usb_audio_get_nominal_feedback(
        usb_audio.streams[FuriHalUsbAudioStreamSpeaker].clock.sample_rate);

    if(!usb_audio.speaker_readable ||
       feedback->reader_generation != usb_audio.speaker_reader_generation) {
        furi_hal_usb_audio_feedback_reset(feedback);
        feedback->frame_number_valid = false;
        tud_audio_n_fb_set(func_id, nominal);
        return;
    }
    furi_hal_usb_audio_feedback_count_sof(feedback, frame_number);
    const uint16_t sof_count = furi_hal_usb_audio_get_speaker_fifo_count();
    feedback->stats.fifo_sof_count_min = MIN(feedback->stats.fifo_sof_count_min, sof_count);
    feedback->stats.fifo_sof_count_max = MAX(feedback->stats.fifo_sof_count_max, sof_count);

    const uint32_t played_frames =
        usb_audio.speaker_played_callback(usb_audio.speaker_played_context);
    feedback->fifo_level_sum += tud_audio_n_available(func_id);
    feedback->step_frame_count++;
    if(feedback->step_frame_count < FURI_HAL_USB_AUDIO_FEEDBACK_STEP_FRAMES) {
        return;
    }

    uint32_t value;
    if(furi_hal_usb_audio_feedback_step(feedback, played_frames, nominal, &value)) {
        feedback->step_frame_count = 0;
        feedback->fifo_level_sum = 0;
        furi_hal_usb_audio_feedback_set(feedback, func_id, value);
    } else {
        furi_hal_usb_audio_feedback_reset(feedback);
        feedback->stats.feedback_resets++;
        furi_hal_usb_audio_feedback_set(feedback, func_id, nominal);
    }
}
