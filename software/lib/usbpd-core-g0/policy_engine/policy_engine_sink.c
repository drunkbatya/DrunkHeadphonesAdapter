#include "policy_engine/policy_engine_i.h"

#define POLICY_ENGINE_FIXED_PDO_DUAL_ROLE_POWER (1U << 29)

void policy_engine_sink_handle_pending_requests(PolicyEngine* pe, PolicyEngineRxEvent* event) {
    if(pe->fast_role_swap_received) {
        pe->state = PolicyEngineStateFastRoleSwapSinkToSourceSend;
        pe->fast_role_swap_received = false;
        policy_engine_drop_dpm_request(pe);
    }
    policy_engine_role_take_hard_reset_request(pe, event);
}

static uint32_t sink_state_startup(PolicyEngine* pe) {
    pe->state = PolicyEngineStateSinkWaitForCapabilities;
    return policy_engine_start_state_timer(pe, POLICY_ENGINE_T_TYPE_C_SINK_WAIT_CAP);
}

void policy_engine_sink_accept_source_capabilities(PolicyEngine* pe, PolicyEngineRxEvent* event) {
    pe->hard_reset_counter = 0U;
    pe->flags.pd_connected = 1U;
    policy_engine_adopt_spec_revision(
        pe, (UsbPdSpecRevision)POLICY_ENGINE_HEADER_SPEC_REVISION(pe->rx_header));
    policy_engine_store_source_capabilities(pe);
    pe->current_ams = PolicyEngineAmsPowerNegotiation;
    policy_engine_consume_message(pe, event, PolicyEngineStateSinkEvaluateCapability);
}

static uint32_t sink_state_wait_for_capabilities(PolicyEngine* pe, PolicyEngineRxEvent* event) {
    uint32_t next_call_ms = POLICY_ENGINE_NEXT_CALL_DEFAULT;

    policy_engine_set_swap_ongoing(pe, false);
    if((*event == PolicyEngineRxEventMessage) &&
       policy_engine_is_data_message(pe, UsbPdDataMessageSourceCapabilities)) {
        policy_engine_sink_accept_source_capabilities(pe, event);
    }
    if(policy_engine_timer_is_expired(pe, &pe->state_timer)) {
        pe->state = PolicyEngineStateHardReset;
        next_call_ms = POLICY_ENGINE_NEXT_CALL_NOW;
    }
    return next_call_ms;
}

static uint32_t sink_state_evaluate_capability(PolicyEngine* pe) {
    pe->requested_rdo =
        pe->callbacks->select_capability(pe->context, pe->port, &pe->source_capabilities);
    pe->state = PolicyEngineStateSinkSelectCapability;
    return POLICY_ENGINE_NEXT_CALL_NOW;
}

static uint32_t sink_state_select_capability(PolicyEngine* pe) {
    uint32_t requested_pdo =
        policy_engine_capabilities_object(&pe->source_capabilities, pe->requested_rdo);

    pe->flags.requested_pps = policy_engine_pdo_is_pps(requested_pdo) ? 1U : 0U;
    if(policy_engine_send_data_message(
           pe,
           UsbPdDataMessageRequest,
           &pe->requested_rdo,
           1U,
           PolicyEngineStateSinkSelectCapabilityWaitResponse,
           pe->flags.request_first_of_ams == 1U) != ProtocolLayerTransmitResultAcknowledged) {
        return POLICY_ENGINE_NEXT_CALL_DEFAULT;
    }
    pe->flags.request_first_of_ams = 0U;
    return policy_engine_start_sender_response_timer(pe);
}

static uint32_t sink_on_request_accepted(PolicyEngine* pe, PolicyEngineRxEvent* event) {
    policy_engine_timer_start(pe, &pe->state_timer, POLICY_ENGINE_T_PS_TRANSITION);
    policy_engine_set_power_state(pe, PolicyEnginePowerTransition);
    policy_engine_set_power(pe, UsbPdPowerSinkStandby);
    policy_engine_consume_message(pe, event, PolicyEngineStateSinkTransitionSink);
    policy_engine_answer_request(pe, UsbPdRequestPower, UsbPdRequestResultAccepted);
    return POLICY_ENGINE_T_PS_TRANSITION;
}

static uint32_t sink_on_request_refused(
    PolicyEngine* pe,
    PolicyEngineRxEvent* event,
    uint16_t control_message) {
    uint32_t next_call_ms;

    if(pe->power == PolicyEnginePowerExplicitContract) {
        pe->state = (control_message == UsbPdControlMessageWait) ?
                        PolicyEngineStateSinkReadyAfterWait :
                        PolicyEngineStateReady;
        if(pe->pps_timer.running) {
            policy_engine_timer_start(
                pe,
                &pe->pps_timer,
                (control_message == UsbPdControlMessageWait) ? POLICY_ENGINE_T_SINK_REQUEST :
                                                               POLICY_ENGINE_T_PPS_REQUEST);
        }
        next_call_ms = POLICY_ENGINE_NEXT_CALL_NOW;
    } else {
        pe->state = PolicyEngineStateSinkWaitForCapabilities;
        next_call_ms = policy_engine_start_state_timer(pe, POLICY_ENGINE_T_TYPE_C_SINK_WAIT_CAP);
    }
    policy_engine_consume_message(pe, event, pe->state);
    policy_engine_answer_request(
        pe,
        UsbPdRequestPower,
        (control_message == UsbPdControlMessageReject) ? UsbPdRequestResultRejected :
                                                         UsbPdRequestResultWait);
    return next_call_ms;
}

static uint32_t
    sink_state_select_capability_wait_response(PolicyEngine* pe, PolicyEngineRxEvent* event) {
    uint32_t next_call_ms = POLICY_ENGINE_NEXT_CALL_DEFAULT;

    if(*event == PolicyEngineRxEventMessage) {
        uint16_t control_message = policy_engine_get_control_message(pe);

        if(control_message == UsbPdControlMessageAccept) {
            next_call_ms = sink_on_request_accepted(pe, event);
        } else if(
            (control_message == UsbPdControlMessageReject) ||
            (control_message == UsbPdControlMessageWait)) {
            next_call_ms = sink_on_request_refused(pe, event, control_message);
        }
    }

    if(policy_engine_timer_is_expired(pe, &pe->state_timer)) {
        policy_engine_answer_request(pe, UsbPdRequestPower, UsbPdRequestResultNoResponse);
        pe->state = PolicyEngineStateHardReset;
        next_call_ms = POLICY_ENGINE_NEXT_CALL_NOW;
    }
    return next_call_ms;
}

static void sink_on_explicit_contract(PolicyEngine* pe) {
    UsbPdEvent event;

    policy_engine_set_power_state(pe, PolicyEnginePowerExplicitContract);
    pe->rdo = pe->requested_rdo;
    pe->contract_pdo = policy_engine_capabilities_object(&pe->source_capabilities, pe->rdo);
    policy_engine_set_power(pe, UsbPdPowerSinkNewContract);
    policy_engine_timer_stop(&pe->pps_timer);
    if(pe->flags.requested_pps == 1U) {
        policy_engine_timer_start(pe, &pe->pps_timer, POLICY_ENGINE_T_PPS_REQUEST);
    }
    if(pe->config.fast_role_swap_detect && policy_engine_is_dual_role(pe) &&
       !pe->fast_role_swap_queried && (pe->source_capabilities.count != 0u) &&
       ((pe->source_capabilities.objects[0] & POLICY_ENGINE_FIXED_PDO_DUAL_ROLE_POWER) != 0u)) {
        pe->fast_role_swap_queried = true;
        pe->fast_role_swap_query_pending = true;
    }
    memset(&event, 0, sizeof(event));
    event.type = UsbPdEventTypeContract;
    event.contract.pdo = pe->contract_pdo;
    event.contract.rdo = pe->rdo;
    policy_engine_notify(pe, &event);
}

static uint32_t sink_state_transition_sink(PolicyEngine* pe, PolicyEngineRxEvent* event) {
    uint32_t next_call_ms = POLICY_ENGINE_NEXT_CALL_DEFAULT;

    if((*event == PolicyEngineRxEventMessage) &&
       (policy_engine_get_control_message(pe) == UsbPdControlMessagePsRdy)) {
        sink_on_explicit_contract(pe);
        policy_engine_consume_message(pe, event, PolicyEngineStateReady);
        next_call_ms = POLICY_ENGINE_NEXT_CALL_NOW;
    }

    if(policy_engine_timer_is_expired(pe, &pe->state_timer)) {
        pe->state = PolicyEngineStateHardReset;
        next_call_ms = POLICY_ENGINE_NEXT_CALL_NOW;
    }
    return next_call_ms;
}

static void sink_start_hard_reset(PolicyEngine* pe, bool received) {
    UsbPdEvent event;

    policy_engine_set_power_state(pe, PolicyEnginePowerDefault);
    policy_engine_discard_requests(pe);
    policy_engine_stop_fast_role_swap(pe);
    pe->current_ams = PolicyEngineAmsHardReset;
    if(pe->sourcing_vbus) {
        policy_engine_set_power(pe, UsbPdPowerSourceOff);
    }
    policy_engine_set_power(pe, UsbPdPowerSinkHardReset);
    policy_engine_set_hard_reset_in_progress(pe, true);
    memset(&event, 0, sizeof(event));
    event.type = UsbPdEventTypeHardReset;
    event.hard_reset_received = received;
    policy_engine_notify(pe, &event);
    policy_engine_timer_start(pe, &pe->state_timer, POLICY_ENGINE_T_SAFE_0V_SINK);
    pe->state = PolicyEngineStateSinkTransitionToDefault;
}

static uint32_t sink_state_hard_reset(PolicyEngine* pe) {
    policy_engine_disable_vconn_if_sourcing(pe);
    policy_engine_set_swap_ongoing(pe, false);
    policy_engine_set_power_state(pe, PolicyEnginePowerDefault);
    if(pe->hard_reset_counter <= POLICY_ENGINE_N_HARD_RESET_COUNT) {
        pe->hard_reset_counter++;
        policy_engine_send_hard_reset(pe);
        sink_start_hard_reset(pe, false);
        return POLICY_ENGINE_NEXT_CALL_NOW;
    }

    pe->state = policy_engine_hard_reset_give_up_state(pe);
    return (pe->state == PolicyEngineStateErrorRecovery) ? POLICY_ENGINE_NEXT_CALL_NOW :
                                                           POLICY_ENGINE_NEXT_CALL_DEFAULT;
}

static uint32_t sink_state_hard_reset_received(PolicyEngine* pe) {
    policy_engine_disable_vconn_if_sourcing(pe);
    sink_start_hard_reset(pe, true);
    return POLICY_ENGINE_NEXT_CALL_NOW;
}

static uint32_t sink_on_hard_reset_recovery_failed(PolicyEngine* pe) {
    if(pe->hard_reset_counter <= POLICY_ENGINE_N_HARD_RESET_COUNT) {
        pe->state = PolicyEngineStateHardReset;
    } else {
        pe->state = policy_engine_hard_reset_give_up_state(pe);
    }
    return POLICY_ENGINE_NEXT_CALL_NOW;
}

static uint32_t sink_state_transition_to_default(PolicyEngine* pe) {
    uint32_t next_call_ms = POLICY_ENGINE_NEXT_CALL_DEFAULT;

    if(policy_engine_vbus_is(pe, UsbPdVbusLevelSafe0V)) {
        policy_engine_timer_start(
            pe,
            &pe->state_timer,
            POLICY_ENGINE_T_SOURCE_RECOVER_SINK + POLICY_ENGINE_T_SOURCE_TURN_ON);
        pe->state = PolicyEngineStateSinkDiscovery;
        next_call_ms = POLICY_ENGINE_NEXT_CALL_NOW;
    }

    if(policy_engine_timer_is_expired(pe, &pe->state_timer)) {
        next_call_ms = sink_on_hard_reset_recovery_failed(pe);
    }
    return next_call_ms;
}

static uint32_t sink_state_discovery(PolicyEngine* pe) {
    uint32_t next_call_ms = POLICY_ENGINE_NEXT_CALL_DEFAULT;

    if(policy_engine_vbus_is(pe, UsbPdVbusLevelPresent)) {
        policy_engine_reset_after_hard_reset(pe);
        next_call_ms = policy_engine_start_state_timer(pe, POLICY_ENGINE_T_TYPE_C_SINK_WAIT_CAP);
        pe->state = PolicyEngineStateSinkWaitForCapabilities;
    }

    if(policy_engine_timer_is_expired(pe, &pe->state_timer)) {
        next_call_ms = sink_on_hard_reset_recovery_failed(pe);
    }
    return next_call_ms;
}

static uint32_t sink_state_soft_reset(PolicyEngine* pe) {
    if(policy_engine_send_control_message(
           pe,
           UsbPdControlMessageAccept,
           PolicyEngineStateReady,
           POLICY_ENGINE_MESSAGE_WITHIN_AMS) != ProtocolLayerTransmitResultAcknowledged) {
        return POLICY_ENGINE_NEXT_CALL_DEFAULT;
    }
    pe->current_ams = PolicyEngineAmsPowerNegotiation;
    pe->state = PolicyEngineStateSinkWaitForCapabilities;
    return policy_engine_start_state_timer(pe, POLICY_ENGINE_T_TYPE_C_SINK_WAIT_CAP);
}

static uint32_t sink_state_send_soft_reset(PolicyEngine* pe) {
    if(pe->power == PolicyEnginePowerTransition) {
        pe->state = PolicyEngineStateHardReset;
        return POLICY_ENGINE_NEXT_CALL_NOW;
    }
    if(policy_engine_send_control_message(
           pe,
           UsbPdControlMessageSoftReset,
           PolicyEngineStateSinkSendSoftResetWaitAccept,
           POLICY_ENGINE_MESSAGE_WITHIN_AMS) != ProtocolLayerTransmitResultAcknowledged) {
        return POLICY_ENGINE_NEXT_CALL_DEFAULT;
    }
    pe->current_ams = PolicyEngineAmsSoftReset;
    return policy_engine_start_sender_response_timer(pe);
}

static uint32_t
    sink_state_send_soft_reset_wait_accept(PolicyEngine* pe, PolicyEngineRxEvent* event) {
    uint32_t next_call_ms = POLICY_ENGINE_NEXT_CALL_DEFAULT;

    if((*event != PolicyEngineRxEventNone) &&
       (policy_engine_get_control_message(pe) == UsbPdControlMessageAccept)) {
        policy_engine_answer_request(pe, UsbPdRequestSoftReset, UsbPdRequestResultAccepted);
        pe->current_ams = PolicyEngineAmsPowerNegotiation;
        next_call_ms = policy_engine_start_state_timer(pe, POLICY_ENGINE_T_TYPE_C_SINK_WAIT_CAP);
        policy_engine_consume_message(pe, event, PolicyEngineStateSinkWaitForCapabilities);
    }

    if(policy_engine_timer_is_expired(pe, &pe->state_timer)) {
        policy_engine_answer_request(pe, UsbPdRequestSoftReset, UsbPdRequestResultNoResponse);
        pe->state = PolicyEngineStateHardReset;
        next_call_ms = POLICY_ENGINE_NEXT_CALL_NOW;
    }
    return next_call_ms;
}

static uint32_t sink_state_get_source_cap(PolicyEngine* pe) {
    if(policy_engine_send_control_message(
           pe,
           UsbPdControlMessageGetSourceCap,
           PolicyEngineStateGetSourceCapWaitResponse,
           POLICY_ENGINE_MESSAGE_FIRST_OF_AMS) != ProtocolLayerTransmitResultAcknowledged) {
        return POLICY_ENGINE_NEXT_CALL_DEFAULT;
    }
    pe->current_ams = PolicyEngineAmsGetSourceCap;
    return policy_engine_start_sender_response_timer(pe);
}

static uint32_t sink_state_ready(PolicyEngine* pe, PolicyEngineRxEvent* event) {
    if(pe->state != PolicyEngineStateReadyIdle) {
        pe->current_ams = PolicyEngineAmsNone;
        pe->state = PolicyEngineStateReadyIdle;
        if(pe->swap_ongoing) {
            policy_engine_set_swap_ongoing(pe, false);
        }
    }
    return policy_engine_sink_ready(pe, event);
}

static uint32_t sink_state_power_role_swap_evaluate(PolicyEngine* pe) {
    UsbPdAnswer answer = pe->callbacks->evaluate_power_role_swap(pe->context, pe->port);

    if(answer == UsbPdAnswerAccept) {
        uint32_t next_call_ms = POLICY_ENGINE_NEXT_CALL_DEFAULT;

        if(policy_engine_send_control_message(
               pe,
               UsbPdControlMessageAccept,
               PolicyEngineStatePowerRoleSwapSinkToSourceTransitionToOff,
               POLICY_ENGINE_MESSAGE_WITHIN_AMS) == ProtocolLayerTransmitResultAcknowledged) {
            pe->current_ams = PolicyEngineAmsPowerRoleSwap;
            policy_engine_set_swap_ongoing(pe, true);
            policy_engine_set_power_state(pe, PolicyEnginePowerTransition);
            policy_engine_set_power(pe, UsbPdPowerSinkOff);
            next_call_ms = policy_engine_start_state_timer(pe, POLICY_ENGINE_T_PS_SOURCE_OFF);
        }
        return next_call_ms;
    }

    if(answer == UsbPdAnswerWait) {
        (void)policy_engine_send_control_message(
            pe, UsbPdControlMessageWait, PolicyEngineStateReady, POLICY_ENGINE_MESSAGE_WITHIN_AMS);
        return POLICY_ENGINE_NEXT_CALL_DEFAULT;
    }
    pe->state = PolicyEngineStateSendReject;
    return POLICY_ENGINE_NEXT_CALL_NOW;
}

static uint32_t
    sink_state_power_role_swap_transition_to_off(PolicyEngine* pe, PolicyEngineRxEvent* event) {
    uint32_t next_call_ms = POLICY_ENGINE_NEXT_CALL_DEFAULT;

    if((*event == PolicyEngineRxEventMessage) &&
       (policy_engine_get_control_message(pe) == UsbPdControlMessagePsRdy)) {
        pe->flags.vbus_on_requested = 0U;
        policy_engine_consume_message(
            pe, event, PolicyEngineStatePowerRoleSwapSinkToSourceSourceOn);
        return POLICY_ENGINE_NEXT_CALL_NOW;
    }

    if(policy_engine_timer_is_expired(pe, &pe->state_timer)) {
        pe->state = PolicyEngineStateErrorRecovery;
        next_call_ms = POLICY_ENGINE_NEXT_CALL_NOW;
    }
    return next_call_ms;
}

static uint32_t sink_state_power_role_swap_source_on(PolicyEngine* pe) {
    bool fast_role_swap = (pe->current_ams == PolicyEngineAmsFastRoleSwap);

    if(pe->flags.vbus_on_requested == 0U) {
        pe->flags.vbus_on_requested = 1U;
        policy_engine_set_power_state(pe, PolicyEnginePowerTransition);
        type_c_connection_swap_power_role(pe->port, UsbPdPowerRoleSource);
        if(!fast_role_swap) {
            policy_engine_set_power(pe, UsbPdPowerSourceOn);
        }
        (void)policy_engine_start_state_timer(pe, POLICY_ENGINE_T_NEW_SOURCE);
    }
    if(!policy_engine_vbus_is(pe, UsbPdVbusLevelPresent)) {
        if(policy_engine_timer_is_expired(pe, &pe->state_timer)) {
            pe->state = PolicyEngineStateErrorRecovery;
            return POLICY_ENGINE_NEXT_CALL_NOW;
        }
        return POLICY_ENGINE_NEXT_CALL_SOON;
    }
    if(pe->spec_revision == UsbPdSpecRevision3) {
        protocol_layer_source_set_sink_tx_ng(pe->port);
    }
    policy_engine_set_power_role(pe, UsbPdPowerRoleSource);
    if(policy_engine_send_control_message(
           pe,
           UsbPdControlMessagePsRdy,
           PolicyEngineStatePowerRoleSwapSinkToSourceComplete,
           POLICY_ENGINE_MESSAGE_WITHIN_AMS) == ProtocolLayerTransmitResultAcknowledged) {
        return policy_engine_start_state_timer(pe, POLICY_ENGINE_T_SWAP_SOURCE_START);
    }
    policy_engine_set_power_role(pe, UsbPdPowerRoleSink);
    if(pe->state != PolicyEngineStatePowerRoleSwapSinkToSourceSourceOn) {
        policy_engine_set_power(pe, UsbPdPowerSourceOff);
        type_c_connection_swap_power_role(pe->port, UsbPdPowerRoleSink);
    }
    return POLICY_ENGINE_NEXT_CALL_DEFAULT;
}

static uint32_t sink_on_power_role_swap_accepted(PolicyEngine* pe, PolicyEngineRxEvent* event) {
    uint32_t next_call_ms;

    policy_engine_set_swap_ongoing(pe, true);
    next_call_ms = policy_engine_start_state_timer(pe, POLICY_ENGINE_T_PS_SOURCE_OFF);
    policy_engine_set_power_state(pe, PolicyEnginePowerTransition);
    policy_engine_set_power(pe, UsbPdPowerSinkOff);
    policy_engine_answer_request(pe, UsbPdRequestPowerRoleSwap, UsbPdRequestResultAccepted);
    policy_engine_consume_message(
        pe, event, PolicyEngineStatePowerRoleSwapSinkToSourceTransitionToOff);
    return next_call_ms;
}

static PolicyEngineState sink_power_role_swap_refused_state(PolicyEngine* pe) {
    return (pe->current_ams == PolicyEngineAmsFastRoleSwap) ? PolicyEngineStateErrorRecovery :
                                                              PolicyEngineStateReady;
}

static uint32_t
    sink_state_power_role_swap_wait_response(PolicyEngine* pe, PolicyEngineRxEvent* event) {
    uint32_t next_call_ms = POLICY_ENGINE_NEXT_CALL_DEFAULT;

    if(*event == PolicyEngineRxEventMessage) {
        switch(policy_engine_get_control_message(pe)) {
        case UsbPdControlMessageAccept:
            next_call_ms = sink_on_power_role_swap_accepted(pe, event);
            break;

        case UsbPdControlMessageReject:
            policy_engine_consume_message(pe, event, sink_power_role_swap_refused_state(pe));
            policy_engine_answer_request(
                pe, UsbPdRequestPowerRoleSwap, UsbPdRequestResultRejected);
            break;

        case UsbPdControlMessageWait:
            policy_engine_consume_message(pe, event, sink_power_role_swap_refused_state(pe));
            policy_engine_answer_request(pe, UsbPdRequestPowerRoleSwap, UsbPdRequestResultWait);
            break;

        case UsbPdControlMessageNotSupported:
            policy_engine_consume_message(pe, event, sink_power_role_swap_refused_state(pe));
            policy_engine_answer_request(
                pe, UsbPdRequestPowerRoleSwap, UsbPdRequestResultNotSupported);
            break;

        default:
            break;
        }
    }

    if(policy_engine_timer_is_expired(pe, &pe->state_timer)) {
        policy_engine_answer_request(pe, UsbPdRequestPowerRoleSwap, UsbPdRequestResultNoResponse);
        pe->state = sink_power_role_swap_refused_state(pe);
        next_call_ms = POLICY_ENGINE_NEXT_CALL_NOW;
    }
    return next_call_ms;
}

static uint32_t sink_state_give_source_cap(PolicyEngine* pe) {
    UsbPdCapabilities source_capabilities;

    memset(&source_capabilities, 0, sizeof(source_capabilities));
    pe->callbacks->get_source_capabilities(pe->context, pe->port, &source_capabilities);
    if(source_capabilities.count != 0u) {
        (void)policy_engine_send_data_message(
            pe,
            UsbPdDataMessageSourceCapabilities,
            source_capabilities.objects,
            source_capabilities.count,
            PolicyEngineStateReady,
            POLICY_ENGINE_MESSAGE_WITHIN_AMS);
    } else {
        pe->state = PolicyEngineStateReady;
    }
    return POLICY_ENGINE_NEXT_CALL_DEFAULT;
}

static uint32_t sink_state_get_pps_status(PolicyEngine* pe) {
    if(policy_engine_send_control_message(
           pe,
           UsbPdControlMessageGetPpsStatus,
           PolicyEngineStateSinkGetPpsStatusWaitResponse,
           POLICY_ENGINE_MESSAGE_FIRST_OF_AMS) != ProtocolLayerTransmitResultAcknowledged) {
        return POLICY_ENGINE_NEXT_CALL_DEFAULT;
    }
    pe->current_ams = PolicyEngineAmsGetPpsStatus;
    return policy_engine_start_sender_response_timer(pe);
}

static void sink_on_pps_status(PolicyEngine* pe) {
    const uint8_t* ppssdb = &pe->rx_message[USB_PD_EXTENDED_PAYLOAD_OFFSET];
    UsbPdEvent event;

    memset(&event, 0, sizeof(event));
    event.type = UsbPdEventTypePpsStatus;
    event.pps_status.output_voltage_20mv = usb_pd_message_read_u16(ppssdb);
    event.pps_status.output_current_50ma = ppssdb[2];
    event.pps_status.real_time_flags = ppssdb[3];
    policy_engine_notify(pe, &event);
    policy_engine_answer_request(pe, UsbPdRequestPpsStatus, UsbPdRequestResultAccepted);
}

static uint32_t
    sink_state_get_pps_status_wait_response(PolicyEngine* pe, PolicyEngineRxEvent* event) {
    uint32_t next_call_ms = POLICY_ENGINE_NEXT_CALL_DEFAULT;

    if(policy_engine_end_ams_on_not_supported(pe, event)) {
        policy_engine_answer_request(pe, UsbPdRequestPpsStatus, UsbPdRequestResultNotSupported);
        return POLICY_ENGINE_NEXT_CALL_NOW;
    }
    if((*event == PolicyEngineRxEventMessage) &&
       (POLICY_ENGINE_HEADER_IS_EXTENDED(pe->rx_header) == 1U) &&
       (POLICY_ENGINE_HEADER_MESSAGE_TYPE(pe->rx_header) == UsbPdExtendedMessagePpsStatus)) {
        sink_on_pps_status(pe);
        policy_engine_consume_message(pe, event, PolicyEngineStateReady);
    }

    if(policy_engine_timer_is_expired(pe, &pe->state_timer)) {
        policy_engine_answer_request(pe, UsbPdRequestPpsStatus, UsbPdRequestResultNoResponse);
        pe->state = PolicyEngineStateReady;
        next_call_ms = POLICY_ENGINE_NEXT_CALL_NOW;
    }
    return next_call_ms;
}

static uint32_t sink_state_fast_role_swap_send(PolicyEngine* pe) {
    pe->current_ams = PolicyEngineAmsFastRoleSwap;
    if(policy_engine_send_control_message(
           pe,
           UsbPdControlMessageFrSwap,
           PolicyEngineStatePowerRoleSwapSinkToSourceWaitResponse,
           POLICY_ENGINE_MESSAGE_WITHIN_AMS) != ProtocolLayerTransmitResultAcknowledged) {
        return POLICY_ENGINE_NEXT_CALL_DEFAULT;
    }
    return policy_engine_start_sender_response_timer(pe);
}

uint32_t policy_engine_sink_run_state(PolicyEngine* pe, PolicyEngineRxEvent* event) {
    uint32_t next_call_ms = POLICY_ENGINE_NEXT_CALL_DEFAULT;

    switch(pe->state) {
    case PolicyEngineStateSinkStartup:
        return sink_state_startup(pe);
    case PolicyEngineStateSinkWaitForCapabilities:
        return sink_state_wait_for_capabilities(pe, event);
    case PolicyEngineStateSinkEvaluateCapability:
        return sink_state_evaluate_capability(pe);
    case PolicyEngineStateSinkSelectCapability:
        return sink_state_select_capability(pe);
    case PolicyEngineStateSinkSelectCapabilityWaitResponse:
        return sink_state_select_capability_wait_response(pe, event);
    case PolicyEngineStateSinkTransitionSink:
        return sink_state_transition_sink(pe, event);
    case PolicyEngineStateHardReset:
        return sink_state_hard_reset(pe);
    case PolicyEngineStateHardResetReceived:
        return sink_state_hard_reset_received(pe);
    case PolicyEngineStateSinkTransitionToDefault:
        return sink_state_transition_to_default(pe);
    case PolicyEngineStateSinkDiscovery:
        return sink_state_discovery(pe);
    case PolicyEngineStateSoftReset:
        return sink_state_soft_reset(pe);
    case PolicyEngineStateSendSoftReset:
        return sink_state_send_soft_reset(pe);
    case PolicyEngineStateSinkSendSoftResetWaitAccept:
        return sink_state_send_soft_reset_wait_accept(pe, event);
    case PolicyEngineStateGetSourceCap:
        return sink_state_get_source_cap(pe);
    case PolicyEngineStateReady:
    case PolicyEngineStateReadyIdle:
    case PolicyEngineStateSinkReadyAfterWait:
        return sink_state_ready(pe, event);
    case PolicyEngineStatePowerRoleSwapEvaluate:
        return sink_state_power_role_swap_evaluate(pe);
    case PolicyEngineStatePowerRoleSwapSinkToSourceTransitionToOff:
        return sink_state_power_role_swap_transition_to_off(pe, event);
    case PolicyEngineStatePowerRoleSwapSinkToSourceSourceOn:
        return sink_state_power_role_swap_source_on(pe);
    case PolicyEngineStatePowerRoleSwapSend:
        return policy_engine_role_send_power_role_swap(
            pe, PolicyEngineStatePowerRoleSwapSinkToSourceWaitResponse);
    case PolicyEngineStatePowerRoleSwapSinkToSourceWaitResponse:
        return sink_state_power_role_swap_wait_response(pe, event);
    case PolicyEngineStateGiveSourceCap:
        return sink_state_give_source_cap(pe);
    case PolicyEngineStateSinkGetPpsStatus:
        return sink_state_get_pps_status(pe);
    case PolicyEngineStateSinkGetPpsStatusWaitResponse:
        return sink_state_get_pps_status_wait_response(pe, event);
    case PolicyEngineStateFastRoleSwapSinkToSourceSend:
        return sink_state_fast_role_swap_send(pe);
    default:
        break;
    }

    (void)policy_engine_role_run_shared_state(pe, event, &next_call_ms);
    return next_call_ms;
}

static bool sink_can_initiate(PolicyEngine* pe) {
    return !policy_engine_receive_queue_has_pending(pe->port) &&
           ((pe->spec_revision == UsbPdSpecRevision2) ||
            ((pe->spec_revision == UsbPdSpecRevision3) && protocol_layer_is_sink_tx_ok(pe->port)));
}

uint32_t policy_engine_sink_ready(PolicyEngine* pe, PolicyEngineRxEvent* event) {
    uint32_t next_call_ms = POLICY_ENGINE_NEXT_CALL_DEFAULT;

    if(*event != PolicyEngineRxEventNone) {
        policy_engine_dispatch_message(pe);
        policy_engine_consume_message(pe, event, pe->state);
    } else if(policy_engine_timer_is_expired(pe, &pe->pps_timer)) {
        pe->requested_rdo = pe->rdo;
        pe->state = PolicyEngineStateSinkSelectCapability;
        next_call_ms = POLICY_ENGINE_NEXT_CALL_NOW;
        pe->flags.request_first_of_ams = 1U;
    } else if(pe->fast_role_swap_query_pending && sink_can_initiate(pe)) {
        pe->fast_role_swap_query_pending = false;
        pe->state = PolicyEngineStateGetSinkCap;
        next_call_ms = POLICY_ENGINE_NEXT_CALL_NOW;
    } else if((pe->dpm_request != PolicyEngineStateDisabled) && sink_can_initiate(pe)) {
        policy_engine_start_dpm_request(pe);
        if(pe->state == PolicyEngineStateSinkSelectCapability) {
            pe->flags.request_first_of_ams = 1U;
        }
        next_call_ms = POLICY_ENGINE_NEXT_CALL_NOW;
    } else {
        next_call_ms = policy_engine_get_nearest_timer_ms(pe);
        if((pe->dpm_request != PolicyEngineStateDisabled) && (next_call_ms >= 2U)) {
            next_call_ms = POLICY_ENGINE_NEXT_CALL_DEFAULT;
        }
    }

    return next_call_ms;
}
