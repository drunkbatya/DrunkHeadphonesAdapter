#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    FuriHalUsbStringLanguage,
    FuriHalUsbStringManufacturer,
    FuriHalUsbStringProduct,
    FuriHalUsbStringSerialNumber,
} FuriHalUsbString;

typedef enum {
    FuriHalUsbInterfaceAudioControl,
    FuriHalUsbInterfaceAudioSpeaker,
    FuriHalUsbInterfaceAudioMic,
    FuriHalUsbInterfaceHid,
    FuriHalUsbInterfaceCount,
} FuriHalUsbInterface;

typedef enum {
    FuriHalUsbEndpointAudioSpeakerOut = 0x01,
    FuriHalUsbEndpointAudioFeedbackIn = 0x81,
    FuriHalUsbEndpointAudioMicIn = 0x82,
    FuriHalUsbEndpointHidIn = 0x83,
} FuriHalUsbEndpoint;

typedef enum {
    FuriHalUsbAudioEntitySpeakerClock = 0x10,
    FuriHalUsbAudioEntitySpeakerInputTerminal = 0x11,
    FuriHalUsbAudioEntitySpeakerFeatureUnit = 0x12,
    FuriHalUsbAudioEntitySpeakerOutputTerminal = 0x13,
    FuriHalUsbAudioEntityMicClock = 0x20,
    FuriHalUsbAudioEntityMicInputTerminal = 0x21,
    FuriHalUsbAudioEntityMicFeatureUnit = 0x22,
    FuriHalUsbAudioEntityMicOutputTerminal = 0x23,
} FuriHalUsbAudioEntity;

typedef enum {
    FuriHalUsbAudioAltSettingIdle,
    FuriHalUsbAudioAltSetting16Bit,
    FuriHalUsbAudioAltSetting24Bit,
} FuriHalUsbAudioAltSetting;

bool furi_hal_usb_audio_is_configured(void);

void furi_hal_usb_hid_init(void);

void furi_hal_usb_hid_reset(void);

void furi_hal_usb_hid_send_pending(void);

void furi_hal_usb_hid_request_send(void);
