#pragma once

#include <stdint.h>

#include <usb_pd_platform.h>

#include <applications/services/usbc/usbc_port.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct UsbcVbus UsbcVbus;

typedef void (*UsbcVbusWakeupCallback)(void* context);

UsbcVbus* usbc_vbus_alloc(UsbcVbusWakeupCallback wakeup_callback, void* context);

void usbc_vbus_free(UsbcVbus* vbus);

UsbPdVbusLevel usbc_vbus_get_level(UsbcVbus* vbus, UsbcPort port);

void usbc_vbus_set_contract_voltage(UsbcVbus* vbus, UsbcPort port, uint32_t voltage_mv);

uint32_t usbc_vbus_process(UsbcVbus* vbus);

#ifdef __cplusplus
}
#endif
