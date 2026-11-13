#pragma once

#include <stdint.h>

#include <furi_hal_i2s.h>
#include <furi_hal_usb_audio.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct AudioCapture AudioCapture;

typedef void (*AudioCaptureErrorCallback)(FuriHalI2sEvent event, void* context);

AudioCapture* audio_capture_alloc(AudioCaptureErrorCallback error_callback, void* context);

void audio_capture_free(AudioCapture* capture);

void audio_capture_start(
    AudioCapture* capture,
    uint32_t sample_rate,
    FuriHalUsbAudioSampleFormat sample_format);

void audio_capture_stop(AudioCapture* capture);

void audio_capture_restart(AudioCapture* capture);

bool audio_capture_is_running(const AudioCapture* capture);

#ifdef __cplusplus
}
#endif
