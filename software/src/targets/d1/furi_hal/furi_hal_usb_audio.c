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
    bool configured;
    FuriHalUsbAudioClock speaker_clock;
    FuriHalUsbAudioClock mic_clock;
    FuriHalUsbAudioFeatureUnit speaker_feature_unit;
    FuriHalUsbAudioFeatureUnit mic_feature_unit;
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

    furi_hal_usb_audio_apply_config(
        speaker, &usb_audio.speaker_clock, &usb_audio.speaker_feature_unit);
    furi_hal_usb_audio_apply_config(mic, &usb_audio.mic_clock, &usb_audio.mic_feature_unit);
    usb_audio.configured = true;
}

bool furi_hal_usb_audio_is_configured(void) {
    return usb_audio.configured;
}

static FuriHalUsbAudioClock* furi_hal_usb_audio_get_clock(uint8_t entity_id) {
    switch(entity_id) {
    case FuriHalUsbAudioEntitySpeakerClock:
        return &usb_audio.speaker_clock;
    case FuriHalUsbAudioEntityMicClock:
        return &usb_audio.mic_clock;
    default:
        return NULL;
    }
}

static FuriHalUsbAudioFeatureUnit* furi_hal_usb_audio_get_feature_unit(uint8_t entity_id) {
    switch(entity_id) {
    case FuriHalUsbAudioEntitySpeakerFeatureUnit:
        return &usb_audio.speaker_feature_unit;
    case FuriHalUsbAudioEntityMicFeatureUnit:
        return &usb_audio.mic_feature_unit;
    default:
        return NULL;
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
    FuriHalUsbAudioClock* clock) {
    const uint8_t control_selector = TU_U16_HIGH(request->wValue);

    if(control_selector != AUDIO20_CS_CTRL_SAM_FREQ || request->bRequest != AUDIO20_CS_REQ_CUR ||
       request->wLength != sizeof(audio20_control_cur_4_t)) {
        return false;
    }

    const uint32_t sample_rate = tu_unaligned_read32(buffer);
    for(size_t i = 0; i < clock->sample_rate_count; i++) {
        if(clock->sample_rates[i] == sample_rate) {
            clock->sample_rate = sample_rate;
            return true;
        }
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
    FuriHalUsbAudioFeatureUnit* feature_unit) {
    const uint8_t control_selector = TU_U16_HIGH(request->wValue);
    const uint8_t channel = TU_U16_LOW(request->wValue);

    if(channel != FURI_HAL_USB_AUDIO_MASTER_CHANNEL || request->bRequest != AUDIO20_CS_REQ_CUR) {
        return false;
    }

    if(control_selector == AUDIO20_FU_CTRL_MUTE &&
       request->wLength == sizeof(audio20_control_cur_1_t)) {
        feature_unit->muted = buffer[0] != 0;
        return true;
    }

    if(control_selector == AUDIO20_FU_CTRL_VOLUME &&
       request->wLength == sizeof(audio20_control_cur_2_t)) {
        const int16_t volume = (int16_t)tu_unaligned_read16(buffer);
        feature_unit->volume =
            CLAMP(volume, feature_unit->volume_range.max, feature_unit->volume_range.min);
        return true;
    }

    return false;
}

bool tud_audio_get_req_entity_cb(uint8_t rhport, tusb_control_request_t const* p_request) {
    const uint8_t entity_id = TU_U16_HIGH(p_request->wIndex);

    const FuriHalUsbAudioClock* clock = furi_hal_usb_audio_get_clock(entity_id);
    if(clock != NULL) {
        return furi_hal_usb_audio_get_clock_request(rhport, p_request, clock);
    }

    const FuriHalUsbAudioFeatureUnit* feature_unit =
        furi_hal_usb_audio_get_feature_unit(entity_id);
    if(feature_unit != NULL) {
        return furi_hal_usb_audio_get_feature_unit_request(rhport, p_request, feature_unit);
    }

    return false;
}

bool tud_audio_set_req_entity_cb(
    uint8_t rhport,
    tusb_control_request_t const* p_request,
    uint8_t* pBuff) {
    UNUSED(rhport);
    const uint8_t entity_id = TU_U16_HIGH(p_request->wIndex);

    FuriHalUsbAudioClock* clock = furi_hal_usb_audio_get_clock(entity_id);
    if(clock != NULL) {
        return furi_hal_usb_audio_set_clock_request(p_request, pBuff, clock);
    }

    FuriHalUsbAudioFeatureUnit* feature_unit = furi_hal_usb_audio_get_feature_unit(entity_id);
    if(feature_unit != NULL) {
        return furi_hal_usb_audio_set_feature_unit_request(p_request, pBuff, feature_unit);
    }

    return false;
}
