#pragma once

#include <stdbool.h>
#include <stdint.h>

#include <furi_hal_usb_audio.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RECORD_AUDIO          "audio"
#define RECORD_HEADSET_EVENTS "headset_events"

typedef enum {
    HeadsetEventPlugged,
    HeadsetEventUnplugged,
} HeadsetEvent;

typedef struct {
    bool active;
    uint32_t sample_rate;
    uint8_t sample_bits;
    int16_t volume;
    bool muted;
    uint32_t i2s_error_count;
} AudioStreamStatus;

typedef struct {
    bool headphone_plugged;
    bool headphone_output_powered;
    AudioStreamStatus streams[FuriHalUsbAudioStreamCount];
} AudioStatus;

typedef struct Audio Audio;

void audio_shutdown(Audio* audio);

void audio_get_status(Audio* audio, AudioStatus* status);

int32_t audio_srv(void* context);

#ifdef __cplusplus
}
#endif
