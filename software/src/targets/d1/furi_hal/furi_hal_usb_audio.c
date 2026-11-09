#include <furi_hal_usb_audio.h>
#include <furi_hal_usb_i.h>

#include <furi.h>

#include <tusb.h>

#define FURI_HAL_USB_AUDIO_SAMPLE_RATE_RESOLUTION_DISCRETE 0
#define FURI_HAL_USB_AUDIO_VOLUME_DEFAULT                  0
#define FURI_HAL_USB_AUDIO_MASTER_CHANNEL                  0
#define FURI_HAL_USB_AUDIO_VOLUME_RANGE_COUNT              1

typedef struct {
    const uint32_t* sample_rates;
    size_t sample_rate_count;
    uint32_t sample_rate;
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
    bool configured;
    FuriHalUsbAudioCallback callback;
    void* callback_context;
    FuriHalUsbAudioStreamState streams[FuriHalUsbAudioStreamCount];
} FuriHalUsbAudio;

static FuriHalUsbAudio usb_audio = {0};

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

size_t furi_hal_usb_audio_speaker_get_available(void) {
    return tud_audio_available();
}

size_t furi_hal_usb_audio_speaker_read(void* data, size_t size) {
    furi_check(data);
    furi_check(size <= UINT16_MAX);
    return tud_audio_read(data, (uint16_t)size);
}

static void furi_hal_usb_audio_notify(const FuriHalUsbAudioEvent* event) {
    if(usb_audio.callback != NULL) {
        usb_audio.callback(event, usb_audio.callback_context);
    }
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

static void furi_hal_usb_audio_notify_start(FuriHalUsbAudioStream stream) {
    const FuriHalUsbAudioStreamState* state = &usb_audio.streams[stream];
    const FuriHalUsbAudioEvent event = {
        .type = FuriHalUsbAudioEventTypeStart,
        .stream = stream,
        .format =
            {
                .sample_rate = state->clock.sample_rate,
                .sample_format = furi_hal_usb_audio_get_sample_format(state->alt_setting),
            },
    };
    furi_hal_usb_audio_notify(&event);
}

static void furi_hal_usb_audio_notify_stop(FuriHalUsbAudioStream stream) {
    const FuriHalUsbAudioEvent event = {
        .type = FuriHalUsbAudioEventTypeStop,
        .stream = stream,
    };
    furi_hal_usb_audio_notify(&event);
}

void furi_hal_usb_audio_reset(void) {
    for(size_t stream = 0; stream < FuriHalUsbAudioStreamCount; stream++) {
        if(usb_audio.streams[stream].alt_setting == FuriHalUsbAudioAltSettingIdle) {
            continue;
        }
        usb_audio.streams[stream].alt_setting = FuriHalUsbAudioAltSettingIdle;
        furi_hal_usb_audio_notify_stop(stream);
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
        audio20_control_cur_1_t clock_valid = {.bCur = true};
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
        state->clock.sample_rate = sample_rate;
        if(state->alt_setting != FuriHalUsbAudioAltSettingIdle) {
            furi_hal_usb_audio_notify_start(stream);
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
        const FuriHalUsbAudioEvent event = {
            .type = FuriHalUsbAudioEventTypeMute,
            .stream = stream,
            .muted = feature_unit->muted,
        };
        furi_hal_usb_audio_notify(&event);
        return true;
    }

    if(control_selector == AUDIO20_FU_CTRL_VOLUME &&
       request->wLength == sizeof(audio20_control_cur_2_t)) {
        const int16_t volume = (int16_t)tu_unaligned_read16(buffer);
        feature_unit->volume =
            CLAMP(volume, feature_unit->volume_range.max, feature_unit->volume_range.min);
        const FuriHalUsbAudioEvent event = {
            .type = FuriHalUsbAudioEventTypeVolume,
            .stream = stream,
            .volume = feature_unit->volume,
        };
        furi_hal_usb_audio_notify(&event);
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

    FuriHalUsbAudioStreamState* state = &usb_audio.streams[stream];
    if(state->alt_setting == alt_setting) {
        return true;
    }

    state->alt_setting = alt_setting;
    if(alt_setting == FuriHalUsbAudioAltSettingIdle) {
        furi_hal_usb_audio_notify_stop(stream);
    } else {
        furi_hal_usb_audio_notify_start(stream);
    }

    return true;
}
