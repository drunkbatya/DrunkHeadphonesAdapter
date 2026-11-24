#pragma once

#include <stdbool.h>
#include <stdint.h>

#include <usb_pd.h>

#include <applications/services/usbc/usbc_port.h>
#include <applications/services/usbc/usbc_vbus.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct UsbcPower UsbcPower;

typedef void (*UsbcPowerWakeupCallback)(void* context);

UsbcPower*
    usbc_power_alloc(UsbcVbus* vbus, UsbcPowerWakeupCallback wakeup_callback, void* context);

void usbc_power_free(UsbcPower* power);

void usbc_power_start(UsbcPower* power);

void usbc_power_set_attached(UsbcPower* power, UsbcPort port, bool attached);

void usbc_power_set(UsbcPower* power, UsbcPort port, UsbPdPower state);

void usbc_power_process(UsbcPower* power);

uint32_t usbc_power_get_next_ms(UsbcPower* power);

void usbc_power_trip_provider(UsbcPower* power);

bool usbc_power_take_trip(UsbcPower* power, UsbcPort port);

bool usbc_power_is_faulted(UsbcPower* power, UsbcPort port);

bool usbc_power_take_fault_change(UsbcPower* power);

#ifdef __cplusplus
}
#endif
