#pragma once

#include <stdbool.h>
#include <stdint.h>

#include <usb_pd_platform.h>

#include <applications/services/usbc/usbc_port.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct UsbcVbus UsbcVbus;

typedef void (*UsbcVbusWakeupCallback)(void* context);

typedef void (*UsbcVbusDropCallback)(UsbcPort port, void* context);

UsbcVbus* usbc_vbus_alloc(UsbcVbusWakeupCallback wakeup_callback, void* context);

void usbc_vbus_free(UsbcVbus* vbus);

void usbc_vbus_set_drop_callback(UsbcVbus* vbus, UsbcVbusDropCallback drop_callback, void* context);

UsbPdVbusLevel usbc_vbus_get_level(UsbcVbus* vbus, UsbcPort port);

void usbc_vbus_set_contract_voltage(UsbcVbus* vbus, UsbcPort port, uint32_t voltage_mv);

bool usbc_vbus_take_level_change(UsbcVbus* vbus);

void usbc_vbus_set_ceiling_voltage(UsbcVbus* vbus, uint32_t voltage_mv);

void usbc_vbus_clear_ceiling(UsbcVbus* vbus);

bool usbc_vbus_is_within_ceiling(UsbcVbus* vbus);

bool usbc_vbus_take_ceiling_violation(UsbcVbus* vbus);

uint32_t usbc_vbus_process(UsbcVbus* vbus);

#ifdef __cplusplus
}
#endif
