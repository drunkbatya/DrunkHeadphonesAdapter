#pragma once

#include <stdbool.h>
#include <stdint.h>

#include <furi_hal_i2c.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TCPP03_ADDRESS_I2C_ADD_LOW  (0x34 << 1)
#define TCPP03_ADDRESS_I2C_ADD_HIGH (0x35 << 1)

#define TCPP03_FLAG_OCP_VCONN (1 << 0)
#define TCPP03_FLAG_OCP_VBUS  (1 << 1)
#define TCPP03_FLAG_OVP_VBUS  (1 << 2)
#define TCPP03_FLAG_OTP       (1 << 3)
#define TCPP03_FLAG_OVP_CC    (1 << 4)
#define TCPP03_FLAG_VBUS_OK   (1 << 5)

typedef enum {
    Tcpp03PowerModeHibernate,
    Tcpp03PowerModeLowPower,
    Tcpp03PowerModeNormal,
} Tcpp03PowerMode;

typedef struct {
    Tcpp03PowerMode power_mode;
    bool provider_switch_closed;
    bool vbus_discharge_enabled;
} Tcpp03Control;

bool tcpp03_init(const FuriHalI2cBusHandle* handle, uint8_t address);

bool tcpp03_write_control(
    const FuriHalI2cBusHandle* handle,
    uint8_t address,
    const Tcpp03Control* control);

bool tcpp03_read_ack(const FuriHalI2cBusHandle* handle, uint8_t address, Tcpp03Control* ack);

bool tcpp03_read_flags(const FuriHalI2cBusHandle* handle, uint8_t address, uint8_t* flags);

#ifdef __cplusplus
}
#endif
