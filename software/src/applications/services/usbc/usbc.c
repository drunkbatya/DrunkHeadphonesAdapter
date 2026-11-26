#include <applications/services/usbc/usbc.h>
#include <applications/services/usbc/usbc_bypass.h>
#include <applications/services/usbc/usbc_platform.h>
#include <applications/services/usbc/usbc_policy.h>
#include <applications/services/usbc/usbc_power.h>
#include <applications/services/usbc/usbc_vbus.h>

#include <furi.h>
#include <furi_hal.h>

#define USBC_WATCHDOG_FEED_MS (FURI_HAL_WATCHDOG_TIMEOUT_MIN_MS / 2)

typedef enum {
    UsbcThreadFlagWakeup = (1 << 0),
} UsbcThreadFlag;

struct Usbc {
    FuriThreadId thread_id;
    FuriEventLoop* event_loop;
    FuriEventLoopTimer* process_timer;
    FuriEventLoopTimer* watchdog_timer;
    UsbcVbus* vbus;
    UsbcPower* power;
    UsbcPolicy* policy;
    UsbcBypass* bypass;
    FuriMutex* status_mutex;
    UsbcStatus status;
};

static void usbc_wakeup_callback(void* context) {
    Usbc* usbc = context;
    furi_thread_flags_set(usbc->thread_id, UsbcThreadFlagWakeup);
}

static void usbc_update_status(Usbc* usbc) {
    const bool policy_changed = usbc_policy_take_status_change(usbc->policy);
    const bool power_changed = usbc_power_take_fault_change(usbc->power);
    const bool vbus_changed = usbc_vbus_take_level_change(usbc->vbus);
    if(!policy_changed && !power_changed && !vbus_changed) {
        return;
    }

    furi_check(furi_mutex_acquire(usbc->status_mutex, FuriWaitForever) == FuriStatusOk);
    usbc_policy_fill_status(usbc->policy, &usbc->status);
    furi_mutex_release(usbc->status_mutex);
}

static void usbc_process(Usbc* usbc) {
    const uint32_t vbus_next_ms = usbc_vbus_process(usbc->vbus);
    usbc_power_process(usbc->power);
    const uint32_t policy_next_ms = usbc_policy_process(usbc->policy);
    const uint32_t bypass_next_ms = usbc_bypass_process(
        usbc->bypass,
        usbc_policy_is_attached(usbc->policy, UsbcPortPhone),
        usbc_policy_is_attached(usbc->policy, UsbcPortCharger));
    usbc_policy_set_usb_bypass(usbc->policy, usbc_bypass_is_enabled(usbc->bypass));
    const uint32_t power_next_ms = usbc_power_get_next_ms(usbc->power);
    const uint32_t next_ms =
        MIN(MIN(vbus_next_ms, bypass_next_ms), MIN(policy_next_ms, power_next_ms));

    if(next_ms == 0) {
        furi_event_loop_timer_stop(usbc->process_timer);
        furi_thread_flags_set(usbc->thread_id, UsbcThreadFlagWakeup);
        return;
    }

    usbc_update_status(usbc);

    if(next_ms == UINT32_MAX) {
        furi_event_loop_timer_stop(usbc->process_timer);
    } else {
        furi_event_loop_timer_start(usbc->process_timer, furi_ms_to_ticks(next_ms));
    }
}

static void usbc_thread_flags_callback(void* context) {
    Usbc* usbc = context;

    const uint32_t flags = furi_thread_flags_get();
    furi_thread_flags_clear(flags);

    if(flags & UsbcThreadFlagWakeup) {
        usbc_process(usbc);
    }
}

static void usbc_process_timer_callback(void* context) {
    Usbc* usbc = context;
    usbc_process(usbc);
}

static void usbc_watchdog_timer_callback(void* context) {
    UNUSED(context);
    furi_hal_watchdog_feed();
}

static Usbc* usbc_alloc(void) {
    Usbc* usbc = malloc(sizeof(Usbc));
    usbc->thread_id = furi_thread_get_current_id();
    usbc->event_loop = furi_event_loop_alloc();
    usbc->process_timer = furi_event_loop_timer_alloc(
        usbc->event_loop, usbc_process_timer_callback, FuriEventLoopTimerTypeOnce, usbc);
    usbc->watchdog_timer = furi_event_loop_timer_alloc(
        usbc->event_loop, usbc_watchdog_timer_callback, FuriEventLoopTimerTypePeriodic, usbc);
    furi_hal_watchdog_start();
    furi_event_loop_timer_start(usbc->watchdog_timer, furi_ms_to_ticks(USBC_WATCHDOG_FEED_MS));
    usbc->vbus = usbc_vbus_alloc(usbc_wakeup_callback, usbc);
    usbc->power = usbc_power_alloc(usbc->vbus, usbc_wakeup_callback, usbc);
    usbc->policy = usbc_policy_alloc(usbc->power, usbc->vbus, usbc_wakeup_callback, usbc);
    usbc->bypass = usbc_bypass_alloc(usbc_wakeup_callback, usbc);
    usbc_policy_set_usb_bypass(usbc->policy, usbc_bypass_is_enabled(usbc->bypass));
    usbc->status_mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    usbc_platform_init(usbc->vbus);
    usbc_policy_start(usbc->policy);
    furi_event_loop_subscribe_thread_flags(usbc->event_loop, usbc_thread_flags_callback, usbc);
    furi_thread_flags_set(usbc->thread_id, UsbcThreadFlagWakeup);
    furi_record_create(RECORD_USBC, usbc);
    return usbc;
}

static void usbc_free(Usbc* usbc) {
    furi_record_destroy(RECORD_USBC);
    furi_event_loop_unsubscribe_thread_flags(usbc->event_loop);
    usbc_platform_deinit();
    usbc_bypass_free(usbc->bypass);
    usbc_policy_free(usbc->policy);
    usbc_power_free(usbc->power);
    usbc_vbus_free(usbc->vbus);
    furi_mutex_free(usbc->status_mutex);
    furi_event_loop_timer_free(usbc->watchdog_timer);
    furi_event_loop_timer_free(usbc->process_timer);
    furi_event_loop_free(usbc->event_loop);
    free(usbc);
}

void usbc_get_status(Usbc* usbc, UsbcStatus* status) {
    furi_check(usbc);
    furi_check(status);

    furi_check(furi_mutex_acquire(usbc->status_mutex, FuriWaitForever) == FuriStatusOk);
    *status = usbc->status;
    furi_mutex_release(usbc->status_mutex);
}

void usbc_request_temporary_usb_bypass(Usbc* usbc) {
    furi_check(usbc);

    usbc_bypass_request_temporary(usbc->bypass);
}

int32_t usbc_srv(void* context) {
    UNUSED(context);

    Usbc* usbc = usbc_alloc();
    furi_event_loop_run(usbc->event_loop);
    usbc_free(usbc);

    return 0;
}
