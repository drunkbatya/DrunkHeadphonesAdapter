#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FURI_HAL_USB_AUDIO_VOLUME_PER_DB         256
#define FURI_HAL_USB_AUDIO_SAMPLE_RATE_MAX_COUNT 4

typedef struct {
    int16_t min;
    int16_t max;
    int16_t resolution;
} FuriHalUsbAudioVolumeRange;

typedef struct {
    const uint32_t* sample_rates;
    size_t sample_rate_count;
    FuriHalUsbAudioVolumeRange volume_range;
} FuriHalUsbAudioStreamConfig;

void furi_hal_usb_audio_set_config(
    const FuriHalUsbAudioStreamConfig* speaker,
    const FuriHalUsbAudioStreamConfig* mic);

#ifdef __cplusplus
}
#endif
