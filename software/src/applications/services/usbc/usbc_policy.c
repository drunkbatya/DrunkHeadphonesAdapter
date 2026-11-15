#include <applications/services/usbc/usbc_policy.h>

#include <furi.h>

#include <usb_pd.h>
#include <usb_pd_pdo.h>

#define TAG "UsbcPolicy"

#define USBC_POLICY_SAFE5V_MV        5000
#define USBC_POLICY_BOARD_CURRENT_MA 150

typedef enum {
    UsbcPolicyLogSourceCapabilities = (1 << 0),
    UsbcPolicyLogSinkCapabilities = (1 << 1),
    UsbcPolicyLogContract = (1 << 2),
} UsbcPolicyLog;

typedef struct {
    UsbPdCapabilities partner_source_capabilities;
    UsbPdCapabilities partner_sink_capabilities;
    UsbPdContract contract;
    uint32_t pending_logs;
} UsbcPolicyPort;

struct UsbcPolicy {
    UsbcPower* power;
    UsbcVbus* vbus;
    UsbcPolicyWakeupCallback wakeup_callback;
    void* context;
    UsbcPolicyPort ports[UsbcPortCount];
};

static const char* const usbc_policy_port_names[UsbcPortCount] = {
    [UsbcPortPhone] = "Phone",
    [UsbcPortCharger] = "Charger",
};

static const UsbPdPortConfig usbc_policy_port_configs[UsbcPortCount] = {
    [UsbcPortPhone] =
        {
            .mode = UsbPdPortModeSink,
            .rp_current = UsbPdRpCurrentDefault,
            .spec_revision = UsbPdSpecRevision3,
        },
    [UsbcPortCharger] =
        {
            .mode = UsbPdPortModeSink,
            .rp_current = UsbPdRpCurrentDefault,
            .spec_revision = UsbPdSpecRevision3,
        },
};

static const char* const usbc_policy_rp_current_names[] = {
    [UsbPdRpCurrentDefault] = "Default",
    [UsbPdRpCurrent1A5] = "1.5A",
    [UsbPdRpCurrent3A0] = "3.0A",
};

static const char* const usbc_policy_fast_role_swap_current_names[] = {
    [UsbPdFastRoleSwapCurrentNone] = "None",
    [UsbPdFastRoleSwapCurrentDefault] = "Default",
    [UsbPdFastRoleSwapCurrent1A5] = "1.5A",
    [UsbPdFastRoleSwapCurrent3A0] = "3.0A",
};

static const char* const usbc_policy_power_names[] = {
    [UsbPdPowerSourceNewContract] = "SourceNewContract",
    [UsbPdPowerSourceOff] = "SourceOff",
    [UsbPdPowerSourceOn] = "SourceOn",
    [UsbPdPowerSinkStandby] = "SinkStandby",
    [UsbPdPowerSinkOff] = "SinkOff",
    [UsbPdPowerSinkNewContract] = "SinkNewContract",
    [UsbPdPowerSinkHardReset] = "SinkHardReset",
};

static const char* usbc_policy_get_power_role_name(UsbPdPowerRole power_role) {
    return (power_role == UsbPdPowerRoleSource) ? "source" : "sink";
}

static uint32_t usbc_policy_get_contract_voltage_mv(const UsbPdContract* contract) {
    if(usb_pd_pdo_is_pps(contract->pdo)) {
        return usb_pd_rdo_get_pps_voltage_mv(contract->rdo);
    }
    return usb_pd_pdo_get_max_voltage_mv(contract->pdo);
}

static void
    usbc_policy_log_pdo(const char* port_name, bool source, uint8_t position, uint32_t pdo) {
    const char* kind = source ? "source" : "sink";
    const uint32_t min_voltage_mv = usb_pd_pdo_get_min_voltage_mv(pdo);
    const uint32_t max_voltage_mv = usb_pd_pdo_get_max_voltage_mv(pdo);
    const uint32_t max_current_ma = usb_pd_pdo_get_max_current_ma(pdo);

    switch(usb_pd_pdo_get_type(pdo)) {
    case UsbPdPdoTypeFixed:
        FURI_LOG_I(
            TAG,
            "%s %s PDO%u 0x%08lX fixed %lu mV %lu mA%s%s%s%s%s",
            port_name,
            kind,
            position,
            pdo,
            max_voltage_mv,
            max_current_ma,
            usb_pd_pdo_is_dual_role_power(pdo) ? ", dual role power" : "",
            (source && usb_pd_pdo_is_usb_suspend_supported(pdo)) ? ", usb suspend supported" : "",
            usb_pd_pdo_is_unconstrained_power(pdo) ? ", unconstrained power" : "",
            usb_pd_pdo_is_usb_communications_capable(pdo) ? ", usb communications" : "",
            usb_pd_pdo_is_dual_role_data(pdo) ? ", dual role data" : "");
        break;
    case UsbPdPdoTypeBattery:
        FURI_LOG_I(
            TAG,
            "%s %s PDO%u 0x%08lX battery %lu-%lu mV %lu mA",
            port_name,
            kind,
            position,
            pdo,
            min_voltage_mv,
            max_voltage_mv,
            max_current_ma);
        break;
    case UsbPdPdoTypeVariable:
        FURI_LOG_I(
            TAG,
            "%s %s PDO%u 0x%08lX variable %lu-%lu mV %lu mA",
            port_name,
            kind,
            position,
            pdo,
            min_voltage_mv,
            max_voltage_mv,
            max_current_ma);
        break;
    case UsbPdPdoTypeAugmented:
        if(usb_pd_pdo_is_pps(pdo)) {
            FURI_LOG_I(
                TAG,
                "%s %s PDO%u 0x%08lX pps %lu-%lu mV %lu mA",
                port_name,
                kind,
                position,
                pdo,
                min_voltage_mv,
                max_voltage_mv,
                max_current_ma);
        } else {
            FURI_LOG_I(TAG, "%s %s PDO%u 0x%08lX augmented", port_name, kind, position, pdo);
        }
        break;
    }
}

static void usbc_policy_log_capabilities(
    const char* port_name,
    bool source,
    const UsbPdCapabilities* capabilities) {
    for(uint8_t i = 0; i < capabilities->count; i++) {
        usbc_policy_log_pdo(port_name, source, i + 1, capabilities->objects[i]);
    }
}

static void usbc_policy_log_contract(const char* port_name, const UsbcPolicyPort* port) {
    const UsbPdContract* contract = &port->contract;
    FURI_LOG_I(
        TAG,
        "%s contract: PDO%u of %u (%lu mV, up to %lu mA), requested %lu mA%s",
        port_name,
        usb_pd_rdo_get_object_position(contract->rdo),
        port->partner_source_capabilities.count,
        usbc_policy_get_contract_voltage_mv(contract),
        usb_pd_pdo_get_max_current_ma(contract->pdo),
        usb_pd_rdo_get_operating_current_ma(contract->rdo, contract->pdo),
        usb_pd_rdo_is_capability_mismatch(contract->rdo) ? ", capability mismatch" : "");
}

static void usbc_policy_flush_logs(UsbcPolicy* policy) {
    for(UsbcPort i = 0; i < UsbcPortCount; i++) {
        UsbcPolicyPort* port = &policy->ports[i];
        const char* port_name = usbc_policy_port_names[i];
        const uint32_t pending_logs = port->pending_logs;
        port->pending_logs = 0;

        if(pending_logs & UsbcPolicyLogSourceCapabilities) {
            usbc_policy_log_capabilities(port_name, true, &port->partner_source_capabilities);
        }
        if(pending_logs & UsbcPolicyLogSinkCapabilities) {
            usbc_policy_log_capabilities(port_name, false, &port->partner_sink_capabilities);
            if(port->partner_sink_capabilities.count > 0) {
                FURI_LOG_I(
                    TAG,
                    "%s sink fast role swap current %s",
                    port_name,
                    usbc_policy_fast_role_swap_current_names[usb_pd_pdo_get_fast_role_swap_current(
                        port->partner_sink_capabilities.objects[0])]);
            }
        }
        if(pending_logs & UsbcPolicyLogContract) {
            usbc_policy_log_contract(port_name, port);
        }
    }
}

static void usbc_policy_on_event(void* context, uint8_t port, const UsbPdEvent* event) {
    furi_check(port < UsbcPortCount);
    UsbcPolicy* policy = context;
    UsbcPolicyPort* policy_port = &policy->ports[port];
    const char* port_name = usbc_policy_port_names[port];

    switch(event->type) {
    case UsbPdEventTypeAttached:
        FURI_LOG_I(
            TAG,
            "%s attached as %s, CC%u, partner Rp %s",
            port_name,
            usbc_policy_get_power_role_name(event->attach.power_role),
            event->attach.pin,
            usbc_policy_rp_current_names[event->attach.partner_rp_current]);
        usbc_power_set_attached(policy->power, port, true);
        break;
    case UsbPdEventTypeDetached:
        FURI_LOG_I(TAG, "%s detached", port_name);
        memset(policy_port, 0, sizeof(UsbcPolicyPort));
        usbc_vbus_set_contract_voltage(policy->vbus, port, USBC_POLICY_SAFE5V_MV);
        usbc_power_set_attached(policy->power, port, false);
        break;
    case UsbPdEventTypeSourceCapabilities:
        policy_port->partner_source_capabilities = *event->capabilities;
        policy_port->pending_logs |= UsbcPolicyLogSourceCapabilities;
        break;
    case UsbPdEventTypeSinkCapabilities:
        policy_port->partner_sink_capabilities = *event->capabilities;
        policy_port->pending_logs |= UsbcPolicyLogSinkCapabilities;
        break;
    case UsbPdEventTypeContract:
        policy_port->contract = event->contract;
        policy_port->pending_logs |= UsbcPolicyLogContract;
        usbc_vbus_set_contract_voltage(
            policy->vbus, port, usbc_policy_get_contract_voltage_mv(&event->contract));
        break;
    case UsbPdEventTypeHardReset:
        FURI_LOG_I(
            TAG, "%s hard reset %s", port_name, event->hard_reset_received ? "received" : "sent");
        memset(&policy_port->contract, 0, sizeof(UsbPdContract));
        policy_port->pending_logs &= ~UsbcPolicyLogContract;
        usbc_vbus_set_contract_voltage(policy->vbus, port, USBC_POLICY_SAFE5V_MV);
        break;
    case UsbPdEventTypeSoftReset:
        FURI_LOG_I(TAG, "%s soft reset", port_name);
        break;
    case UsbPdEventTypePartnerNotPd:
        FURI_LOG_I(TAG, "%s partner is not PD capable", port_name);
        break;
    case UsbPdEventTypePowerRoleSwapped:
    case UsbPdEventTypeFastRoleSwapped:
        FURI_LOG_I(
            TAG, "%s power role %s", port_name, usbc_policy_get_power_role_name(event->power_role));
        break;
    default:
        break;
    }
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
    capabilities->objects[0] =
        usb_pd_pdo_make_fixed(USBC_POLICY_SAFE5V_MV, USBC_POLICY_BOARD_CURRENT_MA);
    capabilities->count = 1;
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
    const uint32_t pdo = source_capabilities->objects[0];
    const uint32_t max_current_ma = usb_pd_pdo_get_max_current_ma(pdo);
    const uint32_t current_ma = MIN((uint32_t)USBC_POLICY_BOARD_CURRENT_MA, max_current_ma);
    const uint32_t rdo = usb_pd_rdo_make(1, pdo, current_ma, 0);
    return usb_pd_rdo_set_capability_mismatch(rdo, max_current_ma < USBC_POLICY_BOARD_CURRENT_MA);
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
    furi_check(port < UsbcPortCount);
    UsbcPolicy* policy = context;

    FURI_LOG_D(TAG, "%s power %s", usbc_policy_port_names[port], usbc_policy_power_names[power]);
    usbc_power_set(policy->power, port, power);
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
    furi_check(power);
    furi_check(vbus);
    furi_check(wakeup_callback);

    UsbcPolicy* policy = malloc(sizeof(UsbcPolicy));
    policy->power = power;
    policy->vbus = vbus;
    policy->wakeup_callback = wakeup_callback;
    policy->context = context;
    usb_pd_init(&usbc_policy_pd_callbacks, policy);
    return policy;
}

void usbc_policy_start(UsbcPolicy* policy) {
    for(UsbcPort i = 0; i < UsbcPortCount; i++) {
        UsbPdPortConfig config = usbc_policy_port_configs[i];
        if(usbc_power_is_faulted(policy->power, i)) {
            FURI_LOG_E(TAG, "%s disabled, TCPP03 faulted", usbc_policy_port_names[i]);
            config.mode = UsbPdPortModeDisabled;
        }
        furi_check(usb_pd_port_init(i, &config));
    }
    usbc_power_start(policy->power);
}

void usbc_policy_free(UsbcPolicy* policy) {
    free(policy);
}

uint32_t usbc_policy_process(UsbcPolicy* policy) {
    const uint32_t next_ms = usb_pd_process();
    if(next_ms != 0) {
        usbc_policy_flush_logs(policy);
    }
    return next_ms;
}
