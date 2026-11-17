#include <applications/services/usbc/usbc_policy.h>

#include <furi.h>

#include <usb_pd.h>
#include <usb_pd_pdo.h>

#define TAG "UsbcPolicy"

#define USBC_POLICY_SAFE5V_MV       5000
#define USBC_POLICY_MAX_VOLTAGE_MV  20000
#define USBC_POLICY_MAX_CURRENT_MA  3000
#define USBC_POLICY_BOARD_POWER_MW  750
#define USBC_POLICY_BOARD_PDP_W     1
#define USBC_POLICY_MW_PER_W        1000
#define USBC_POLICY_CURRENT_STEP_MA 10
#define USBC_POLICY_RETRY_MS        10
#define USBC_POLICY_AMS_REPEAT_MS   100

typedef enum {
    UsbcPolicyLogSourceCapabilities = (1 << 0),
    UsbcPolicyLogSinkCapabilities = (1 << 1),
    UsbcPolicyLogContract = (1 << 2),
    UsbcPolicyLogPhoneOffer = (1 << 3),
    UsbcPolicyLogPhoneRequest = (1 << 4),
    UsbcPolicyLogSinkPdp = (1 << 5),
} UsbcPolicyLog;

typedef enum {
    UsbcPolicyActionChargerPower = (1 << 0),
    UsbcPolicyActionChargerSourceCapabilities = (1 << 1),
    UsbcPolicyActionPhoneSourceCapabilities = (1 << 2),
    UsbcPolicyActionPhoneDataRoleSwap = (1 << 3),
    UsbcPolicyActionPhoneSinkCapabilities = (1 << 4),
    UsbcPolicyActionPhoneHardReset = (1 << 5),
    UsbcPolicyActionPhonePowerRoleSwap = (1 << 6),
} UsbcPolicyAction;

typedef enum {
    UsbcPolicyCapabilitiesSource,
    UsbcPolicyCapabilitiesSink,
    UsbcPolicyCapabilitiesOffer,
    UsbcPolicyCapabilitiesKindCount,
} UsbcPolicyCapabilitiesKind;

typedef struct {
    bool attached;
    bool partner_not_pd;
    UsbPdPowerRole power_role;
    UsbPdDataRole data_role;
    UsbPdRpCurrent partner_rp_current;
    UsbPdCapabilities partner_source_capabilities;
    UsbPdCapabilities partner_sink_capabilities;
    UsbPdContract contract;
    uint32_t pending_logs;
    uint8_t logged_pdo_counts[UsbcPolicyCapabilitiesKindCount];
    UsbPdSinkPdp sink_pdp;
} UsbcPolicyPort;

typedef struct {
    UsbPdCapabilities offer;
    uint32_t voltage_mv;
    uint32_t current_ma;
    bool contract_pending;
    bool power_pending;
    bool source_on_pending;
    uint32_t request_rdo;
    bool request_accepted;
    bool power_role_swap_refused;
    uint32_t power_role_swap_retry_tick;
} UsbcPolicyPhone;

struct UsbcPolicy {
    UsbcPower* power;
    UsbcVbus* vbus;
    UsbcPolicyWakeupCallback wakeup_callback;
    void* context;
    UsbcPolicyPort ports[UsbcPortCount];
    UsbPdPortMode port_modes[UsbcPortCount];
    UsbcPolicyPhone phone;
    uint32_t pending_actions;
    bool status_changed;
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
            .power_role_swap = true,
            .data_role_swap = true,
        },
    [UsbcPortCharger] =
        {
            .mode = UsbPdPortModeSink,
            .rp_current = UsbPdRpCurrentDefault,
            .spec_revision = UsbPdSpecRevision3,
        },
};

static const uint32_t usbc_policy_pass_through_voltages_mv[] = {5000, 9000, 15000, 20000};

static const char* const usbc_policy_rp_current_names[] = {
    [UsbPdRpCurrentDefault] = "Default",
    [UsbPdRpCurrent1A5] = "1.5A",
    [UsbPdRpCurrent3A0] = "3.0A",
};

static const uint32_t usbc_policy_rp_currents_ma[] = {
    [UsbPdRpCurrentDefault] = 500,
    [UsbPdRpCurrent1A5] = 1500,
    [UsbPdRpCurrent3A0] = 3000,
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

static const char* const usbc_policy_request_result_names[] = {
    [UsbPdRequestResultAccepted] = "accepted",
    [UsbPdRequestResultRejected] = "rejected",
    [UsbPdRequestResultWait] = "wait",
    [UsbPdRequestResultNotSupported] = "not supported",
    [UsbPdRequestResultNoResponse] = "no response",
    [UsbPdRequestResultDiscarded] = "discarded",
};

static const char* const usbc_policy_capabilities_kind_names[] = {
    [UsbcPolicyCapabilitiesSource] = "source",
    [UsbcPolicyCapabilitiesSink] = "sink",
    [UsbcPolicyCapabilitiesOffer] = "offer",
};

static const char* usbc_policy_get_power_role_name(UsbPdPowerRole power_role) {
    return (power_role == UsbPdPowerRoleSource) ? "source" : "sink";
}

static const char* usbc_policy_get_data_role_name(UsbPdDataRole data_role) {
    return (data_role == UsbPdDataRoleDfp) ? "DFP" : "UFP";
}

static uint32_t usbc_policy_get_contract_voltage_mv(const UsbPdContract* contract) {
    if(usb_pd_pdo_is_pps(contract->pdo)) {
        return usb_pd_rdo_get_pps_voltage_mv(contract->rdo);
    }
    return usb_pd_pdo_get_max_voltage_mv(contract->pdo);
}

static uint32_t usbc_policy_get_board_current_ma(uint32_t voltage_mv) {
    return (USBC_POLICY_BOARD_POWER_MW * 1000 + voltage_mv - 1) / voltage_mv;
}

static uint32_t usbc_policy_round_up_current_ma(uint32_t current_ma) {
    return (current_ma + USBC_POLICY_CURRENT_STEP_MA - 1) / USBC_POLICY_CURRENT_STEP_MA *
           USBC_POLICY_CURRENT_STEP_MA;
}

static bool usbc_policy_is_fixed_spr(uint32_t pdo) {
    return (usb_pd_pdo_get_type(pdo) == UsbPdPdoTypeFixed) &&
           (usb_pd_pdo_get_max_voltage_mv(pdo) <= USBC_POLICY_MAX_VOLTAGE_MV);
}

static uint32_t usbc_policy_get_max_power_mw(const UsbPdCapabilities* capabilities) {
    uint32_t max_power_mw = 0;

    for(uint8_t i = 0; i < capabilities->count; i++) {
        const uint32_t pdo = capabilities->objects[i];
        if(!usbc_policy_is_fixed_spr(pdo)) {
            continue;
        }
        const uint32_t current_ma =
            MIN(usb_pd_pdo_get_max_current_ma(pdo), (uint32_t)USBC_POLICY_MAX_CURRENT_MA);
        const uint32_t power_mw = usb_pd_pdo_get_max_voltage_mv(pdo) * current_ma / 1000;
        max_power_mw = MAX(max_power_mw, power_mw);
    }
    return max_power_mw;
}

static uint8_t
    usbc_policy_find_fixed_position(const UsbPdCapabilities* capabilities, uint32_t voltage_mv) {
    for(uint8_t i = 0; i < capabilities->count; i++) {
        const uint32_t pdo = capabilities->objects[i];
        if(usbc_policy_is_fixed_spr(pdo) && (usb_pd_pdo_get_max_voltage_mv(pdo) == voltage_mv)) {
            return i + 1;
        }
    }
    return 0;
}

static bool
    usbc_policy_is_capabilities_equal(const UsbPdCapabilities* a, const UsbPdCapabilities* b) {
    return (a->count == b->count) &&
           (memcmp(a->objects, b->objects, a->count * sizeof(a->objects[0])) == 0);
}

static bool usbc_policy_is_phone_sink(const UsbcPolicy* policy) {
    const UsbcPolicyPort* phone = &policy->ports[UsbcPortPhone];
    return phone->attached && (phone->power_role == UsbPdPowerRoleSource);
}

static bool usbc_policy_is_charger_pd(const UsbcPolicy* policy) {
    const UsbcPolicyPort* charger = &policy->ports[UsbcPortCharger];
    return charger->attached && (charger->power_role == UsbPdPowerRoleSink) &&
           (charger->contract.rdo != 0);
}

static bool usbc_policy_is_charger_ready(const UsbcPolicy* policy) {
    const UsbcPolicyPort* charger = &policy->ports[UsbcPortCharger];
    return usbc_policy_is_charger_pd(policy) ||
           (charger->attached && (charger->power_role == UsbPdPowerRoleSink) &&
            charger->partner_not_pd);
}

static uint32_t usbc_policy_get_charger_voltage_mv(const UsbcPolicy* policy) {
    if(usbc_policy_is_charger_pd(policy)) {
        return usbc_policy_get_contract_voltage_mv(&policy->ports[UsbcPortCharger].contract);
    }
    return USBC_POLICY_SAFE5V_MV;
}

static bool usbc_policy_is_charger_safe5v(const UsbcPolicy* policy) {
    return usbc_policy_is_charger_ready(policy) &&
           (usbc_policy_get_charger_voltage_mv(policy) == USBC_POLICY_SAFE5V_MV);
}

static void usbc_policy_get_charger_capabilities(
    const UsbcPolicy* policy,
    UsbPdCapabilities* capabilities) {
    const UsbcPolicyPort* charger = &policy->ports[UsbcPortCharger];

    if(charger->partner_not_pd) {
        const uint32_t pdo = usb_pd_pdo_make_fixed(
            USBC_POLICY_SAFE5V_MV, usbc_policy_rp_currents_ma[charger->partner_rp_current]);
        capabilities->objects[0] = usb_pd_pdo_set_unconstrained_power(pdo, true);
        capabilities->count = 1;
        return;
    }
    *capabilities = charger->partner_source_capabilities;
}

static bool usbc_policy_is_charger_power_sufficient(const UsbcPolicy* policy) {
    if(!usbc_policy_is_charger_pd(policy)) {
        return usbc_policy_is_charger_ready(policy);
    }
    const UsbPdContract* contract = &policy->ports[UsbcPortCharger].contract;
    const uint32_t voltage_mv = usbc_policy_get_contract_voltage_mv(contract);
    const uint32_t needed_ma =
        usbc_policy_get_board_current_ma(voltage_mv) + policy->phone.current_ma;
    return (voltage_mv == policy->phone.voltage_mv) &&
           (usb_pd_rdo_get_operating_current_ma(contract->rdo, contract->pdo) >= needed_ma);
}

static void usbc_policy_build_phone_offer(const UsbcPolicy* policy, UsbPdCapabilities* offer) {
    UsbPdCapabilities charger_capabilities;

    usbc_policy_get_charger_capabilities(policy, &charger_capabilities);
    offer->count = 0;
    for(uint8_t i = 0; i < charger_capabilities.count; i++) {
        const uint32_t pdo = charger_capabilities.objects[i];
        if(!usbc_policy_is_fixed_spr(pdo)) {
            continue;
        }
        const uint32_t voltage_mv = usb_pd_pdo_get_max_voltage_mv(pdo);
        const uint32_t charger_current_ma =
            MIN(usb_pd_pdo_get_max_current_ma(pdo), (uint32_t)USBC_POLICY_MAX_CURRENT_MA);
        const uint32_t board_current_ma = usbc_policy_get_board_current_ma(voltage_mv);
        const uint32_t phone_current_ma =
            (charger_current_ma > board_current_ma) ? (charger_current_ma - board_current_ma) : 0;
        if(offer->count == 0) {
            if(voltage_mv != USBC_POLICY_SAFE5V_MV) {
                break;
            }
            uint32_t first_pdo = usb_pd_pdo_make_fixed(voltage_mv, phone_current_ma);
            first_pdo = usb_pd_pdo_set_dual_role_power(first_pdo, true);
            first_pdo = usb_pd_pdo_set_unconstrained_power(
                first_pdo, usb_pd_pdo_is_unconstrained_power(pdo));
            first_pdo = usb_pd_pdo_set_usb_communications_capable(first_pdo, true);
            first_pdo = usb_pd_pdo_set_dual_role_data(first_pdo, true);
            offer->objects[offer->count++] = first_pdo;
        } else if(phone_current_ma > 0) {
            offer->objects[offer->count++] = usb_pd_pdo_make_fixed(voltage_mv, phone_current_ma);
        }
    }
}

static void usbc_policy_get_pass_through_sink_capabilities(
    const UsbcPolicy* policy,
    UsbPdCapabilities* capabilities) {
    const UsbPdCapabilities* phone_sink = &policy->ports[UsbcPortPhone].partner_sink_capabilities;

    capabilities->count = 0;
    for(uint8_t i = 0; i < phone_sink->count; i++) {
        const uint32_t pdo = phone_sink->objects[i];
        if(!usbc_policy_is_fixed_spr(pdo)) {
            continue;
        }
        const uint32_t voltage_mv = usb_pd_pdo_get_max_voltage_mv(pdo);
        const uint32_t current_ma =
            MIN(usb_pd_pdo_get_max_current_ma(pdo) + usbc_policy_get_board_current_ma(voltage_mv),
                (uint32_t)USBC_POLICY_MAX_CURRENT_MA);
        capabilities->objects[capabilities->count++] =
            usb_pd_pdo_make_fixed(voltage_mv, current_ma);
    }
    if((capabilities->count == 0) ||
       (usb_pd_pdo_get_max_voltage_mv(capabilities->objects[0]) != USBC_POLICY_SAFE5V_MV)) {
        capabilities->count = 0;
        for(size_t i = 0; i < COUNT_OF(usbc_policy_pass_through_voltages_mv); i++) {
            capabilities->objects[capabilities->count++] = usb_pd_pdo_make_fixed(
                usbc_policy_pass_through_voltages_mv[i], USBC_POLICY_MAX_CURRENT_MA);
        }
    }
    capabilities->objects[0] =
        usb_pd_pdo_set_higher_capability(capabilities->objects[0], capabilities->count > 1);
}

static void usbc_policy_get_board_sink_capabilities(
    const UsbcPolicy* policy,
    UsbPdCapabilities* capabilities) {
    uint32_t pdo = usb_pd_pdo_make_fixed(
        USBC_POLICY_SAFE5V_MV, usbc_policy_get_board_current_ma(USBC_POLICY_SAFE5V_MV));
    pdo = usb_pd_pdo_set_dual_role_power(pdo, true);
    pdo = usb_pd_pdo_set_unconstrained_power(pdo, usbc_policy_is_charger_ready(policy));
    pdo = usb_pd_pdo_set_usb_communications_capable(pdo, true);
    pdo = usb_pd_pdo_set_dual_role_data(pdo, true);
    capabilities->objects[0] = pdo;
    capabilities->count = 1;
}

static void usbc_policy_get_port_sink_capabilities(
    const UsbcPolicy* policy,
    UsbcPort port,
    UsbPdCapabilities* capabilities) {
    if(port == UsbcPortCharger) {
        usbc_policy_get_pass_through_sink_capabilities(policy, capabilities);
    } else {
        usbc_policy_get_board_sink_capabilities(policy, capabilities);
    }
}

static uint32_t usbc_policy_make_board_rdo(const UsbPdCapabilities* capabilities) {
    const uint32_t pdo = capabilities->objects[0];
    const uint32_t needed_ma = usbc_policy_get_board_current_ma(USBC_POLICY_SAFE5V_MV);
    const uint32_t max_current_ma = usb_pd_pdo_get_max_current_ma(pdo);
    const uint32_t rdo = usb_pd_rdo_make(1, pdo, MIN(needed_ma, max_current_ma), 0);
    return usb_pd_rdo_set_capability_mismatch(rdo, max_current_ma < needed_ma);
}

static uint32_t
    usbc_policy_make_charger_rdo(const UsbcPolicy* policy, const UsbPdCapabilities* capabilities) {
    const bool phone_sink = usbc_policy_is_phone_sink(policy);
    uint32_t voltage_mv = phone_sink ? policy->phone.voltage_mv : USBC_POLICY_SAFE5V_MV;
    uint8_t position = usbc_policy_find_fixed_position(capabilities, voltage_mv);

    if(position == 0) {
        voltage_mv = USBC_POLICY_SAFE5V_MV;
        position = 1;
    }
    const uint32_t pdo = capabilities->objects[position - 1];
    const uint32_t max_current_ma =
        MIN(usb_pd_pdo_get_max_current_ma(pdo), (uint32_t)USBC_POLICY_MAX_CURRENT_MA);
    uint32_t current_ma =
        MIN(usbc_policy_round_up_current_ma(usbc_policy_get_board_current_ma(voltage_mv)),
            max_current_ma);
    uint32_t desired_mw = USBC_POLICY_BOARD_POWER_MW;
    if(phone_sink) {
        current_ma = max_current_ma;
        desired_mw +=
            usbc_policy_get_max_power_mw(&policy->ports[UsbcPortPhone].partner_sink_capabilities);
    }
    const uint32_t rdo = usb_pd_rdo_make(position, pdo, current_ma, 0);
    return usb_pd_rdo_set_capability_mismatch(
        rdo, desired_mw > usbc_policy_get_max_power_mw(capabilities));
}

static void usbc_policy_reset_phone(UsbcPolicy* policy) {
    policy->status_changed = true;
    memset(&policy->phone.offer, 0, sizeof(UsbPdCapabilities));
    policy->phone.voltage_mv = USBC_POLICY_SAFE5V_MV;
    policy->phone.current_ma = 0;
    policy->phone.contract_pending = false;
    policy->phone.power_pending = false;
    policy->phone.source_on_pending = false;
    policy->phone.power_role_swap_refused = false;
    policy->pending_actions &=
        ~(UsbcPolicyActionPhoneSourceCapabilities | UsbcPolicyActionPhoneDataRoleSwap |
          UsbcPolicyActionPhoneSinkCapabilities | UsbcPolicyActionPhoneHardReset |
          UsbcPolicyActionPhonePowerRoleSwap);
}

static void usbc_policy_set_phone_mode(UsbcPolicy* policy, UsbPdPortMode mode) {
    FURI_LOG_I(TAG, "Phone port mode %s", (mode == UsbPdPortModeSource) ? "source" : "sink");
    policy->status_changed = true;
    policy->port_modes[UsbcPortPhone] = mode;
    usb_pd_set_mode(UsbcPortPhone, mode);
}

static void usbc_policy_update_phone_mode(UsbcPolicy* policy) {
    const UsbcPolicyPort* phone = &policy->ports[UsbcPortPhone];
    const bool charger_ready = usbc_policy_is_charger_ready(policy);

    if(policy->port_modes[UsbcPortPhone] == UsbPdPortModeDisabled) {
        return;
    }
    if(phone->attached) {
        if((phone->power_role == UsbPdPowerRoleSink) && charger_ready) {
            policy->pending_actions |= UsbcPolicyActionPhonePowerRoleSwap;
        } else if((phone->power_role == UsbPdPowerRoleSource) && !charger_ready) {
            usbc_policy_set_phone_mode(policy, UsbPdPortModeSink);
        }
        return;
    }
    const UsbPdPortMode mode = charger_ready ? UsbPdPortModeSource : UsbPdPortModeSink;
    if(mode != policy->port_modes[UsbcPortPhone]) {
        usbc_policy_set_phone_mode(policy, mode);
    }
}

static void usbc_policy_update_phone_power(UsbcPolicy* policy) {
    if(policy->phone.source_on_pending && usbc_policy_is_charger_safe5v(policy)) {
        policy->phone.source_on_pending = false;
        usbc_power_set(policy->power, UsbcPortPhone, UsbPdPowerSourceOn);
    }
    if(policy->phone.power_pending && usbc_policy_is_charger_power_sufficient(policy)) {
        policy->phone.power_pending = false;
        policy->phone.contract_pending = false;
        usbc_vbus_set_contract_voltage(policy->vbus, UsbcPortPhone, policy->phone.voltage_mv);
        usb_pd_power_ready(UsbcPortPhone);
    }
}

static bool usbc_policy_is_phone_contract_stable(const UsbcPolicy* policy) {
    return usbc_policy_is_phone_sink(policy) && !policy->phone.contract_pending &&
           (policy->ports[UsbcPortPhone].contract.rdo != 0);
}

static void usbc_policy_check_phone_offer(UsbcPolicy* policy) {
    UsbPdCapabilities offer;

    if(!usbc_policy_is_phone_contract_stable(policy)) {
        return;
    }
    usbc_policy_build_phone_offer(policy, &offer);
    if(!usbc_policy_is_capabilities_equal(&offer, &policy->phone.offer)) {
        policy->pending_actions |= UsbcPolicyActionPhoneSourceCapabilities;
    }
}

static void usbc_policy_check_phone_voltage(UsbcPolicy* policy) {
    if(!usbc_policy_is_phone_contract_stable(policy)) {
        return;
    }
    const uint32_t charger_voltage_mv = usbc_policy_get_charger_voltage_mv(policy);
    if(policy->phone.voltage_mv != charger_voltage_mv) {
        FURI_LOG_W(
            TAG,
            "Phone contract %lu mV, charger %lu mV",
            policy->phone.voltage_mv,
            charger_voltage_mv);
        policy->pending_actions |= UsbcPolicyActionPhoneHardReset;
    }
}

static void usbc_policy_log_pdo(
    const char* port_name,
    UsbcPolicyCapabilitiesKind kind,
    uint8_t position,
    uint32_t pdo) {
    const char* kind_name = usbc_policy_capabilities_kind_names[kind];
    const bool source = (kind != UsbcPolicyCapabilitiesSink);
    const uint32_t min_voltage_mv = usb_pd_pdo_get_min_voltage_mv(pdo);
    const uint32_t max_voltage_mv = usb_pd_pdo_get_max_voltage_mv(pdo);
    const uint32_t max_current_ma = usb_pd_pdo_get_max_current_ma(pdo);

    switch(usb_pd_pdo_get_type(pdo)) {
    case UsbPdPdoTypeFixed:
        FURI_LOG_I(
            TAG,
            "%s %s PDO%u 0x%08lX fixed %lu mV %lu mA%s%s%s%s%s",
            port_name,
            kind_name,
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
            kind_name,
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
            kind_name,
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
                kind_name,
                position,
                pdo,
                min_voltage_mv,
                max_voltage_mv,
                max_current_ma);
        } else {
            FURI_LOG_I(TAG, "%s %s PDO%u 0x%08lX augmented", port_name, kind_name, position, pdo);
        }
        break;
    }
}

static bool usbc_policy_log_capabilities_line(
    UsbcPolicyPort* port,
    const char* port_name,
    UsbcPolicyCapabilitiesKind kind,
    const UsbPdCapabilities* capabilities) {
    uint8_t* logged_count = &port->logged_pdo_counts[kind];

    if(*logged_count >= capabilities->count) {
        *logged_count = 0;
        return false;
    }
    usbc_policy_log_pdo(port_name, kind, *logged_count + 1, capabilities->objects[*logged_count]);
    (*logged_count)++;
    return true;
}

static void usbc_policy_add_capabilities_log(
    UsbcPolicyPort* port,
    UsbcPolicyLog log,
    UsbcPolicyCapabilitiesKind kind) {
    port->pending_logs |= log;
    port->logged_pdo_counts[kind] = 0;
}

static void
    usbc_policy_log_contract(const char* port_name, const UsbPdContract* contract, uint8_t count) {
    FURI_LOG_I(
        TAG,
        "%s contract: PDO%u of %u (%lu mV, up to %lu mA), requested %lu mA%s",
        port_name,
        usb_pd_rdo_get_object_position(contract->rdo),
        count,
        usbc_policy_get_contract_voltage_mv(contract),
        usb_pd_pdo_get_max_current_ma(contract->pdo),
        usb_pd_rdo_get_operating_current_ma(contract->rdo, contract->pdo),
        usb_pd_rdo_is_capability_mismatch(contract->rdo) ? ", capability mismatch" : "");
}

static void usbc_policy_log_phone_request(const UsbcPolicy* policy) {
    const uint32_t rdo = policy->phone.request_rdo;
    const uint8_t position = usb_pd_rdo_get_object_position(rdo);
    const uint32_t pdo = ((position > 0) && (position <= policy->phone.offer.count)) ?
                             policy->phone.offer.objects[position - 1] :
                             0;
    FURI_LOG_I(
        TAG,
        "Phone request: PDO%u of %u (%lu mV, up to %lu mA), %lu mA, %s",
        position,
        policy->phone.offer.count,
        usb_pd_pdo_get_max_voltage_mv(pdo),
        usb_pd_pdo_get_max_current_ma(pdo),
        usb_pd_rdo_get_operating_current_ma(rdo, pdo),
        policy->phone.request_accepted ? "accepted" : "rejected");
}

static bool usbc_policy_log_port_line(UsbcPolicy* policy, UsbcPort i) {
    UsbcPolicyPort* port = &policy->ports[i];
    const char* port_name = usbc_policy_port_names[i];

    if(port->pending_logs & UsbcPolicyLogSourceCapabilities) {
        if(usbc_policy_log_capabilities_line(
               port, port_name, UsbcPolicyCapabilitiesSource, &port->partner_source_capabilities)) {
            return true;
        }
        port->pending_logs &= ~UsbcPolicyLogSourceCapabilities;
    }
    if(port->pending_logs & UsbcPolicyLogSinkCapabilities) {
        if(usbc_policy_log_capabilities_line(
               port, port_name, UsbcPolicyCapabilitiesSink, &port->partner_sink_capabilities)) {
            return true;
        }
        port->pending_logs &= ~UsbcPolicyLogSinkCapabilities;
        if(port->partner_sink_capabilities.count > 0) {
            FURI_LOG_I(
                TAG,
                "%s sink fast role swap current %s",
                port_name,
                usbc_policy_fast_role_swap_current_names[usb_pd_pdo_get_fast_role_swap_current(
                    port->partner_sink_capabilities.objects[0])]);
            return true;
        }
    }
    if(port->pending_logs & UsbcPolicyLogPhoneOffer) {
        if(usbc_policy_log_capabilities_line(
               port, port_name, UsbcPolicyCapabilitiesOffer, &policy->phone.offer)) {
            return true;
        }
        port->pending_logs &= ~UsbcPolicyLogPhoneOffer;
    }
    if(port->pending_logs & UsbcPolicyLogPhoneRequest) {
        port->pending_logs &= ~UsbcPolicyLogPhoneRequest;
        usbc_policy_log_phone_request(policy);
        return true;
    }
    if(port->pending_logs & UsbcPolicyLogSinkPdp) {
        port->pending_logs &= ~UsbcPolicyLogSinkPdp;
        FURI_LOG_I(
            TAG,
            "%s sink PDP sent: minimum %u W, operational %u W, maximum %u W",
            port_name,
            port->sink_pdp.minimum_w,
            port->sink_pdp.operational_w,
            port->sink_pdp.maximum_w);
        return true;
    }
    if(port->pending_logs & UsbcPolicyLogContract) {
        port->pending_logs &= ~UsbcPolicyLogContract;
        const uint8_t count = (port->power_role == UsbPdPowerRoleSource) ?
                                  policy->phone.offer.count :
                                  port->partner_source_capabilities.count;
        usbc_policy_log_contract(port_name, &port->contract, count);
        return true;
    }
    return false;
}

static bool usbc_policy_log_line(UsbcPolicy* policy) {
    for(UsbcPort i = 0; i < UsbcPortCount; i++) {
        if(usbc_policy_log_port_line(policy, i)) {
            return true;
        }
    }
    return false;
}

static void usbc_policy_on_attached(UsbcPolicy* policy, UsbcPort port, const UsbPdAttach* attach) {
    UsbcPolicyPort* policy_port = &policy->ports[port];

    FURI_LOG_I(
        TAG,
        "%s attached as %s, CC%u, partner Rp %s",
        usbc_policy_port_names[port],
        usbc_policy_get_power_role_name(attach->power_role),
        attach->pin,
        usbc_policy_rp_current_names[attach->partner_rp_current]);
    policy_port->attached = true;
    policy_port->power_role = attach->power_role;
    policy_port->data_role = (attach->power_role == UsbPdPowerRoleSource) ? UsbPdDataRoleDfp :
                                                                            UsbPdDataRoleUfp;
    policy_port->partner_rp_current = attach->partner_rp_current;
    usbc_power_set_attached(policy->power, port, true);
}

static void usbc_policy_on_detached(UsbcPolicy* policy, UsbcPort port) {
    FURI_LOG_I(TAG, "%s detached", usbc_policy_port_names[port]);
    memset(&policy->ports[port], 0, sizeof(UsbcPolicyPort));
    usbc_vbus_set_contract_voltage(policy->vbus, port, USBC_POLICY_SAFE5V_MV);
    usbc_power_set_attached(policy->power, port, false);
    if(port == UsbcPortPhone) {
        usbc_policy_reset_phone(policy);
        policy->pending_actions |= UsbcPolicyActionChargerPower;
    } else {
        policy->phone.power_role_swap_refused = false;
        policy->pending_actions &=
            ~(UsbcPolicyActionChargerPower | UsbcPolicyActionChargerSourceCapabilities);
    }
    usbc_policy_update_phone_mode(policy);
}

static void
    usbc_policy_on_contract(UsbcPolicy* policy, UsbcPort port, const UsbPdContract* contract) {
    UsbcPolicyPort* policy_port = &policy->ports[port];
    const uint32_t voltage_mv = usbc_policy_get_contract_voltage_mv(contract);

    policy_port->contract = *contract;
    policy_port->pending_logs |= UsbcPolicyLogContract;
    usbc_vbus_set_contract_voltage(policy->vbus, port, voltage_mv);
    if(port == UsbcPortCharger) {
        if(usbc_policy_is_phone_sink(policy)) {
            usbc_vbus_set_contract_voltage(policy->vbus, UsbcPortPhone, voltage_mv);
        }
        usbc_policy_update_phone_power(policy);
        usbc_policy_check_phone_voltage(policy);
        usbc_policy_check_phone_offer(policy);
        usbc_policy_update_phone_mode(policy);
    } else if(policy_port->power_role == UsbPdPowerRoleSource) {
        if(policy_port->data_role == UsbPdDataRoleDfp) {
            policy->pending_actions |= UsbcPolicyActionPhoneDataRoleSwap;
        }
        if(policy_port->partner_sink_capabilities.count == 0) {
            policy->pending_actions |= UsbcPolicyActionPhoneSinkCapabilities;
        }
    } else {
        usbc_policy_update_phone_mode(policy);
    }
}

static void
    usbc_policy_on_phone_power_role_swap_answered(UsbcPolicy* policy, UsbPdRequestResult result) {
    FURI_LOG_I(TAG, "Phone power role swap %s", usbc_policy_request_result_names[result]);
    switch(result) {
    case UsbPdRequestResultAccepted:
        break;
    case UsbPdRequestResultRejected:
    case UsbPdRequestResultNotSupported:
        policy->phone.power_role_swap_refused = true;
        break;
    case UsbPdRequestResultWait:
        policy->phone.power_role_swap_retry_tick =
            furi_get_tick() + furi_ms_to_ticks(USBC_POLICY_AMS_REPEAT_MS);
        policy->pending_actions |= UsbcPolicyActionPhonePowerRoleSwap;
        break;
    default:
        policy->pending_actions |= UsbcPolicyActionPhonePowerRoleSwap;
        break;
    }
}

static void usbc_policy_on_request_answered(
    UsbcPolicy* policy,
    UsbcPort port,
    const UsbPdRequestAnswer* answer) {
    if((port == UsbcPortPhone) && (answer->request == UsbPdRequestPowerRoleSwap)) {
        usbc_policy_on_phone_power_role_swap_answered(policy, answer->result);
        return;
    }
    if((port != UsbcPortCharger) || (answer->request != UsbPdRequestPower)) {
        return;
    }
    switch(answer->result) {
    case UsbPdRequestResultAccepted:
        break;
    case UsbPdRequestResultWait:
    case UsbPdRequestResultDiscarded:
        policy->pending_actions |= UsbcPolicyActionChargerPower;
        break;
    default:
        FURI_LOG_I(TAG, "Charger request %s", usbc_policy_request_result_names[answer->result]);
        if(policy->phone.power_pending) {
            policy->phone.power_pending = false;
            policy->phone.contract_pending = false;
            policy->pending_actions |= UsbcPolicyActionPhoneHardReset;
        }
        break;
    }
}

static void usbc_policy_on_event(void* context, uint8_t port, const UsbPdEvent* event) {
    furi_check(port < UsbcPortCount);
    UsbcPolicy* policy = context;
    UsbcPolicyPort* policy_port = &policy->ports[port];
    const char* port_name = usbc_policy_port_names[port];

    policy->status_changed = true;

    switch(event->type) {
    case UsbPdEventTypeAttached:
        usbc_policy_on_attached(policy, port, &event->attach);
        break;
    case UsbPdEventTypeDetached:
        usbc_policy_on_detached(policy, port);
        break;
    case UsbPdEventTypeSourceCapabilities:
        policy_port->partner_source_capabilities = *event->capabilities;
        usbc_policy_add_capabilities_log(
            policy_port, UsbcPolicyLogSourceCapabilities, UsbcPolicyCapabilitiesSource);
        break;
    case UsbPdEventTypeSinkCapabilities:
        policy_port->partner_sink_capabilities = *event->capabilities;
        usbc_policy_add_capabilities_log(
            policy_port, UsbcPolicyLogSinkCapabilities, UsbcPolicyCapabilitiesSink);
        if((port == UsbcPortPhone) && usbc_policy_is_charger_pd(policy)) {
            policy->pending_actions |= UsbcPolicyActionChargerSourceCapabilities;
        }
        break;
    case UsbPdEventTypeContract:
        usbc_policy_on_contract(policy, port, &event->contract);
        break;
    case UsbPdEventTypeRequestAnswered:
        usbc_policy_on_request_answered(policy, port, &event->request_answer);
        break;
    case UsbPdEventTypeHardReset:
        FURI_LOG_I(
            TAG, "%s hard reset %s", port_name, event->hard_reset_received ? "received" : "sent");
        memset(&policy_port->contract, 0, sizeof(UsbPdContract));
        policy_port->pending_logs &= ~UsbcPolicyLogContract;
        policy_port->data_role = (policy_port->power_role == UsbPdPowerRoleSource) ?
                                     UsbPdDataRoleDfp :
                                     UsbPdDataRoleUfp;
        usbc_vbus_set_contract_voltage(policy->vbus, port, USBC_POLICY_SAFE5V_MV);
        break;
    case UsbPdEventTypeSoftReset:
        FURI_LOG_I(TAG, "%s soft reset", port_name);
        break;
    case UsbPdEventTypePartnerNotPd:
        FURI_LOG_I(TAG, "%s partner is not PD capable", port_name);
        policy_port->partner_not_pd = true;
        if(port == UsbcPortCharger) {
            usbc_policy_check_phone_offer(policy);
            usbc_policy_update_phone_mode(policy);
        }
        break;
    case UsbPdEventTypePowerRoleSwapped:
    case UsbPdEventTypeFastRoleSwapped:
        FURI_LOG_I(
            TAG, "%s power role %s", port_name, usbc_policy_get_power_role_name(event->power_role));
        policy_port->power_role = event->power_role;
        memset(&policy_port->contract, 0, sizeof(UsbPdContract));
        policy_port->pending_logs &= ~UsbcPolicyLogContract;
        break;
    case UsbPdEventTypeDataRoleSwapped:
        FURI_LOG_I(
            TAG, "%s data role %s", port_name, usbc_policy_get_data_role_name(event->data_role));
        policy_port->data_role = event->data_role;
        break;
    default:
        break;
    }
}

static void usbc_policy_get_source_capabilities(
    void* context,
    uint8_t port,
    UsbPdCapabilities* capabilities) {
    UsbcPolicy* policy = context;

    capabilities->count = 0;
    if((port != UsbcPortPhone) || !usbc_policy_is_charger_ready(policy)) {
        return;
    }
    usbc_policy_build_phone_offer(policy, &policy->phone.offer);
    policy->status_changed = true;
    usbc_policy_add_capabilities_log(
        &policy->ports[UsbcPortPhone], UsbcPolicyLogPhoneOffer, UsbcPolicyCapabilitiesOffer);
    *capabilities = policy->phone.offer;
}

static void usbc_policy_get_sink_capabilities(
    void* context,
    uint8_t port,
    UsbPdCapabilities* capabilities) {
    furi_check(port < UsbcPortCount);
    usbc_policy_get_port_sink_capabilities(context, port, capabilities);
}

static void usbc_policy_get_sink_pdp(void* context, uint8_t port, UsbPdSinkPdp* pdp) {
    furi_check(port < UsbcPortCount);
    UsbcPolicy* policy = context;
    UsbcPolicyPort* policy_port = &policy->ports[port];
    UsbPdCapabilities capabilities;

    usbc_policy_get_port_sink_capabilities(policy, port, &capabilities);
    const uint32_t needed_mw = usbc_policy_get_max_power_mw(&capabilities);
    const uint32_t needed_w =
        MAX((needed_mw + USBC_POLICY_MW_PER_W - 1) / USBC_POLICY_MW_PER_W,
            (uint32_t)USBC_POLICY_BOARD_PDP_W);
    pdp->operational_w = needed_w;
    pdp->maximum_w = needed_w;
    pdp->minimum_w = needed_w;
    if(port == UsbcPortCharger) {
        const uint32_t offered_w =
            usbc_policy_get_max_power_mw(&policy_port->partner_source_capabilities) /
            USBC_POLICY_MW_PER_W;
        pdp->minimum_w = MAX(MIN(needed_w, offered_w), (uint32_t)USBC_POLICY_BOARD_PDP_W);
    }
    policy_port->sink_pdp = *pdp;
    policy_port->pending_logs |= UsbcPolicyLogSinkPdp;
}

static bool usbc_policy_get_pps_status(void* context, uint8_t port, UsbPdPpsStatus* status) {
    UNUSED(context);
    UNUSED(port);
    UNUSED(status);
    return false;
}

static UsbPdAnswer usbc_policy_evaluate_request(void* context, uint8_t port, uint32_t rdo) {
    UsbcPolicy* policy = context;
    const UsbPdCapabilities* offer = &policy->phone.offer;
    const uint8_t position = usb_pd_rdo_get_object_position(rdo);

    if((port != UsbcPortPhone) || !usbc_policy_is_phone_sink(policy)) {
        return UsbPdAnswerReject;
    }
    policy->phone.request_rdo = rdo;
    policy->phone.request_accepted = false;
    policy->ports[UsbcPortPhone].pending_logs |= UsbcPolicyLogPhoneRequest;
    if((position == 0) || (position > offer->count)) {
        return UsbPdAnswerReject;
    }
    const uint32_t pdo = offer->objects[position - 1];
    const uint32_t current_ma = usb_pd_rdo_get_operating_current_ma(rdo, pdo);
    if(current_ma > usb_pd_pdo_get_max_current_ma(pdo)) {
        return UsbPdAnswerReject;
    }
    policy->phone.request_accepted = true;
    policy->phone.contract_pending = true;
    policy->phone.voltage_mv = usb_pd_pdo_get_max_voltage_mv(pdo);
    policy->phone.current_ma = current_ma;
    return UsbPdAnswerAccept;
}

static uint32_t usbc_policy_select_capability(
    void* context,
    uint8_t port,
    const UsbPdCapabilities* source_capabilities) {
    UsbcPolicy* policy = context;

    if(port == UsbcPortCharger) {
        usbc_policy_check_phone_offer(policy);
        return usbc_policy_make_charger_rdo(policy, source_capabilities);
    }
    return usbc_policy_make_board_rdo(source_capabilities);
}

static UsbPdAnswer usbc_policy_evaluate_power_role_swap(void* context, uint8_t port) {
    UsbcPolicy* policy = context;

    if((port != UsbcPortPhone) || (policy->ports[port].power_role != UsbPdPowerRoleSink)) {
        return UsbPdAnswerReject;
    }
    if(usbc_policy_is_charger_safe5v(policy)) {
        return UsbPdAnswerAccept;
    }
    if(policy->ports[UsbcPortCharger].attached) {
        return UsbPdAnswerWait;
    }
    return UsbPdAnswerReject;
}

static UsbPdAnswer usbc_policy_evaluate_data_role_swap(void* context, uint8_t port) {
    UsbcPolicy* policy = context;

    if((port == UsbcPortPhone) && (policy->ports[port].data_role == UsbPdDataRoleDfp)) {
        return UsbPdAnswerAccept;
    }
    return UsbPdAnswerReject;
}

static UsbPdAnswer usbc_policy_evaluate_vconn_swap(void* context, uint8_t port) {
    UNUSED(context);
    UNUSED(port);
    return UsbPdAnswerReject;
}

static void usbc_policy_set_phone_power(UsbcPolicy* policy, UsbPdPower power) {
    switch(power) {
    case UsbPdPowerSourceOn:
        policy->phone.source_on_pending = true;
        usbc_policy_update_phone_power(policy);
        if(policy->phone.source_on_pending) {
            policy->pending_actions |= UsbcPolicyActionChargerPower;
        }
        break;
    case UsbPdPowerSourceNewContract:
        usbc_power_set(policy->power, UsbcPortPhone, power);
        policy->phone.power_pending = true;
        usbc_policy_update_phone_power(policy);
        if(policy->phone.power_pending) {
            policy->pending_actions |= UsbcPolicyActionChargerPower;
        }
        break;
    case UsbPdPowerSourceOff:
        usbc_power_set(policy->power, UsbcPortPhone, power);
        usbc_policy_reset_phone(policy);
        usbc_vbus_set_contract_voltage(policy->vbus, UsbcPortPhone, USBC_POLICY_SAFE5V_MV);
        policy->pending_actions |= UsbcPolicyActionChargerPower;
        break;
    default:
        usbc_power_set(policy->power, UsbcPortPhone, power);
        break;
    }
}

static void usbc_policy_set_power(void* context, uint8_t port, UsbPdPower power) {
    furi_check(port < UsbcPortCount);
    UsbcPolicy* policy = context;

    FURI_LOG_D(TAG, "%s power %s", usbc_policy_port_names[port], usbc_policy_power_names[power]);
    if(power == UsbPdPowerSinkStandby) {
        usbc_vbus_set_contract_voltage(policy->vbus, port, USBC_POLICY_SAFE5V_MV);
    }
    if(port == UsbcPortPhone) {
        usbc_policy_set_phone_power(policy, power);
    } else {
        usbc_power_set(policy->power, port, power);
    }
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
    .get_sink_pdp = usbc_policy_get_sink_pdp,
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

static bool usbc_policy_run_charger_power(UsbcPolicy* policy) {
    UsbcPolicyPort* charger = &policy->ports[UsbcPortCharger];

    if(!usbc_policy_is_charger_pd(policy)) {
        return true;
    }
    const uint32_t rdo =
        usbc_policy_make_charger_rdo(policy, &charger->partner_source_capabilities);
    if(rdo == charger->contract.rdo) {
        usbc_policy_update_phone_power(policy);
        return true;
    }
    return usb_pd_request_power(UsbcPortCharger, rdo);
}

static bool usbc_policy_run_phone_power_role_swap(UsbcPolicy* policy) {
    const UsbcPolicyPort* phone = &policy->ports[UsbcPortPhone];

    if(!phone->attached || (phone->power_role != UsbPdPowerRoleSink) ||
       (phone->contract.rdo == 0) || policy->phone.power_role_swap_refused ||
       !usbc_policy_is_charger_safe5v(policy)) {
        return true;
    }
    if((int32_t)(furi_get_tick() - policy->phone.power_role_swap_retry_tick) < 0) {
        return false;
    }
    return usb_pd_request(UsbcPortPhone, UsbPdRequestPowerRoleSwap);
}

static bool usbc_policy_run_action(UsbcPolicy* policy, UsbcPolicyAction action) {
    switch(action) {
    case UsbcPolicyActionChargerPower:
        return usbc_policy_run_charger_power(policy);
    case UsbcPolicyActionChargerSourceCapabilities:
        return !usbc_policy_is_charger_pd(policy) ||
               usb_pd_request(UsbcPortCharger, UsbPdRequestSourceCapabilities);
    case UsbcPolicyActionPhoneSourceCapabilities:
        return !usbc_policy_is_phone_sink(policy) ||
               usb_pd_send_source_capabilities(UsbcPortPhone);
    case UsbcPolicyActionPhoneDataRoleSwap:
        return !usbc_policy_is_phone_sink(policy) ||
               (policy->ports[UsbcPortPhone].data_role != UsbPdDataRoleDfp) ||
               usb_pd_request(UsbcPortPhone, UsbPdRequestDataRoleSwap);
    case UsbcPolicyActionPhoneSinkCapabilities:
        return !usbc_policy_is_phone_sink(policy) ||
               usb_pd_request(UsbcPortPhone, UsbPdRequestSinkCapabilities);
    case UsbcPolicyActionPhoneHardReset:
        return !policy->ports[UsbcPortPhone].attached || usb_pd_send_hard_reset(UsbcPortPhone);
    case UsbcPolicyActionPhonePowerRoleSwap:
        return usbc_policy_run_phone_power_role_swap(policy);
    }
    return true;
}

static void usbc_policy_run_actions(UsbcPolicy* policy) {
    static const UsbcPolicyAction actions[] = {
        UsbcPolicyActionPhoneHardReset,
        UsbcPolicyActionChargerPower,
        UsbcPolicyActionPhonePowerRoleSwap,
        UsbcPolicyActionPhoneDataRoleSwap,
        UsbcPolicyActionPhoneSinkCapabilities,
        UsbcPolicyActionChargerSourceCapabilities,
        UsbcPolicyActionPhoneSourceCapabilities,
    };

    for(size_t i = 0; i < COUNT_OF(actions); i++) {
        if((policy->pending_actions & actions[i]) && usbc_policy_run_action(policy, actions[i])) {
            policy->pending_actions &= ~actions[i];
        }
    }
}

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
    usbc_policy_reset_phone(policy);
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
        policy->port_modes[i] = config.mode;
        furi_check(usb_pd_port_init(i, &config));
    }
    usbc_power_start(policy->power);
}

void usbc_policy_free(UsbcPolicy* policy) {
    free(policy);
}

uint32_t usbc_policy_process(UsbcPolicy* policy) {
    usbc_policy_run_actions(policy);
    uint32_t next_ms = usb_pd_process();
    if(policy->pending_actions) {
        next_ms = MIN(next_ms, (uint32_t)USBC_POLICY_RETRY_MS);
    }
    if((next_ms != 0) && usbc_policy_log_line(policy)) {
        next_ms = 0;
    }
    return next_ms;
}

static const UsbPdCapabilities*
    usbc_policy_get_port_offer(const UsbcPolicy* policy, UsbcPort port) {
    static const UsbPdCapabilities empty_offer = {0};

    if(port == UsbcPortPhone) {
        return &policy->phone.offer;
    }
    return &empty_offer;
}

bool usbc_policy_take_status_change(UsbcPolicy* policy) {
    const bool status_changed = policy->status_changed;
    policy->status_changed = false;
    return status_changed;
}

void usbc_policy_fill_status(UsbcPolicy* policy, UsbcStatus* status) {
    for(UsbcPort i = 0; i < UsbcPortCount; i++) {
        const UsbcPolicyPort* policy_port = &policy->ports[i];
        UsbcPortStatus* port_status = &status->ports[i];
        UsbPdPortStatus pd_status;
        usb_pd_get_port_status(i, &pd_status);

        port_status->mode = policy->port_modes[i];
        port_status->attached = policy_port->attached;
        port_status->partner_not_pd = policy_port->partner_not_pd;
        port_status->power_role = policy_port->power_role;
        port_status->data_role = policy_port->data_role;
        port_status->cc_pin = pd_status.pin;
        port_status->partner_rp_current = policy_port->partner_rp_current;
        port_status->spec_revision = pd_status.spec_revision;
        port_status->vbus_level = usbc_vbus_get_level(policy->vbus, i);
        port_status->contract = policy_port->contract;
        port_status->partner_source_capabilities = policy_port->partner_source_capabilities;
        port_status->partner_sink_capabilities = policy_port->partner_sink_capabilities;
        port_status->offer = *usbc_policy_get_port_offer(policy, i);
        port_status->tcpp03_faulted = usbc_power_is_faulted(policy->power, i);
    }
}
