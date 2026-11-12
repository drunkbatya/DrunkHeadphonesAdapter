#ifndef TYPE_C_CONNECTION_H_
#define TYPE_C_CONNECTION_H_

#include <stdbool.h>
#include <stdint.h>

#include "usb_pd_platform.h"

typedef enum {
    TypeCConnectionEventTypeDetached,
    TypeCConnectionEventTypeAttached,
} TypeCConnectionEventType;

typedef struct {
    TypeCConnectionEventType type;
    UsbPdPowerRole power_role;
    UsbPdCcPin pin;
    UsbPdRpCurrent partner_rp_current;
    bool vconn_required;
} TypeCConnectionEvent;

typedef void (
    *TypeCConnectionCallback)(void* context, uint8_t port, const TypeCConnectionEvent* event);

void type_c_connection_init(
    uint8_t port,
    UsbPdPortMode mode,
    UsbPdRpCurrent rp_current,
    TypeCConnectionCallback callback,
    void* context);

void type_c_connection_set_mode(uint8_t port, UsbPdPortMode mode);
void type_c_connection_set_rp_current(uint8_t port, UsbPdRpCurrent current);
void type_c_connection_enter_error_recovery(uint8_t port);

void type_c_connection_set_swap_in_progress(uint8_t port, bool in_progress);
void type_c_connection_set_vbus_disconnect_detection(uint8_t port, bool enabled);
void type_c_connection_swap_power_role(uint8_t port, UsbPdPowerRole power_role);

uint32_t type_c_connection_process(uint8_t port);

#endif
