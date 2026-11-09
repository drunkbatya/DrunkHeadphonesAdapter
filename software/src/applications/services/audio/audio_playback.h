#pragma once

#include <stdint.h>

#include <furi_hal_usb_audio.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct AudioPlayback AudioPlayback;

typedef void (*AudioPlaybackErrorCallback)(void* context);

AudioPlayback* audio_playback_alloc(AudioPlaybackErrorCallback error_callback, void* context);

void audio_playback_free(AudioPlayback* playback);

void audio_playback_start(
    AudioPlayback* playback,
    uint32_t sample_rate,
    FuriHalUsbAudioSampleFormat sample_format);

void audio_playback_stop(AudioPlayback* playback);

void audio_playback_restart(AudioPlayback* playback);

bool audio_playback_is_running(const AudioPlayback* playback);

#ifdef __cplusplus
}
#endif
