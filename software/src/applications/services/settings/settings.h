#pragma once

#include <stdint.h>

#include <adapter.pb.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SETTINGS_SPEAKER_VOLUME_OFFSET_MAX_STEPS 20
#define SETTINGS_MIC_GAIN_MAX_STEPS              10U

typedef struct {
    int8_t speaker_volume_offset;
    uint8_t mic_gain;
} Settings;

void settings_reset(Settings* settings);

void settings_to_proto(const Settings* settings, adapter_Settings* message);

void settings_apply_proto(Settings* settings, const adapter_Settings* message);

#ifdef __cplusplus
}
#endif
