#include "policy_engine/policy_engine_i.h"

void policy_engine_role_enter_startup(
    PolicyEngine* pe,
    UsbPdPowerRole power_role,
    UsbPdDataRole data_role,
    PolicyEngineState startup_state) {
    policy_engine_reset_session(pe);
    policy_engine_reset_counters(pe);
    protocol_layer_set_header(pe->port, power_role, data_role, pe->config.spec_revision);
    pe->data_role = data_role;
    pe->spec_revision = pe->config.spec_revision;
    protocol_layer_reset(pe->port);
    pe->state = startup_state;
}

void policy_engine_role_reject_wrong_data_role(PolicyEngine* pe, PolicyEngineRxEvent* event) {
    if((*event == PolicyEngineRxEventMessage) &&
       (pe->data_role == POLICY_ENGINE_HEADER_DATA_ROLE(pe->rx_header))) {
        policy_engine_consume_message(pe, event, PolicyEngineStateErrorRecovery);
    }
}

void policy_engine_role_take_hard_reset_request(PolicyEngine* pe, PolicyEngineRxEvent* event) {
    if(pe->hard_reset_pending) {
        pe->hard_reset_pending = false;
        pe->hard_reset_by_partner = true;
        policy_engine_consume_message(pe, event, PolicyEngineStateHardResetReceived);
    } else if(pe->dpm_request == PolicyEngineStateHardReset) {
        pe->dpm_request = PolicyEngineStateDisabled;
        policy_engine_consume_message(pe, event, PolicyEngineStateHardReset);
    }
}

uint32_t policy_engine_role_finish_step(
    PolicyEngine* pe,
    PolicyEngineRxEvent* event,
    uint32_t next_call_ms) {
    if((pe->flags.tx_discarded == 1U) && (pe->flags.dpm_request_ongoing == 1U)) {
        policy_engine_answer_request(pe, pe->active_request, UsbPdRequestResultDiscarded);
        pe->flags.dpm_request_ongoing = 0U;
    }
    if(*event == PolicyEngineRxEventNone) {
        if(pe->flags.tx_discarded == 1U) {
            pe->state = PolicyEngineStateReady;
            next_call_ms = POLICY_ENGINE_NEXT_CALL_SOON;
        }
    } else {
        if(policy_engine_resolve_ams_conflict(pe)) {
            policy_engine_consume_message(pe, event, pe->state);
        }
        next_call_ms = POLICY_ENGINE_NEXT_CALL_NOW;
    }
    pe->flags.tx_discarded = 0U;
    return next_call_ms;
}

uint32_t policy_engine_role_send_power_role_swap(
    PolicyEngine* pe,
    PolicyEngineState wait_response_state) {
    ProtocolLayerTransmitResult result;

    result = policy_engine_send_control_message(
        pe, UsbPdControlMessagePrSwap, wait_response_state, POLICY_ENGINE_MESSAGE_FIRST_OF_AMS);
    if(result != ProtocolLayerTransmitResultAcknowledged) {
        if(result != ProtocolLayerTransmitResultSinkTxBlocked) {
            policy_engine_answer_request(
                pe, UsbPdRequestPowerRoleSwap, UsbPdRequestResultNoResponse);
        }
        return POLICY_ENGINE_NEXT_CALL_DEFAULT;
    }
    pe->current_ams = PolicyEngineAmsPowerRoleSwap;
    return policy_engine_start_sender_response_timer(pe);
}

static uint32_t role_state_disabled(PolicyEngine* pe) {
    policy_engine_discard_requests(pe);
    policy_engine_set_swap_ongoing(pe, false);
    if(pe->hard_reset_in_progress) {
        policy_engine_set_hard_reset_in_progress(pe, false);
    }
    if(pe->flags.disabled_reported == 0U) {
        pe->flags.disabled_reported = 1U;
        policy_engine_notify_type(pe, UsbPdEventTypePartnerNotPd);
    }
    return POLICY_ENGINE_NEXT_CALL_IDLE;
}

static uint32_t role_state_error_recovery(PolicyEngine* pe) {
    policy_engine_discard_requests(pe);
    policy_engine_set_swap_ongoing(pe, false);
    type_c_connection_enter_error_recovery(pe->port);
    policy_engine_wakeup(pe->port);
    return POLICY_ENGINE_NEXT_CALL_IDLE;
}

static uint32_t role_state_send_reject(PolicyEngine* pe) {
    (void)policy_engine_send_control_message(
        pe, UsbPdControlMessageReject, PolicyEngineStateReady, POLICY_ENGINE_MESSAGE_WITHIN_AMS);
    return POLICY_ENGINE_NEXT_CALL_DEFAULT;
}

static void role_on_partner_source_capabilities(PolicyEngine* pe, PolicyEngineRxEvent* event) {
    UsbPdCapabilities partner_capabilities;
    UsbPdEvent notification;

    policy_engine_read_capabilities(pe, &partner_capabilities);
    policy_engine_consume_message(pe, event, PolicyEngineStateReady);
    memset(&notification, 0, sizeof(notification));
    notification.type = UsbPdEventTypeSourceCapabilities;
    notification.capabilities = &partner_capabilities;
    policy_engine_notify(pe, &notification);
}

static uint32_t
    role_state_get_source_cap_wait_response(PolicyEngine* pe, PolicyEngineRxEvent* event) {
    uint32_t next_call_ms = POLICY_ENGINE_NEXT_CALL_DEFAULT;

    if(*event == PolicyEngineRxEventMessage) {
        if(policy_engine_is_data_message(pe, UsbPdDataMessageSourceCapabilities) &&
           (pe->power_role == UsbPdPowerRoleSink)) {
            policy_engine_answer_request(
                pe, UsbPdRequestSourceCapabilities, UsbPdRequestResultAccepted);
            policy_engine_sink_accept_source_capabilities(pe, event);
            return POLICY_ENGINE_NEXT_CALL_NOW;
        }
        if(policy_engine_is_data_message(pe, UsbPdDataMessageSourceCapabilities)) {
            role_on_partner_source_capabilities(pe, event);
            policy_engine_answer_request(
                pe, UsbPdRequestSourceCapabilities, UsbPdRequestResultAccepted);
        } else {
            uint16_t control_message = policy_engine_get_control_message(pe);

            if((control_message == UsbPdControlMessageReject) ||
               (control_message == UsbPdControlMessageNotSupported)) {
                next_call_ms = POLICY_ENGINE_NEXT_CALL_NOW;
                policy_engine_consume_message(pe, event, PolicyEngineStateReady);
                policy_engine_answer_request(
                    pe,
                    UsbPdRequestSourceCapabilities,
                    (control_message == UsbPdControlMessageReject) ?
                        UsbPdRequestResultRejected :
                        UsbPdRequestResultNotSupported);
            }
        }
    }

    if(policy_engine_timer_is_expired(pe, &pe->state_timer)) {
        pe->state = PolicyEngineStateReady;
        policy_engine_answer_request(
            pe, UsbPdRequestSourceCapabilities, UsbPdRequestResultNoResponse);
        next_call_ms = POLICY_ENGINE_NEXT_CALL_NOW;
    }
    return next_call_ms;
}

bool policy_engine_role_run_shared_state(
    PolicyEngine* pe,
    PolicyEngineRxEvent* event,
    uint32_t* next_call_ms) {
    switch(pe->state) {
    case PolicyEngineStateDisabled:
        *next_call_ms = role_state_disabled(pe);
        return true;

    case PolicyEngineStateErrorRecovery:
        *next_call_ms = role_state_error_recovery(pe);
        return true;

    case PolicyEngineStateSendReject:
        *next_call_ms = role_state_send_reject(pe);
        return true;

    case PolicyEngineStateGetSourceCapWaitResponse:
        *next_call_ms = role_state_get_source_cap_wait_response(pe, event);
        return true;

    case PolicyEngineStateSendNotSupported:
    case PolicyEngineStateGetSinkCap:
    case PolicyEngineStateGetSinkCapWaitResponse:
    case PolicyEngineStateGiveSinkCap:
    case PolicyEngineStateBistCarrierMode:
    case PolicyEngineStateBistCarrierModeWaitExit:
    case PolicyEngineStateBistTestData:
    case PolicyEngineStateGiveRevision:
    case PolicyEngineStateGiveSinkCapExtended:
    case PolicyEngineStateGiveSourceInfo:
    case PolicyEngineStateDataRoleSwapEvaluate:
    case PolicyEngineStateDataRoleSwapSend:
    case PolicyEngineStateDataRoleSwapWaitResponse:
        *next_call_ms = policy_engine_common_run(pe, event);
        return true;

    case PolicyEngineStateVconnSwapEvaluate:
    case PolicyEngineStateVconnSwapAccept:
    case PolicyEngineStateVconnSwapWaitForVconn:
    case PolicyEngineStateVconnSwapTurnOnVconn:
    case PolicyEngineStateVconnSwapSendPsRdy:
    case PolicyEngineStateVconnSwapSend:
    case PolicyEngineStateVconnSwapWaitResponse:
    case PolicyEngineStateVconnSwapForceVconn:
        *next_call_ms = vconn_swap_run(pe, event);
        return true;

    default:
        return false;
    }
}
