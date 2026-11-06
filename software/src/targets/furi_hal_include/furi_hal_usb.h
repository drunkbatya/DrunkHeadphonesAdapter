#pragma once

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    FuriHalUsbStateEventMount,
    FuriHalUsbStateEventUnmount,
    FuriHalUsbStateEventSuspend,
    FuriHalUsbStateEventResume,
} FuriHalUsbStateEvent;

typedef void (*FuriHalUsbStateCallback)(FuriHalUsbStateEvent state, void* context);

void furi_hal_usb_init(void);

void furi_hal_usb_enable(void);

void furi_hal_usb_disable(void);

void furi_hal_usb_set_state_callback(FuriHalUsbStateCallback callback, void* context);

#ifdef __cplusplus
}
#endif
