#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    FuriHalUsbPdIdPhone,
    FuriHalUsbPdIdCharger,

    FuriHalUsbPdIdMax,
} FuriHalUsbPdId;

typedef enum {
    FuriHalUsbPdCcPin1,
    FuriHalUsbPdCcPin2,
} FuriHalUsbPdCcPin;

typedef enum {
    FuriHalUsbPdRpDefault,
    FuriHalUsbPdRp1A5,
    FuriHalUsbPdRp3A0,
} FuriHalUsbPdRp;

typedef enum {
    FuriHalUsbPdCcStateOpen,
    FuriHalUsbPdCcStateRa,
    FuriHalUsbPdCcStateRd,
    FuriHalUsbPdCcStateRpDefault,
    FuriHalUsbPdCcStateRp1A5,
    FuriHalUsbPdCcStateRp3A0,
} FuriHalUsbPdCcState;

typedef enum {
    FuriHalUsbPdEventCcChanged,
    FuriHalUsbPdEventMessageReceived,
    FuriHalUsbPdEventHardResetReceived,
    FuriHalUsbPdEventHardResetSent,
    FuriHalUsbPdEventTransmitSent,
    FuriHalUsbPdEventTransmitFailed,
    FuriHalUsbPdEventFastRoleSwapReceived,
} FuriHalUsbPdEvent;

typedef void (*FuriHalUsbPdCallback)(FuriHalUsbPdEvent event, void* context);

void furi_hal_usbpd_init(void);

void furi_hal_usbpd_start(FuriHalUsbPdId id, FuriHalUsbPdCallback callback, void* context);

void furi_hal_usbpd_stop(FuriHalUsbPdId id);

void furi_hal_usbpd_set_rp(FuriHalUsbPdId id, FuriHalUsbPdRp rp);

void furi_hal_usbpd_set_rd(FuriHalUsbPdId id);

void furi_hal_usbpd_set_cc_open(FuriHalUsbPdId id);

void furi_hal_usbpd_get_cc(FuriHalUsbPdId id, FuriHalUsbPdCcState* cc1, FuriHalUsbPdCcState* cc2);

void furi_hal_usbpd_set_receive_buffer(FuriHalUsbPdId id, uint8_t* buffer, size_t size);

void furi_hal_usbpd_start_communication(FuriHalUsbPdId id, FuriHalUsbPdCcPin pin);

void furi_hal_usbpd_stop_communication(FuriHalUsbPdId id);

void furi_hal_usbpd_enable_receive(FuriHalUsbPdId id);

void furi_hal_usbpd_disable_receive(FuriHalUsbPdId id);

bool furi_hal_usbpd_send_message(FuriHalUsbPdId id, const uint8_t* data, size_t size);

void furi_hal_usbpd_send_hard_reset(FuriHalUsbPdId id);

void furi_hal_usbpd_start_bist_carrier(FuriHalUsbPdId id);

void furi_hal_usbpd_stop_bist_carrier(FuriHalUsbPdId id);

void furi_hal_usbpd_set_sink_tx_ok(FuriHalUsbPdId id, bool ok);

bool furi_hal_usbpd_is_sink_tx_ok(FuriHalUsbPdId id);

void furi_hal_usbpd_send_fast_role_swap(FuriHalUsbPdId id);

void furi_hal_usbpd_set_fast_role_swap_detection(FuriHalUsbPdId id, bool enabled);

#ifdef __cplusplus
}
#endif
