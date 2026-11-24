#include <applications/services/usbc/usbc_power.h>

#include <furi.h>
#include <furi_hal.h>

#include <drivers/tcpp03.h>

#define TAG "UsbcPower"

#define USBC_POWER_I2C_ATTEMPTS       3
#define USBC_POWER_I2C_RETRY_DELAY_MS 1
#define USBC_POWER_WRITE_RETRY_MS     10
#define USBC_POWER_FAULT_TIMEOUT_MS   100
#define USBC_POWER_TRIP_LIMIT         3
#define USBC_POWER_TRIP_WINDOW_MS     10000

#define USBC_POWER_OCP_FLAGS   (TCPP03_FLAG_OCP_VBUS | TCPP03_FLAG_OCP_VCONN)
#define USBC_POWER_TRIP_FLAGS  (USBC_POWER_OCP_FLAGS | TCPP03_FLAG_OVP_VBUS)
#define USBC_POWER_FAULT_FLAGS (USBC_POWER_TRIP_FLAGS | TCPP03_FLAG_OTP | TCPP03_FLAG_OVP_CC)

typedef struct {
    bool faulted;
    bool started;
    bool attached;
    bool discharging;
    bool write_failing;
    uint32_t write_failing_since_tick;
    volatile bool flag_pending;
    bool rewrite_pending;
    bool ocp_recovery_pending;
    bool trip_event_pending;
    uint8_t trip_count;
    uint32_t trip_window_start_tick;
    UsbPdPower state;
    Tcpp03Control written;
} UsbcPowerPort;

struct UsbcPower {
    UsbcVbus* vbus;
    UsbcPowerWakeupCallback wakeup_callback;
    void* context;
    UsbcPowerPort ports[UsbcPortCount];
    bool provider_blocked;
    bool fault_changed;
};

static const uint8_t usbc_power_addresses[UsbcPortCount] = {
    [UsbcPortPhone] = TCPP03_ADDRESS_I2C_ADD_LOW,
    [UsbcPortCharger] = TCPP03_ADDRESS_I2C_ADD_HIGH,
};

static const GpioPin* const usbc_power_flag_pins[UsbcPortCount] = {
    [UsbcPortPhone] = &gpio_tcpp03_phone_flg_n,
    [UsbcPortCharger] = &gpio_tcpp03_charger_flg_n,
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
    const bool provider_closed = (i == UsbcPortPhone) && !power->provider_blocked &&
                                 usbc_power_is_provider_closed(power);
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

static bool usbc_power_read_flags(UsbcPort i, uint8_t* flags) {
    for(size_t attempt = 0; attempt < USBC_POWER_I2C_ATTEMPTS; attempt++) {
        if(attempt > 0) {
            furi_delay_ms(USBC_POWER_I2C_RETRY_DELAY_MS);
        }
        furi_hal_i2c_acquire(&furi_hal_i2c_handle_internal);
        const bool success =
            tcpp03_read_flags(&furi_hal_i2c_handle_internal, usbc_power_addresses[i], flags);
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
    power->fault_changed = true;
    if(i == UsbcPortPhone) {
        furi_hal_gpio_write(&gpio_tcpp03_phone_en, false);
    }
}

static void
    usbc_power_on_write_failed(UsbcPower* power, UsbcPort i, const Tcpp03Control* control) {
    UsbcPowerPort* port = &power->ports[i];
    const uint32_t now = furi_get_tick();

    if(port->written.provider_switch_closed && !control->provider_switch_closed) {
        FURI_LOG_E(
            TAG, "TCPP03 0x%02X provider switch open write failed", usbc_power_addresses[i] >> 1);
        port->write_failing = false;
        usbc_power_set_faulted(power, i);
        return;
    }
    if(!port->write_failing) {
        FURI_LOG_E(TAG, "TCPP03 0x%02X write failed", usbc_power_addresses[i] >> 1);
        port->write_failing = true;
        port->write_failing_since_tick = now;
        return;
    }
    if((now - port->write_failing_since_tick) >= furi_ms_to_ticks(USBC_POWER_FAULT_TIMEOUT_MS)) {
        FURI_LOG_E(
            TAG,
            "TCPP03 0x%02X write failed for %u ms",
            usbc_power_addresses[i] >> 1,
            USBC_POWER_FAULT_TIMEOUT_MS);
        port->write_failing = false;
        usbc_power_set_faulted(power, i);
    }
}

static bool usbc_power_is_ocp_recovery_word(const Tcpp03Control* control) {
    return !control->provider_switch_closed && !control->vbus_discharge_enabled;
}

static void usbc_power_apply(UsbcPower* power) {
    if(power->provider_blocked && !usbc_power_is_provider_closed(power)) {
        FURI_LOG_I(TAG, "Provider switch unblocked");
        power->provider_blocked = false;
    }
    for(UsbcPort i = 0; i < UsbcPortCount; i++) {
        UsbcPowerPort* port = &power->ports[i];
        if(port->faulted) {
            continue;
        }
        const Tcpp03Control control = usbc_power_get_control(power, i);
        if(!port->rewrite_pending && usbc_power_is_control_equal(&control, &port->written)) {
            continue;
        }
        if(!usbc_power_write_chip(i, &control)) {
            usbc_power_on_write_failed(power, i, &control);
            continue;
        }
        if(port->write_failing) {
            FURI_LOG_I(TAG, "TCPP03 0x%02X write recovered", usbc_power_addresses[i] >> 1);
            port->write_failing = false;
        }
        if((control.power_mode == Tcpp03PowerModeNormal) &&
           (port->written.power_mode != Tcpp03PowerModeNormal)) {
            port->flag_pending = true;
        }
        port->written = control;
        port->rewrite_pending = false;
        usbc_power_log_control(i, &control);
        if(port->ocp_recovery_pending && usbc_power_is_ocp_recovery_word(&control)) {
            FURI_LOG_I(
                TAG, "TCPP03 0x%02X OCP recovery word written", usbc_power_addresses[i] >> 1);
            port->ocp_recovery_pending = false;
        }
    }
}

static bool usbc_power_take_flag(UsbcPowerPort* port) {
    FURI_CRITICAL_ENTER();
    const bool flag_pending = port->flag_pending;
    port->flag_pending = false;
    FURI_CRITICAL_EXIT();
    return flag_pending;
}

static bool usbc_power_is_trip_limit_reached(UsbcPowerPort* port) {
    const uint32_t now = furi_get_tick();

    if((port->trip_count == 0) ||
       ((now - port->trip_window_start_tick) >= furi_ms_to_ticks(USBC_POWER_TRIP_WINDOW_MS))) {
        port->trip_count = 0;
        port->trip_window_start_tick = now;
    }
    port->trip_count++;
    return port->trip_count >= USBC_POWER_TRIP_LIMIT;
}

static void usbc_power_trip(UsbcPower* power, UsbcPort i, bool ocp) {
    UsbcPowerPort* port = &power->ports[i];

    if(i == UsbcPortPhone) {
        power->provider_blocked = true;
    }
    if(usbc_power_is_trip_limit_reached(port)) {
        FURI_LOG_E(
            TAG,
            "TCPP03 0x%02X tripped %u times in %u ms",
            usbc_power_addresses[i] >> 1,
            USBC_POWER_TRIP_LIMIT,
            USBC_POWER_TRIP_WINDOW_MS);
        usbc_power_set_faulted(power, i);
        return;
    }
    port->trip_event_pending = true;
    if(ocp) {
        port->ocp_recovery_pending = true;
        port->rewrite_pending = true;
    }
}

static void usbc_power_check_flags(UsbcPower* power, UsbcPort i) {
    UsbcPowerPort* port = &power->ports[i];
    uint8_t flags;

    if(!usbc_power_take_flag(port) || port->faulted ||
       (port->written.power_mode != Tcpp03PowerModeNormal)) {
        return;
    }
    if(!usbc_power_read_flags(i, &flags)) {
        FURI_LOG_E(TAG, "TCPP03 0x%02X flags read failed", usbc_power_addresses[i] >> 1);
        usbc_power_set_faulted(power, i);
        return;
    }
    if(flags & USBC_POWER_TRIP_FLAGS) {
        FURI_LOG_E(
            TAG, "TCPP03 0x%02X tripped, flags 0x%02X", usbc_power_addresses[i] >> 1, flags);
        usbc_power_trip(power, i, flags & USBC_POWER_OCP_FLAGS);
    } else if(flags & USBC_POWER_FAULT_FLAGS) {
        FURI_LOG_W(TAG, "TCPP03 0x%02X flags 0x%02X", usbc_power_addresses[i] >> 1, flags);
    }
}

static void usbc_power_on_flag(UsbcPower* power, UsbcPort i) {
    power->ports[i].flag_pending = true;
    power->wakeup_callback(power->context);
}

static void usbc_power_phone_flag_callback(void* context) {
    usbc_power_on_flag(context, UsbcPortPhone);
}

static void usbc_power_charger_flag_callback(void* context) {
    usbc_power_on_flag(context, UsbcPortCharger);
}

UsbcPower*
    usbc_power_alloc(UsbcVbus* vbus, UsbcPowerWakeupCallback wakeup_callback, void* context) {
    furi_check(vbus);
    furi_check(wakeup_callback);

    UsbcPower* power = malloc(sizeof(UsbcPower));
    power->vbus = vbus;
    power->wakeup_callback = wakeup_callback;
    power->context = context;

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

    furi_hal_gpio_add_int_callback(
        usbc_power_flag_pins[UsbcPortPhone], usbc_power_phone_flag_callback, power);
    furi_hal_gpio_add_int_callback(
        usbc_power_flag_pins[UsbcPortCharger], usbc_power_charger_flag_callback, power);

    return power;
}

void usbc_power_free(UsbcPower* power) {
    for(UsbcPort i = 0; i < UsbcPortCount; i++) {
        furi_hal_gpio_remove_int_callback(usbc_power_flag_pins[i]);
    }
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
    for(UsbcPort i = 0; i < UsbcPortCount; i++) {
        usbc_power_check_flags(power, i);
    }
    usbc_power_apply(power);
}

uint32_t usbc_power_get_next_ms(UsbcPower* power) {
    uint32_t next_ms = UINT32_MAX;

    for(UsbcPort i = 0; i < UsbcPortCount; i++) {
        if(power->ports[i].flag_pending) {
            return 0;
        }
        if(power->ports[i].write_failing) {
            next_ms = USBC_POWER_WRITE_RETRY_MS;
        }
    }
    return next_ms;
}

void usbc_power_trip_provider(UsbcPower* power) {
    if(power->ports[UsbcPortPhone].faulted) {
        return;
    }
    usbc_power_trip(power, UsbcPortPhone, false);
    usbc_power_apply(power);
}

bool usbc_power_take_trip(UsbcPower* power, UsbcPort port) {
    furi_check(port < UsbcPortCount);

    const bool trip_event_pending = power->ports[port].trip_event_pending;
    power->ports[port].trip_event_pending = false;
    return trip_event_pending;
}

bool usbc_power_is_faulted(UsbcPower* power, UsbcPort port) {
    furi_check(port < UsbcPortCount);
    return power->ports[port].faulted;
}

bool usbc_power_take_fault_change(UsbcPower* power) {
    const bool fault_changed = power->fault_changed;
    power->fault_changed = false;
    return fault_changed;
}
