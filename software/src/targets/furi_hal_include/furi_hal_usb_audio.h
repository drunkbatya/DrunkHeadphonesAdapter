#pragma once

#include <stdbool.h>
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

typedef enum {
    FuriHalUsbAudioStreamSpeaker,
    FuriHalUsbAudioStreamMic,

    FuriHalUsbAudioStreamCount,
} FuriHalUsbAudioStream;

typedef enum {
    FuriHalUsbAudioSampleFormat16Bit,
    FuriHalUsbAudioSampleFormat24Bit,
} FuriHalUsbAudioSampleFormat;

typedef struct {
    uint32_t sample_rate;
    FuriHalUsbAudioSampleFormat sample_format;
} FuriHalUsbAudioFormat;

typedef enum {
    FuriHalUsbAudioEventTypeStart,
    FuriHalUsbAudioEventTypeStop,
    FuriHalUsbAudioEventTypeVolume,
    FuriHalUsbAudioEventTypeMute,
} FuriHalUsbAudioEventType;

typedef struct {
    FuriHalUsbAudioEventType type;
    FuriHalUsbAudioStream stream;
    union {
        FuriHalUsbAudioFormat format;
        int16_t volume;
        bool muted;
    };
} FuriHalUsbAudioEvent;

typedef void (*FuriHalUsbAudioCallback)(const FuriHalUsbAudioEvent* event, void* context);

typedef uint32_t (*FuriHalUsbAudioSpeakerPlayedCallback)(void* context);

void furi_hal_usb_audio_set_config(
    const FuriHalUsbAudioStreamConfig* speaker,
    const FuriHalUsbAudioStreamConfig* mic);

void furi_hal_usb_audio_set_callback(FuriHalUsbAudioCallback callback, void* context);

int16_t furi_hal_usb_audio_get_volume(FuriHalUsbAudioStream stream);

bool furi_hal_usb_audio_is_muted(FuriHalUsbAudioStream stream);

void furi_hal_usb_audio_speaker_attach(
    const FuriHalUsbAudioFormat* format,
    FuriHalUsbAudioSpeakerPlayedCallback played_callback,
    void* context);

void furi_hal_usb_audio_speaker_detach(void);

size_t furi_hal_usb_audio_speaker_get_available(void);

size_t furi_hal_usb_audio_speaker_read(void* data, size_t size);

#ifdef __cplusplus
}
#endif
