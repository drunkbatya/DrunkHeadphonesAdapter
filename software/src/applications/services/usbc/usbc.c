#include <applications/services/usbc/usbc.h>
#include <applications/services/usbc/usbc_platform.h>
#include <applications/services/usbc/usbc_policy.h>
#include <applications/services/usbc/usbc_power.h>
#include <applications/services/usbc/usbc_vbus.h>

#include <furi.h>

typedef enum {
    UsbcThreadFlagWakeup = (1 << 0),
} UsbcThreadFlag;

typedef struct {
    FuriThreadId thread_id;
    FuriEventLoop* event_loop;
    FuriEventLoopTimer* process_timer;
    UsbcVbus* vbus;
    UsbcPower* power;
    UsbcPolicy* policy;
} Usbc;

static void usbc_wakeup_callback(void* context) {
    UNUSED(context);
}

static void usbc_thread_flags_callback(void* context) {
    UNUSED(context);
}

static void usbc_process_timer_callback(void* context) {
    UNUSED(context);
}

static Usbc* usbc_alloc(void) {
    Usbc* usbc = malloc(sizeof(Usbc));
    usbc->thread_id = furi_thread_get_current_id();
    usbc->event_loop = furi_event_loop_alloc();
    usbc->process_timer = furi_event_loop_timer_alloc(
        usbc->event_loop, usbc_process_timer_callback, FuriEventLoopTimerTypeOnce, usbc);
    usbc->vbus = usbc_vbus_alloc(usbc_wakeup_callback, usbc);
    usbc->power = usbc_power_alloc();
    usbc->policy = usbc_policy_alloc(usbc->power, usbc->vbus, usbc_wakeup_callback, usbc);
    usbc_platform_init(usbc->vbus);
    usbc_policy_start(usbc->policy);
    furi_event_loop_subscribe_thread_flags(usbc->event_loop, usbc_thread_flags_callback, usbc);
    return usbc;
}

static void usbc_free(Usbc* usbc) {
    furi_event_loop_unsubscribe_thread_flags(usbc->event_loop);
    usbc_platform_deinit();
    usbc_policy_free(usbc->policy);
    usbc_power_free(usbc->power);
    usbc_vbus_free(usbc->vbus);
    furi_event_loop_timer_free(usbc->process_timer);
    furi_event_loop_free(usbc->event_loop);
    free(usbc);
}

int32_t usbc_srv(void* context) {
    UNUSED(context);

    Usbc* usbc = usbc_alloc();
    furi_event_loop_run(usbc->event_loop);
    usbc_free(usbc);

    return 0;
}
