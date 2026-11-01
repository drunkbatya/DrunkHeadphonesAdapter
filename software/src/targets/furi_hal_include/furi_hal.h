#pragma once

#include <furi_hal_adc.h>
#include <furi_hal_bus.h>
#include <furi_hal_clock.h>
#include <furi_hal_cortex.h>
#include <furi_hal_gpio.h>
#include <furi_hal_i2c.h>
#include <furi_hal_i2s.h>
#include <furi_hal_interrupt.h>
#include <furi_hal_power.h>
#include <furi_hal_resources.h>
#include <furi_hal_usb.h>
#include <furi_hal_usbpd.h>

#ifdef __cplusplus
extern "C" {
#endif

void furi_hal_init_early(void);

void furi_hal_init(void);

#ifdef __cplusplus
}
#endif
