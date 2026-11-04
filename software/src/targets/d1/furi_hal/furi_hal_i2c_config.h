#pragma once

#include <furi_hal_i2c_types.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Internal i2c bus, I2C1, under reset when not used */
extern FuriHalI2cBus furi_hal_i2c_bus_internal;

/** Handle for internal i2c bus
 * Bus: furi_hal_i2c_bus_internal
 * Pins: PB6(SCL) / PB7(SDA), float on release
 * Params: 400khz
 */
extern const FuriHalI2cBusHandle furi_hal_i2c_handle_internal;

#ifdef __cplusplus
}
#endif
