#include <drivers/tcpp03.h>
#include <drivers/tcpp03_reg.h>

#include <furi.h>

static const uint8_t tcpp03_power_mode_bits[] = {
    [Tcpp03PowerModeHibernate] = TCPP03_CONTROL_POWER_MODE_HIBERNATE,
    [Tcpp03PowerModeLowPower] = TCPP03_CONTROL_POWER_MODE_LOW_POWER,
    [Tcpp03PowerModeNormal] = TCPP03_CONTROL_POWER_MODE_NORMAL,
};

static bool
    tcpp03_read(const FuriHalI2cBusHandle* handle, uint8_t address, uint8_t reg, uint8_t* value) {
    return furi_hal_i2c_read_reg_8(handle, address, reg, value, TCPP03_I2C_TIMEOUT);
}

static bool tcpp03_decode_power_mode(uint8_t value, Tcpp03PowerMode* power_mode) {
    switch(value & TCPP03_CONTROL_POWER_MODE_MASK) {
    case TCPP03_CONTROL_POWER_MODE_HIBERNATE:
        *power_mode = Tcpp03PowerModeHibernate;
        return true;
    case TCPP03_CONTROL_POWER_MODE_LOW_POWER:
        *power_mode = Tcpp03PowerModeLowPower;
        return true;
    case TCPP03_CONTROL_POWER_MODE_NORMAL:
        *power_mode = Tcpp03PowerModeNormal;
        return true;
    default:
        return false;
    }
}

bool tcpp03_init(const FuriHalI2cBusHandle* handle, uint8_t address) {
    furi_check(handle);

    uint8_t flags;
    if(!tcpp03_read(handle, address, TCPP03_REG_FLAGS, &flags)) {
        return false;
    }
    return !(flags & TCPP03_FLAGS_DEVICE_TCPP02);
}

bool tcpp03_write_control(
    const FuriHalI2cBusHandle* handle,
    uint8_t address,
    const Tcpp03Control* control) {
    furi_check(handle);
    furi_check(control);
    furi_check(control->power_mode < COUNT_OF(tcpp03_power_mode_bits));

    uint8_t value = TCPP03_CONTROL_GDC_OPEN | tcpp03_power_mode_bits[control->power_mode];
    if(control->provider_switch_closed) {
        value |= TCPP03_CONTROL_GDP_CLOSED;
    }
    if(control->vbus_discharge_enabled) {
        value |= TCPP03_CONTROL_VBUS_DISCHARGE;
    }
    return furi_hal_i2c_write_reg_8(
        handle, address, TCPP03_REG_CONTROL, value, TCPP03_I2C_TIMEOUT);
}

bool tcpp03_read_ack(const FuriHalI2cBusHandle* handle, uint8_t address, Tcpp03Control* ack) {
    furi_check(handle);
    furi_check(ack);

    uint8_t value;
    if(!tcpp03_read(handle, address, TCPP03_REG_ACK, &value)) {
        return false;
    }
    if(!tcpp03_decode_power_mode(value, &ack->power_mode)) {
        return false;
    }
    ack->provider_switch_closed = value & TCPP03_CONTROL_GDP_CLOSED;
    ack->vbus_discharge_enabled = value & TCPP03_CONTROL_VBUS_DISCHARGE;
    return true;
}

bool tcpp03_read_flags(const FuriHalI2cBusHandle* handle, uint8_t address, uint8_t* flags) {
    furi_check(handle);
    furi_check(flags);

    if(!tcpp03_read(handle, address, TCPP03_REG_FLAGS, flags)) {
        return false;
    }
    *flags &= TCPP03_FLAGS_MASK;
    return true;
}
