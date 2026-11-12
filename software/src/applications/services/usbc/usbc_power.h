#pragma once

#include <usb_pd.h>

#include <applications/services/usbc/usbc_port.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct UsbcPower UsbcPower;

UsbcPower* usbc_power_alloc(void);

void usbc_power_free(UsbcPower* power);

void usbc_power_set(UsbcPower* power, UsbcPort port, UsbPdPower state);

#ifdef __cplusplus
}
#endif
