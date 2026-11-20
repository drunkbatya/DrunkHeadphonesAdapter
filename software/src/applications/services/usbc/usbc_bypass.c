#include <applications/services/usbc/usbc_bypass.h>

#include <furi.h>
#include <furi_hal.h>

#define TAG "UsbcBypass"

#define USBC_BYPASS_DEBOUNCE_MS 50

struct UsbcBypass {
    UsbcBypassWakeupCallback wakeup_callback;
    void* context;
    volatile bool switch_changed;
    bool settling;
    uint32_t settle_start_tick;
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

static bool usbc_bypass_take_switch_change(UsbcBypass* bypass) {
    FURI_CRITICAL_ENTER();
    const bool switch_changed = bypass->switch_changed;
    bypass->switch_changed = false;
    FURI_CRITICAL_EXIT();
    return switch_changed;
}

UsbcBypass* usbc_bypass_alloc(UsbcBypassWakeupCallback wakeup_callback, void* context) {
    furi_check(wakeup_callback);

    UsbcBypass* bypass = malloc(sizeof(UsbcBypass));
    bypass->wakeup_callback = wakeup_callback;
    bypass->context = context;
    bypass->enabled = furi_hal_gpio_read(&gpio_usb_bypass_switch);
    usbc_bypass_apply(bypass->enabled);
    furi_hal_gpio_add_int_callback(&gpio_usb_bypass_switch, usbc_bypass_switch_callback, bypass);
    return bypass;
}

void usbc_bypass_free(UsbcBypass* bypass) {
    furi_check(bypass);

    furi_hal_gpio_remove_int_callback(&gpio_usb_bypass_switch);
    free(bypass);
}

uint32_t usbc_bypass_process(UsbcBypass* bypass) {
    furi_check(bypass);

    const uint32_t debounce_ticks = furi_ms_to_ticks(USBC_BYPASS_DEBOUNCE_MS);
    if(usbc_bypass_take_switch_change(bypass)) {
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

    const bool enabled = furi_hal_gpio_read(&gpio_usb_bypass_switch);
    if(enabled != bypass->enabled) {
        bypass->enabled = enabled;
        usbc_bypass_apply(enabled);
    }
    return UINT32_MAX;
}

bool usbc_bypass_is_enabled(const UsbcBypass* bypass) {
    furi_check(bypass);

    return bypass->enabled;
}
