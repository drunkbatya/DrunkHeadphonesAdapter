#include <applications/services/usbc/usbc_power.h>

#include <furi.h>
#include <furi_hal.h>

#include <drivers/tcpp03.h>

#define TAG "UsbcPower"

#define USBC_POWER_I2C_ATTEMPTS       3
#define USBC_POWER_I2C_RETRY_DELAY_MS 1

typedef struct {
    bool faulted;
    bool started;
    bool attached;
    bool discharging;
    UsbPdPower state;
    Tcpp03Control written;
} UsbcPowerPort;

struct UsbcPower {
    UsbcVbus* vbus;
    UsbcPowerPort ports[UsbcPortCount];
};

static const uint8_t usbc_power_addresses[UsbcPortCount] = {
    [UsbcPortPhone] = TCPP03_ADDRESS_I2C_ADD_LOW,
    [UsbcPortCharger] = TCPP03_ADDRESS_I2C_ADD_HIGH,
};

static const char* const usbc_power_mode_names[] = {
    [Tcpp03PowerModeHibernate] = "Hibernate",
    [Tcpp03PowerModeLowPower] = "LowPower",
    [Tcpp03PowerModeNormal] = "Normal",
};

static bool usbc_power_is_sourcing(const UsbcPowerPort* port) {
    return (port->state == UsbPdPowerSourceOn) || (port->state == UsbPdPowerSourceNewContract);
}

static bool usbc_power_is_provider_closed(const UsbcPower* power) {
    bool closed = false;
    for(UsbcPort i = 0; i < UsbcPortCount; i++) {
        closed |= usbc_power_is_sourcing(&power->ports[i]);
    }
    return closed;
}

static Tcpp03Control usbc_power_get_control(const UsbcPower* power, UsbcPort i) {
    const UsbcPowerPort* port = &power->ports[i];
    const bool provider_closed = (i == UsbcPortPhone) && usbc_power_is_provider_closed(power);
    Tcpp03Control control = {
        .power_mode = Tcpp03PowerModeHibernate,
        .provider_switch_closed = provider_closed,
        .vbus_discharge_enabled = port->discharging,
    };

    if(port->started) {
        control.power_mode = (port->attached || port->discharging || provider_closed) ?
                                 Tcpp03PowerModeNormal :
                                 Tcpp03PowerModeLowPower;
    }
    return control;
}

static bool usbc_power_is_control_equal(const Tcpp03Control* a, const Tcpp03Control* b) {
    return (a->power_mode == b->power_mode) &&
           (a->provider_switch_closed == b->provider_switch_closed) &&
           (a->vbus_discharge_enabled == b->vbus_discharge_enabled);
}

static bool usbc_power_init_chip(UsbcPort i) {
    for(size_t attempt = 0; attempt < USBC_POWER_I2C_ATTEMPTS; attempt++) {
        if(attempt > 0) {
            furi_delay_ms(USBC_POWER_I2C_RETRY_DELAY_MS);
        }
        furi_hal_i2c_acquire(&furi_hal_i2c_handle_internal);
        const bool success = tcpp03_init(&furi_hal_i2c_handle_internal, usbc_power_addresses[i]);
        furi_hal_i2c_release(&furi_hal_i2c_handle_internal);
        if(success) {
            return true;
        }
    }
    return false;
}

static bool usbc_power_write_chip(UsbcPort i, const Tcpp03Control* control) {
    for(size_t attempt = 0; attempt < USBC_POWER_I2C_ATTEMPTS; attempt++) {
        if(attempt > 0) {
            furi_delay_ms(USBC_POWER_I2C_RETRY_DELAY_MS);
        }
        furi_hal_i2c_acquire(&furi_hal_i2c_handle_internal);
        const bool success =
            tcpp03_write_control(&furi_hal_i2c_handle_internal, usbc_power_addresses[i], control);
        furi_hal_i2c_release(&furi_hal_i2c_handle_internal);
        if(success) {
            return true;
        }
    }
    return false;
}

static void usbc_power_log_control(UsbcPort i, const Tcpp03Control* control) {
    FURI_LOG_I(
        TAG,
        "TCPP03 0x%02X %s, provider switch %s, discharge %s",
        usbc_power_addresses[i] >> 1,
        usbc_power_mode_names[control->power_mode],
        control->provider_switch_closed ? "closed" : "open",
        control->vbus_discharge_enabled ? "on" : "off");
}

static void usbc_power_set_faulted(UsbcPower* power, UsbcPort i) {
    power->ports[i].faulted = true;
    if(i == UsbcPortPhone) {
        furi_hal_gpio_write(&gpio_tcpp03_phone_en, false);
    }
}

static void usbc_power_apply(UsbcPower* power) {
    for(UsbcPort i = 0; i < UsbcPortCount; i++) {
        UsbcPowerPort* port = &power->ports[i];
        if(port->faulted) {
            continue;
        }
        const Tcpp03Control control = usbc_power_get_control(power, i);
        if(usbc_power_is_control_equal(&control, &port->written)) {
            continue;
        }
        if(!usbc_power_write_chip(i, &control)) {
            FURI_LOG_E(TAG, "TCPP03 0x%02X write failed", usbc_power_addresses[i] >> 1);
            usbc_power_set_faulted(power, i);
            continue;
        }
        port->written = control;
        usbc_power_log_control(i, &control);
    }
}

UsbcPower* usbc_power_alloc(UsbcVbus* vbus) {
    furi_check(vbus);

    UsbcPower* power = malloc(sizeof(UsbcPower));
    power->vbus = vbus;

    furi_hal_gpio_write(&gpio_tcpp03_phone_en, true);

    for(UsbcPort i = 0; i < UsbcPortCount; i++) {
        power->ports[i].state = UsbPdPowerSinkOff;
    }

    for(UsbcPort i = 0; i < UsbcPortCount; i++) {
        UsbcPowerPort* port = &power->ports[i];
        port->written = usbc_power_get_control(power, i);
        if(!usbc_power_init_chip(i)) {
            FURI_LOG_E(TAG, "TCPP03 0x%02X init failed", usbc_power_addresses[i] >> 1);
            usbc_power_set_faulted(power, i);
            continue;
        }
        if(!usbc_power_write_chip(i, &port->written)) {
            FURI_LOG_E(TAG, "TCPP03 0x%02X write failed", usbc_power_addresses[i] >> 1);
            usbc_power_set_faulted(power, i);
            continue;
        }
        usbc_power_log_control(i, &port->written);
    }

    return power;
}

void usbc_power_free(UsbcPower* power) {
    furi_hal_gpio_write(&gpio_tcpp03_phone_en, false);
    free(power);
}

void usbc_power_start(UsbcPower* power) {
    for(UsbcPort i = 0; i < UsbcPortCount; i++) {
        power->ports[i].started = true;
    }
    usbc_power_apply(power);
}

void usbc_power_set_attached(UsbcPower* power, UsbcPort port, bool attached) {
    furi_check(port < UsbcPortCount);

    power->ports[port].attached = attached;
    usbc_power_apply(power);
}

void usbc_power_set(UsbcPower* power, UsbcPort port, UsbPdPower state) {
    furi_check(port < UsbcPortCount);
    UsbcPowerPort* power_port = &power->ports[port];

    power_port->state = state;
    power_port->discharging = (state == UsbPdPowerSourceOff);
    usbc_power_apply(power);
}

void usbc_power_process(UsbcPower* power) {
    for(UsbcPort i = 0; i < UsbcPortCount; i++) {
        UsbcPowerPort* port = &power->ports[i];
        if(port->discharging && (usbc_vbus_get_level(power->vbus, i) == UsbPdVbusLevelSafe0V)) {
            port->discharging = false;
        }
    }
    usbc_power_apply(power);
}

bool usbc_power_is_faulted(UsbcPower* power, UsbcPort port) {
    furi_check(port < UsbcPortCount);
    return power->ports[port].faulted;
}
