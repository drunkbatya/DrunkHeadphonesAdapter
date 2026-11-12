#include "policy_engine/policy_engine_i.h"

static bool requests_start(
    PolicyEngine* pe,
    PolicyEngineState dpm_request,
    UsbPdRequest request,
    bool answerable) {
    pe->dpm_request_id = request;
    pe->dpm_request_answerable = answerable;
    pe->dpm_request = dpm_request;
    policy_engine_wakeup(pe->port);
    return true;
}

bool policy_engine_request(uint8_t port, UsbPdRequest request) {
    PolicyEngine* pe = &policy_engines[port];
    bool explicit_contract;
    bool dual_role;

    if(policy_engine_is_busy(pe)) {
        return false;
    }
    explicit_contract = (pe->power == PolicyEnginePowerExplicitContract);
    dual_role = policy_engine_is_dual_role(pe);
    if(!explicit_contract) {
        return false;
    }

    switch(request) {
    case UsbPdRequestPowerRoleSwap:
        if(!dual_role || !explicit_contract) {
            return false;
        }
        return requests_start(pe, PolicyEngineStatePowerRoleSwapSend, request, true);

    case UsbPdRequestDataRoleSwap:
        if(!pe->config.data_role_swap || !explicit_contract) {
            return false;
        }
        return requests_start(pe, PolicyEngineStateDataRoleSwapSend, request, true);

    case UsbPdRequestVconnSwap:
        if(!pe->config.vconn_swap) {
            return false;
        }
        return requests_start(pe, PolicyEngineStateVconnSwapSend, request, true);

    case UsbPdRequestSourceCapabilities:
        if(((pe->power_role != UsbPdPowerRoleSink) && !dual_role) || !explicit_contract) {
            return false;
        }
        return requests_start(pe, PolicyEngineStateGetSourceCap, request, true);

    case UsbPdRequestSinkCapabilities:
        if(((pe->power_role != UsbPdPowerRoleSource) && !dual_role) || !explicit_contract) {
            return false;
        }
        return requests_start(pe, PolicyEngineStateGetSinkCap, request, true);

    case UsbPdRequestPpsStatus:
        if((pe->power_role != UsbPdPowerRoleSink) || !explicit_contract ||
           (pe->spec_revision != UsbPdSpecRevision3)) {
            return false;
        }
        return requests_start(pe, PolicyEngineStateSinkGetPpsStatus, request, true);

    case UsbPdRequestSoftReset:
        return requests_start(pe, PolicyEngineStateSendSoftReset, request, true);

    default:
        return false;
    }
}

bool policy_engine_request_power(uint8_t port, uint32_t rdo) {
    PolicyEngine* pe = &policy_engines[port];

    if(policy_engine_is_busy(pe)) {
        return false;
    }
    if((pe->power_role != UsbPdPowerRoleSink) ||
       (pe->power != PolicyEnginePowerExplicitContract)) {
        return false;
    }
    pe->requested_rdo = rdo;
    return requests_start(pe, PolicyEngineStateSinkSelectCapability, UsbPdRequestPower, true);
}

bool policy_engine_send_source_capabilities(uint8_t port) {
    PolicyEngine* pe = &policy_engines[port];

    if(policy_engine_is_busy(pe) || (pe->power_role != UsbPdPowerRoleSource)) {
        return false;
    }
    if((pe->state == PolicyEngineStateReady) || (pe->state == PolicyEngineStateReadyIdle)) {
        return requests_start(
            pe, PolicyEngineStateSourceSendCapabilities, UsbPdRequestPower, false);
    }
    if(pe->state == PolicyEngineStateSourceWaitNewCapabilities) {
        pe->state = PolicyEngineStateSourceSendCapabilities;
        policy_engine_wakeup(port);
        return true;
    }
    return false;
}

bool policy_engine_send_hard_reset_request(uint8_t port) {
    PolicyEngine* pe = &policy_engines[port];

    if(!pe->connected) {
        return false;
    }
    policy_engine_drop_dpm_request(pe);
    return requests_start(pe, PolicyEngineStateHardReset, UsbPdRequestPower, false);
}
