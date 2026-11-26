#include <applications/services/usbc/usbc_bypass.h>

#include <furi.h>
#include <furi_hal.h>

#define TAG "UsbcBypass"

#define USBC_BYPASS_DEBOUNCE_MS 50

struct UsbcBypass {
    UsbcBypassWakeupCallback wakeup_callback;
    void* context;
    volatile bool switch_changed;
    volatile bool temporary_requested;
    bool settling;
    uint32_t settle_start_tick;
    bool switch_on;
    bool temporary;
    bool charger_attached_during_temporary;
    bool enabled;
};

static void usbc_bypass_switch_callback(void* context) {
    UsbcBypass* bypass = context;
    bypass->switch_changed = true;
    bypass->wakeup_callback(bypass->context);
}

static void usbc_bypass_apply(bool enabled) {
    if(enabled) {
        furi_hal_usb_disable();
        furi_hal_gpio_write(&gpio_usb_mux_select, true);
    } else {
        furi_hal_gpio_write(&gpio_usb_mux_select, false);
        furi_hal_usb_enable();
    }
    FURI_LOG_I(TAG, "USB data %s", enabled ? "bypassed to charger port" : "on adapter");
}

static bool usbc_bypass_take_flag(volatile bool* flag) {
    FURI_CRITICAL_ENTER();
    const bool value = *flag;
    *flag = false;
    FURI_CRITICAL_EXIT();
    return value;
}

static void usbc_bypass_stop_temporary(UsbcBypass* bypass, const char* reason) {
    bypass->temporary = false;
    FURI_LOG_I(TAG, "Temporary bypass off, %s", reason);
}

static uint32_t usbc_bypass_debounce_switch(UsbcBypass* bypass) {
    const uint32_t debounce_ticks = furi_ms_to_ticks(USBC_BYPASS_DEBOUNCE_MS);
    if(usbc_bypass_take_flag(&bypass->switch_changed)) {
        bypass->settling = true;
        bypass->settle_start_tick = furi_get_tick();
        return USBC_BYPASS_DEBOUNCE_MS;
    }
    if(!bypass->settling) {
        return UINT32_MAX;
    }

    const uint32_t elapsed_ticks = furi_get_tick() - bypass->settle_start_tick;
    if(elapsed_ticks < debounce_ticks) {
        return MAX(debounce_ticks - elapsed_ticks, 1UL);
    }
    bypass->settling = false;

    const bool switch_on = furi_hal_gpio_read(&gpio_usb_bypass_switch);
    if(switch_on != bypass->switch_on) {
        bypass->switch_on = switch_on;
        FURI_LOG_I(TAG, "Switch %s", switch_on ? "on" : "off");
        if(bypass->temporary) {
            usbc_bypass_stop_temporary(bypass, "switch moved");
        }
    }
    return UINT32_MAX;
}

static void usbc_bypass_start_temporary(UsbcBypass* bypass, bool charger_attached) {
    if(bypass->switch_on || bypass->temporary) {
        FURI_LOG_W(
            TAG,
            "Temporary bypass request ignored, switch %s, temporary %s",
            bypass->switch_on ? "on" : "off",
            bypass->temporary ? "on" : "off");
        return;
    }
    bypass->temporary = true;
    bypass->charger_attached_during_temporary = charger_attached;
    FURI_LOG_I(
        TAG, "Temporary bypass on, charger port %s", charger_attached ? "attached" : "empty");
}

static void
    usbc_bypass_update_temporary(UsbcBypass* bypass, bool phone_attached, bool charger_attached) {
    if(!bypass->temporary) {
        return;
    }
    if(!phone_attached) {
        usbc_bypass_stop_temporary(bypass, "phone detached");
    } else if(charger_attached) {
        bypass->charger_attached_during_temporary = true;
    } else if(bypass->charger_attached_during_temporary) {
        usbc_bypass_stop_temporary(bypass, "charger port detached");
    }
}

UsbcBypass* usbc_bypass_alloc(UsbcBypassWakeupCallback wakeup_callback, void* context) {
    furi_check(wakeup_callback);

    UsbcBypass* bypass = malloc(sizeof(UsbcBypass));
    bypass->wakeup_callback = wakeup_callback;
    bypass->context = context;
    bypass->switch_on = furi_hal_gpio_read(&gpio_usb_bypass_switch);
    bypass->enabled = bypass->switch_on;
    usbc_bypass_apply(bypass->enabled);
    furi_hal_gpio_add_int_callback(&gpio_usb_bypass_switch, usbc_bypass_switch_callback, bypass);
    return bypass;
}

void usbc_bypass_free(UsbcBypass* bypass) {
    furi_check(bypass);

    furi_hal_gpio_remove_int_callback(&gpio_usb_bypass_switch);
    free(bypass);
}

void usbc_bypass_request_temporary(UsbcBypass* bypass) {
    furi_check(bypass);

    bypass->temporary_requested = true;
    bypass->wakeup_callback(bypass->context);
}

uint32_t usbc_bypass_process(UsbcBypass* bypass, bool phone_attached, bool charger_attached) {
    furi_check(bypass);

    uint32_t next_ms = usbc_bypass_debounce_switch(bypass);
    if(usbc_bypass_take_flag(&bypass->temporary_requested)) {
        usbc_bypass_start_temporary(bypass, charger_attached);
    }
    usbc_bypass_update_temporary(bypass, phone_attached, charger_attached);

    const bool enabled = bypass->switch_on || bypass->temporary;
    if(enabled != bypass->enabled) {
        bypass->enabled = enabled;
        usbc_bypass_apply(enabled);
        next_ms = 0;
    }
    return next_ms;
}

bool usbc_bypass_is_enabled(const UsbcBypass* bypass) {
    furi_check(bypass);

    return bypass->enabled;
}
