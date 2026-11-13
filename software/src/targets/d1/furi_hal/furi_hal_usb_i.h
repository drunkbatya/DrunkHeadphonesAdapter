#pragma once

#include <stdbool.h>
#include <stdint.h>

#define FURI_HAL_USB_ROOT_PORT 0

#define FURI_HAL_USB_AUDIO_SPEAKER_CHANNEL_CONFIG \
    (AUDIO20_CHANNEL_CONFIG_FRONT_LEFT | AUDIO20_CHANNEL_CONFIG_FRONT_RIGHT)

#define FURI_HAL_USB_AUDIO_MIC_CHANNEL_CONFIG AUDIO20_CHANNEL_CONFIG_FRONT_CENTER

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
    FuriHalUsbInterfaceCdcControl,
    FuriHalUsbInterfaceCdcData,
    FuriHalUsbInterfaceCount,
} FuriHalUsbInterface;

typedef enum {
    FuriHalUsbEndpointAudioSpeakerOut = 0x01,
    FuriHalUsbEndpointAudioFeedbackIn = 0x81,
    FuriHalUsbEndpointAudioMicIn = 0x82,
    FuriHalUsbEndpointHidIn = 0x83,
    FuriHalUsbEndpointCdcDataOut = 0x04,
    FuriHalUsbEndpointCdcDataIn = 0x84,
    FuriHalUsbEndpointCdcNotificationIn = 0x85,
    FuriHalUsbEndpointAudioInterruptIn = 0x86,
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

void furi_hal_usb_audio_reset(void);

void furi_hal_usb_audio_begin_bus_reset(void);

void furi_hal_usb_audio_finish_bus_reset(void);

void furi_hal_usb_audio_send_notifications(void);

void furi_hal_usb_audio_request_notification(void);

void furi_hal_usb_remote_wakeup(void);

void furi_hal_usb_hid_init(void);

void furi_hal_usb_hid_reset(void);

void furi_hal_usb_hid_send_pending(void);

void furi_hal_usb_hid_request_send(void);

void furi_hal_usb_cdc_mount(void);

void furi_hal_usb_cdc_reset(void);

void furi_hal_usb_cdc_process_rx(void);

void furi_hal_usb_cdc_process_tx(void);

void furi_hal_usb_cdc_request_rx(void);

void furi_hal_usb_cdc_request_tx(void);
