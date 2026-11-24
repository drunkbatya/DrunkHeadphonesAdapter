#pragma once

#include <stdint.h>

#include <furi_hal_i2s.h>
#include <furi_hal_usb_audio.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct AudioPlayback AudioPlayback;

typedef struct {
    FuriHalI2sEvent event;
    uint32_t transferred_halves;
    size_t dma_position;
    size_t dma_length;
} AudioPlaybackError;

typedef struct {
    uint32_t underruns;
    size_t fifo_min;
    size_t fifo_max;
    size_t half_size;
} AudioPlaybackStats;

typedef void (*AudioPlaybackErrorCallback)(const AudioPlaybackError* error, void* context);

AudioPlayback* audio_playback_alloc(AudioPlaybackErrorCallback error_callback, void* context);

void audio_playback_free(AudioPlayback* playback);

void audio_playback_start(
    AudioPlayback* playback,
    uint32_t sample_rate,
    FuriHalUsbAudioSampleFormat sample_format);

void audio_playback_stop(AudioPlayback* playback);

void audio_playback_restart(AudioPlayback* playback);

bool audio_playback_is_running(const AudioPlayback* playback);

void audio_playback_take_stats(AudioPlayback* playback, AudioPlaybackStats* stats);

#ifdef __cplusplus
}
#endif
