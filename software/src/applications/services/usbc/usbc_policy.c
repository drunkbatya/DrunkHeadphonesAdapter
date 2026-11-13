#include <applications/services/usbc/usbc_policy.h>

#include <furi.h>

#include <usb_pd.h>

struct UsbcPolicy {
    UsbcPower* power;
    UsbcVbus* vbus;
    UsbcPolicyWakeupCallback wakeup_callback;
    void* context;
};

static void usbc_policy_on_event(void* context, uint8_t port, const UsbPdEvent* event) {
    UNUSED(context);
    UNUSED(port);
    UNUSED(event);
}

static void usbc_policy_get_source_capabilities(
    void* context,
    uint8_t port,
    UsbPdCapabilities* capabilities) {
    UNUSED(context);
    UNUSED(port);
    UNUSED(capabilities);
}

static void usbc_policy_get_sink_capabilities(
    void* context,
    uint8_t port,
    UsbPdCapabilities* capabilities) {
    UNUSED(context);
    UNUSED(port);
    UNUSED(capabilities);
}

static bool usbc_policy_get_pps_status(void* context, uint8_t port, UsbPdPpsStatus* status) {
    UNUSED(context);
    UNUSED(port);
    UNUSED(status);
    return false;
}

static UsbPdAnswer usbc_policy_evaluate_request(void* context, uint8_t port, uint32_t rdo) {
    UNUSED(context);
    UNUSED(port);
    UNUSED(rdo);
    return UsbPdAnswerReject;
}

static uint32_t usbc_policy_select_capability(
    void* context,
    uint8_t port,
    const UsbPdCapabilities* source_capabilities) {
    UNUSED(context);
    UNUSED(port);
    UNUSED(source_capabilities);
    return 0;
}

static UsbPdAnswer usbc_policy_evaluate_power_role_swap(void* context, uint8_t port) {
    UNUSED(context);
    UNUSED(port);
    return UsbPdAnswerReject;
}

static UsbPdAnswer usbc_policy_evaluate_data_role_swap(void* context, uint8_t port) {
    UNUSED(context);
    UNUSED(port);
    return UsbPdAnswerReject;
}

static UsbPdAnswer usbc_policy_evaluate_vconn_swap(void* context, uint8_t port) {
    UNUSED(context);
    UNUSED(port);
    return UsbPdAnswerReject;
}

static void usbc_policy_set_power(void* context, uint8_t port, UsbPdPower power) {
    UNUSED(context);
    UNUSED(port);
    UNUSED(power);
}

static void usbc_policy_set_vconn(void* context, uint8_t port, bool enabled) {
    UNUSED(context);
    UNUSED(port);
    UNUSED(enabled);
}

static void usbc_policy_wakeup(void* context) {
    UsbcPolicy* policy = context;
    policy->wakeup_callback(policy->context);
}

static const UsbPdCallbacks usbc_policy_pd_callbacks = {
    .on_event = usbc_policy_on_event,
    .get_source_capabilities = usbc_policy_get_source_capabilities,
    .get_sink_capabilities = usbc_policy_get_sink_capabilities,
    .get_pps_status = usbc_policy_get_pps_status,
    .evaluate_request = usbc_policy_evaluate_request,
    .select_capability = usbc_policy_select_capability,
    .evaluate_power_role_swap = usbc_policy_evaluate_power_role_swap,
    .evaluate_data_role_swap = usbc_policy_evaluate_data_role_swap,
    .evaluate_vconn_swap = usbc_policy_evaluate_vconn_swap,
    .set_power = usbc_policy_set_power,
    .set_vconn = usbc_policy_set_vconn,
    .wakeup = usbc_policy_wakeup,
};

UsbcPolicy* usbc_policy_alloc(
    UsbcPower* power,
    UsbcVbus* vbus,
    UsbcPolicyWakeupCallback wakeup_callback,
    void* context) {
    UsbcPolicy* policy = malloc(sizeof(UsbcPolicy));
    policy->power = power;
    policy->vbus = vbus;
    policy->wakeup_callback = wakeup_callback;
    policy->context = context;
    usb_pd_init(&usbc_policy_pd_callbacks, policy);
    return policy;
}

void usbc_policy_start(UsbcPolicy* policy) {
    UNUSED(policy);
}

void usbc_policy_free(UsbcPolicy* policy) {
    free(policy);
}

uint32_t usbc_policy_process(UsbcPolicy* policy) {
    UNUSED(policy);
    return UINT32_MAX;
}
