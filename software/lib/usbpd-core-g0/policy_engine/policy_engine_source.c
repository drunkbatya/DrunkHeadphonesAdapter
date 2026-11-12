#include "policy_engine/policy_engine_i.h"

ProtocolLayerTransmitResult
    policy_engine_source_send_capabilities(PolicyEngine* pe, bool first_message_of_ams) {
    uint8_t index;
    uint8_t byte_index;

    memset(&pe->source_capabilities, 0, sizeof(pe->source_capabilities));
    pe->callbacks->get_source_capabilities(pe->context, pe->port, &pe->source_capabilities);
    if(pe->source_capabilities.count == 0u) {
        return ProtocolLayerTransmitResultFailed;
    }
    if(pe->source_capabilities.count > USB_PD_MAX_DATA_OBJECTS) {
        pe->source_capabilities.count = USB_PD_MAX_DATA_OBJECTS;
    }
    for(index = 0u; index < pe->source_capabilities.count; index++) {
        for(byte_index = 0u; byte_index < USB_PD_DATA_OBJECT_SIZE; byte_index++) {
            pe->tx_buffer
                [USB_PD_MESSAGE_HEADER_SIZE + (index * USB_PD_DATA_OBJECT_SIZE) + byte_index] =
                (uint8_t)(pe->source_capabilities.objects[index] >> (8U * byte_index));
        }
    }
    return protocol_layer_send_message(
        pe->port,
        UsbPdDataMessageSourceCapabilities,
        pe->tx_buffer,
        (uint16_t)(USB_PD_MESSAGE_HEADER_SIZE +
                   (pe->source_capabilities.count * USB_PD_DATA_OBJECT_SIZE)),
        first_message_of_ams);
}

void policy_engine_source_handle_pending_requests(PolicyEngine* pe, PolicyEngineRxEvent* event) {
    if(pe->fast_role_swap_signalled) {
        pe->fast_role_swap_signalled = false;
        pe->state = PolicyEngineStateFastRoleSwapSourceToSinkEvaluate;
        pe->current_ams = PolicyEngineAmsFastRoleSwap;
        policy_engine_drop_dpm_request(pe);
        policy_engine_timer_start_at(
            &pe->fast_role_swap_timer,
            pe->fast_role_swap_signalled_ms,
            POLICY_ENGINE_T_FR_SWAP_RESPONSE);
    }
    policy_engine_role_take_hard_reset_request(pe, event);
}

static void source_release_sink_tx(PolicyEngine* pe) {
    if(pe->spec_revision == UsbPdSpecRevision3) {
        protocol_layer_source_set_sink_tx_ok(pe->port);
    }
}

static uint32_t source_state_startup(PolicyEngine* pe) {
    pe->caps_counter = 0U;
    policy_engine_set_power_state(pe, PolicyEnginePowerDefault);
    if(pe->swap_ongoing) {
        pe->state = PolicyEngineStateSourceWaitSwapSourceStart;
        return policy_engine_start_state_timer(pe, POLICY_ENGINE_T_SWAP_SOURCE_START);
    }
    if(policy_engine_vbus_is(pe, UsbPdVbusLevelPresent)) {
        pe->state = PolicyEngineStateSourceSendCapabilities;
        return POLICY_ENGINE_NEXT_CALL_NOW;
    }
    if(pe->flags.vbus_on_requested == 0U) {
        pe->flags.vbus_on_requested = 1U;
        policy_engine_set_power(pe, UsbPdPowerSourceOn);
        return policy_engine_start_state_timer(pe, POLICY_ENGINE_T_SOURCE_TURN_ON);
    }
    if(policy_engine_timer_is_expired(pe, &pe->state_timer)) {
        pe->state = PolicyEngineStateErrorRecovery;
        return POLICY_ENGINE_NEXT_CALL_NOW;
    }
    return POLICY_ENGINE_NEXT_CALL_SOON;
}

static void source_enter_ready(PolicyEngine* pe) {
    pe->current_ams = PolicyEngineAmsNone;
    if(pe->swap_ongoing) {
        policy_engine_set_swap_ongoing(pe, false);
    }
    source_release_sink_tx(pe);
    pe->state = PolicyEngineStateReadyIdle;
    if(pe->pps_contract && !pe->pps_timer.running) {
        policy_engine_timer_start(pe, &pe->pps_timer, POLICY_ENGINE_T_PPS_TIMEOUT);
    }
}

static uint32_t source_start_dpm_request(PolicyEngine* pe) {
    policy_engine_start_dpm_request(pe);
    return POLICY_ENGINE_NEXT_CALL_NOW;
}

static uint32_t source_state_ready(PolicyEngine* pe, PolicyEngineRxEvent* event) {
    if(pe->state == PolicyEngineStateReady) {
        source_enter_ready(pe);
    }

    if(*event != PolicyEngineRxEventNone) {
        policy_engine_dispatch_message(pe);
        policy_engine_consume_message(pe, event, pe->state);
        return POLICY_ENGINE_NEXT_CALL_DEFAULT;
    }

    if(policy_engine_timer_is_expired(pe, &pe->pps_timer)) {
        pe->state = PolicyEngineStateHardReset;
        return POLICY_ENGINE_NEXT_CALL_NOW;
    }

    if(!policy_engine_receive_queue_has_pending(pe->port) &&
       (pe->dpm_request != PolicyEngineStateDisabled)) {
        return source_start_dpm_request(pe);
    }

    return policy_engine_get_nearest_timer_ms(pe);
}

static uint32_t source_state_wait_swap_source_start(PolicyEngine* pe) {
    if(policy_engine_timer_is_expired(pe, &pe->state_timer)) {
        pe->state = PolicyEngineStateSourceSendCapabilities;
        return POLICY_ENGINE_NEXT_CALL_NOW;
    }
    return POLICY_ENGINE_NEXT_CALL_DEFAULT;
}

static uint32_t source_on_capabilities_acknowledged(PolicyEngine* pe) {
    uint32_t next_call_ms;

    pe->current_ams = PolicyEngineAmsPowerNegotiation;
    pe->flags.pd_connected = 1U;
    pe->caps_counter = 0U;
    pe->hard_reset_counter = 0U;
    next_call_ms = policy_engine_start_sender_response_timer(pe);
    pe->state = PolicyEngineStateSourceWaitRequest;
    return next_call_ms;
}

static uint32_t source_on_capabilities_not_acknowledged(PolicyEngine* pe) {
    uint32_t next_call_ms = POLICY_ENGINE_NEXT_CALL_DEFAULT;

    if(pe->flags.pd_connected == 1U) {
        source_release_sink_tx(pe);
        pe->state = PolicyEngineStateSendSoftReset;
        next_call_ms = POLICY_ENGINE_NEXT_CALL_NOW;
    } else if(pe->caps_counter > POLICY_ENGINE_N_CAPS_COUNT) {
        pe->state = PolicyEngineStateDisabled;
    } else {
        next_call_ms = policy_engine_start_state_timer(pe, POLICY_ENGINE_T_TYPE_C_SEND_SOURCE_CAP);
        pe->state = PolicyEngineStateSourceDiscovery;
    }
    return next_call_ms;
}

static uint32_t source_state_send_capabilities(PolicyEngine* pe) {
    bool first_message_of_ams = (pe->flags.pd_connected == 1U) &&
                                (pe->current_ams == PolicyEngineAmsNone);

    switch(policy_engine_source_send_capabilities(pe, first_message_of_ams)) {
    case ProtocolLayerTransmitResultAcknowledged:
        return source_on_capabilities_acknowledged(pe);

    case ProtocolLayerTransmitResultSinkTxBlocked:
    case ProtocolLayerTransmitResultWaitChunkRequest:
        return POLICY_ENGINE_NEXT_CALL_DEFAULT;

    default:
        return source_on_capabilities_not_acknowledged(pe);
    }
}

static uint32_t source_state_give_source_cap(PolicyEngine* pe) {
    ProtocolLayerTransmitResult result = policy_engine_source_send_capabilities(pe, false);

    if(result == ProtocolLayerTransmitResultAcknowledged) {
        (void)policy_engine_start_sender_response_timer(pe);
        pe->current_ams = PolicyEngineAmsPowerNegotiation;
        pe->state = PolicyEngineStateSourceWaitRequest;
    } else if(result != ProtocolLayerTransmitResultWaitChunkRequest) {
        pe->current_ams = PolicyEngineAmsNone;
        pe->state = PolicyEngineStateSendSoftReset;
    }
    return POLICY_ENGINE_NEXT_CALL_DEFAULT;
}

static uint32_t source_state_discovery(PolicyEngine* pe) {
    if(policy_engine_timer_is_expired(pe, &pe->state_timer)) {
        pe->caps_counter++;
        pe->state = PolicyEngineStateSourceSendCapabilities;
    }
    return POLICY_ENGINE_NEXT_CALL_DEFAULT;
}

static void source_accept_request(PolicyEngine* pe, PolicyEngineRxEvent* event) {
    pe->rdo = usb_pd_message_read_u32(&pe->rx_message[USB_PD_MESSAGE_HEADER_SIZE]);
    policy_engine_adopt_spec_revision(
        pe, (UsbPdSpecRevision)POLICY_ENGINE_HEADER_SPEC_REVISION(pe->rx_header));
    policy_engine_consume_message(pe, event, PolicyEngineStateSourceNegotiateCapability);
}

static uint32_t source_state_wait_request(PolicyEngine* pe, PolicyEngineRxEvent* event) {
    uint32_t next_call_ms = POLICY_ENGINE_NEXT_CALL_DEFAULT;

    if(*event == PolicyEngineRxEventMessage) {
        if(policy_engine_is_data_message(pe, UsbPdDataMessageRequest)) {
            source_accept_request(pe, event);
        } else if(policy_engine_is_data_message(pe, UsbPdDataMessageEprRequest)) {
            policy_engine_consume_message(pe, event, PolicyEngineStateHardReset);
        }
    }
    if(policy_engine_timer_is_expired(pe, &pe->state_timer)) {
        pe->state = PolicyEngineStateHardReset;
        next_call_ms = POLICY_ENGINE_NEXT_CALL_NOW;
    }
    return next_call_ms;
}

static uint32_t source_accept_requested_power(PolicyEngine* pe, bool requested_pps) {
    uint32_t next_call_ms = policy_engine_start_state_timer(pe, POLICY_ENGINE_T_SOURCE_TRANSITION);

    if((pe->power != PolicyEnginePowerDefault) && requested_pps &&
       (pe->flags.requested_pps == 1U)) {
        policy_engine_timer_start(pe, &pe->state_timer, 0U);
        next_call_ms = POLICY_ENGINE_NEXT_CALL_NOW;
    }
    policy_engine_send_control_message(
        pe,
        UsbPdControlMessageAccept,
        PolicyEngineStateSourceTransitionSupply,
        POLICY_ENGINE_MESSAGE_WITHIN_AMS);
    pe->flags.requested_pps = requested_pps ? 1U : 0U;
    return next_call_ms;
}

static void source_refuse_requested_power(PolicyEngine* pe, UsbPdControlMessage control_message) {
    PolicyEngineState refused_next_state = (pe->power == PolicyEnginePowerExplicitContract) ?
                                               PolicyEngineStateReady :
                                               PolicyEngineStateSourceWaitNewCapabilities;

    policy_engine_send_control_message(
        pe, control_message, refused_next_state, POLICY_ENGINE_MESSAGE_WITHIN_AMS);
}

static uint32_t source_state_negotiate_capability(PolicyEngine* pe) {
    uint32_t next_call_ms = POLICY_ENGINE_NEXT_CALL_DEFAULT;
    bool requested_pps = policy_engine_pdo_is_pps(
        policy_engine_capabilities_object(&pe->source_capabilities, pe->rdo));

    switch(pe->callbacks->evaluate_request(pe->context, pe->port, pe->rdo)) {
    case UsbPdAnswerAccept:
        next_call_ms = source_accept_requested_power(pe, requested_pps);
        break;

    case UsbPdAnswerWait:
        source_refuse_requested_power(pe, UsbPdControlMessageWait);
        break;

    default:
        source_refuse_requested_power(pe, UsbPdControlMessageReject);
        break;
    }
    return next_call_ms;
}

static uint32_t source_state_transition_supply(PolicyEngine* pe) {
    if(!policy_engine_timer_is_expired(pe, &pe->state_timer)) {
        return POLICY_ENGINE_NEXT_CALL_DEFAULT;
    }
    policy_engine_set_power_state(pe, PolicyEnginePowerTransition);
    policy_engine_set_power(pe, UsbPdPowerSourceNewContract);
    pe->state = PolicyEngineStateSourceTransitionSupplyPsRdy;
    return policy_engine_start_state_timer(pe, POLICY_ENGINE_T_SOURCE_TRANSITION_REQ);
}

static void source_on_explicit_contract(PolicyEngine* pe) {
    UsbPdCapabilities sink_capabilities;
    UsbPdEvent event;

    policy_engine_set_power_state(pe, PolicyEnginePowerExplicitContract);
    pe->contract_pdo = policy_engine_capabilities_object(&pe->source_capabilities, pe->rdo);
    pe->pps_contract = (pe->flags.requested_pps == 1U);
    if(pe->pps_contract) {
        policy_engine_timer_start(pe, &pe->pps_timer, POLICY_ENGINE_T_PPS_TIMEOUT);
    } else {
        policy_engine_timer_stop(&pe->pps_timer);
    }
    pe->fast_role_swap_signal_allowed = false;
    if(pe->config.fast_role_swap) {
        memset(&sink_capabilities, 0, sizeof(sink_capabilities));
        pe->callbacks->get_sink_capabilities(pe->context, pe->port, &sink_capabilities);
        pe->fast_role_swap_signal_allowed =
            policy_engine_sink_capabilities_request_fast_role_swap(&sink_capabilities);
    }
    memset(&event, 0, sizeof(event));
    event.type = UsbPdEventTypeContract;
    event.contract.pdo = pe->contract_pdo;
    event.contract.rdo = pe->rdo;
    policy_engine_notify(pe, &event);
}

static uint32_t source_state_transition_supply_ps_rdy(PolicyEngine* pe) {
    if(!pe->power_ready) {
        if(policy_engine_timer_is_expired(pe, &pe->state_timer)) {
            pe->state = PolicyEngineStateHardReset;
            return POLICY_ENGINE_NEXT_CALL_NOW;
        }
        return POLICY_ENGINE_NEXT_CALL_DEFAULT;
    }
    if(policy_engine_send_control_message(
           pe,
           UsbPdControlMessagePsRdy,
           PolicyEngineStateReady,
           POLICY_ENGINE_MESSAGE_WITHIN_AMS) == ProtocolLayerTransmitResultAcknowledged) {
        source_on_explicit_contract(pe);
    }
    return POLICY_ENGINE_NEXT_CALL_DEFAULT;
}

static uint32_t source_state_wait_new_capabilities(PolicyEngine* pe) {
    policy_engine_drop_dpm_request(pe);
    return POLICY_ENGINE_NEXT_CALL_IDLE;
}

static uint32_t source_state_soft_reset(PolicyEngine* pe) {
    policy_engine_send_control_message(
        pe,
        UsbPdControlMessageAccept,
        PolicyEngineStateSourceSendCapabilities,
        POLICY_ENGINE_MESSAGE_WITHIN_AMS);
    return POLICY_ENGINE_NEXT_CALL_DEFAULT;
}

static uint32_t source_state_send_soft_reset(PolicyEngine* pe) {
    if(policy_engine_send_control_message(
           pe,
           UsbPdControlMessageSoftReset,
           PolicyEngineStateSourceSendSoftResetWaitAccept,
           POLICY_ENGINE_MESSAGE_WITHIN_AMS) != ProtocolLayerTransmitResultAcknowledged) {
        return POLICY_ENGINE_NEXT_CALL_DEFAULT;
    }
    return policy_engine_start_sender_response_timer(pe);
}

static uint32_t
    source_state_send_soft_reset_wait_accept(PolicyEngine* pe, PolicyEngineRxEvent* event) {
    uint32_t next_call_ms = POLICY_ENGINE_NEXT_CALL_DEFAULT;

    if((*event != PolicyEngineRxEventNone) &&
       (policy_engine_get_control_message(pe) == UsbPdControlMessageAccept)) {
        policy_engine_answer_request(pe, UsbPdRequestSoftReset, UsbPdRequestResultAccepted);
        policy_engine_consume_message(pe, event, PolicyEngineStateSourceSendCapabilities);
    }
    if(policy_engine_timer_is_expired(pe, &pe->state_timer)) {
        policy_engine_answer_request(pe, UsbPdRequestSoftReset, UsbPdRequestResultNoResponse);
        pe->state = PolicyEngineStateHardReset;
        next_call_ms = POLICY_ENGINE_NEXT_CALL_NOW;
    }
    return next_call_ms;
}

static uint32_t source_give_up_after_hard_reset(PolicyEngine* pe) {
    pe->state = policy_engine_hard_reset_give_up_state(pe);
    return (pe->state == PolicyEngineStateErrorRecovery) ? POLICY_ENGINE_NEXT_CALL_NOW :
                                                           POLICY_ENGINE_NEXT_CALL_DEFAULT;
}

static void source_notify_hard_reset(PolicyEngine* pe, bool received) {
    UsbPdEvent event;

    memset(&event, 0, sizeof(event));
    event.type = UsbPdEventTypeHardReset;
    event.hard_reset_received = received;
    policy_engine_notify(pe, &event);
}

static uint32_t source_state_hard_reset(PolicyEngine* pe) {
    if(pe->hard_reset_counter > POLICY_ENGINE_N_HARD_RESET_COUNT) {
        return source_give_up_after_hard_reset(pe);
    }
    source_release_sink_tx(pe);
    policy_engine_send_hard_reset(pe);
    policy_engine_set_swap_ongoing(pe, false);
    pe->hard_reset_counter++;
    pe->hard_reset_by_partner = false;
    policy_engine_discard_requests(pe);
    policy_engine_stop_fast_role_swap(pe);
    pe->current_ams = PolicyEngineAmsHardReset;
    policy_engine_set_hard_reset_in_progress(pe, true);
    source_notify_hard_reset(pe, false);
    pe->state = PolicyEngineStateSourceTransitionToDefault;
    return policy_engine_start_state_timer(pe, POLICY_ENGINE_T_PS_HARD_RESET);
}

static uint32_t source_state_hard_reset_received(PolicyEngine* pe) {
    source_release_sink_tx(pe);
    policy_engine_discard_requests(pe);
    policy_engine_stop_fast_role_swap(pe);
    pe->current_ams = PolicyEngineAmsHardReset;
    policy_engine_set_hard_reset_in_progress(pe, true);
    source_notify_hard_reset(pe, true);
    pe->state = PolicyEngineStateSourceTransitionToDefault;
    return policy_engine_start_state_timer(pe, POLICY_ENGINE_T_PS_HARD_RESET);
}

static uint32_t source_state_transition_to_default(PolicyEngine* pe) {
    if(!policy_engine_timer_is_expired(pe, &pe->state_timer)) {
        return POLICY_ENGINE_NEXT_CALL_DEFAULT;
    }
    policy_engine_set_power(pe, UsbPdPowerSourceOff);
    pe->flags.vbus_on_requested = 0U;
    pe->state = PolicyEngineStateSourceTransitionToDefaultWaitVsafe0v;
    (void)policy_engine_start_state_timer(pe, POLICY_ENGINE_T_SAFE_0V_SOURCE);
    return POLICY_ENGINE_NEXT_CALL_NOW;
}

static uint32_t source_state_transition_to_default_wait_vsafe0v(PolicyEngine* pe) {
    uint32_t next_call_ms = POLICY_ENGINE_NEXT_CALL_DEFAULT;

    if(policy_engine_vbus_is(pe, UsbPdVbusLevelSafe0V)) {
        next_call_ms = policy_engine_start_state_timer(pe, POLICY_ENGINE_T_SOURCE_RECOVER);
        pe->state = PolicyEngineStateSourceTransitionToDefaultWaitVsafe5v;
    } else if(policy_engine_timer_is_expired(pe, &pe->state_timer)) {
        next_call_ms = source_give_up_after_hard_reset(pe);
    }
    return next_call_ms;
}

static uint32_t source_state_transition_to_default_wait_vsafe5v(PolicyEngine* pe) {
    if(!policy_engine_timer_is_expired(pe, &pe->state_timer)) {
        if((pe->flags.vbus_on_requested == 1U) &&
           policy_engine_vbus_is(pe, UsbPdVbusLevelPresent)) {
            policy_engine_reset_after_hard_reset(pe);
            pe->state = PolicyEngineStateSourceSendCapabilities;
            return POLICY_ENGINE_NEXT_CALL_NOW;
        }
        return POLICY_ENGINE_NEXT_CALL_DEFAULT;
    }
    if(pe->flags.vbus_on_requested == 0U) {
        pe->flags.vbus_on_requested = 1U;
        policy_engine_set_power(pe, UsbPdPowerSourceOn);
        return policy_engine_start_state_timer(pe, POLICY_ENGINE_T_SOURCE_TURN_ON);
    }
    pe->state = PolicyEngineStateErrorRecovery;
    return POLICY_ENGINE_NEXT_CALL_NOW;
}

static uint32_t source_state_get_source_cap(PolicyEngine* pe) {
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

static void source_on_power_role_swap_refused(
    PolicyEngine* pe,
    PolicyEngineRxEvent* event,
    UsbPdRequestResult result) {
    policy_engine_answer_request(pe, UsbPdRequestPowerRoleSwap, result);
    policy_engine_consume_message(pe, event, PolicyEngineStateReady);
}

static uint32_t
    source_state_power_role_swap_wait_response(PolicyEngine* pe, PolicyEngineRxEvent* event) {
    uint32_t next_call_ms = POLICY_ENGINE_NEXT_CALL_DEFAULT;

    if(*event == PolicyEngineRxEventMessage) {
        switch(policy_engine_get_control_message(pe)) {
        case UsbPdControlMessageAccept:
            pe->fast_role_swap_signal_allowed = false;
            policy_engine_answer_request(
                pe, UsbPdRequestPowerRoleSwap, UsbPdRequestResultAccepted);
            next_call_ms = policy_engine_start_state_timer(pe, POLICY_ENGINE_T_SOURCE_TRANSITION);
            policy_engine_consume_message(
                pe, event, PolicyEngineStatePowerRoleSwapSourceToSinkAccept);
            return next_call_ms;

        case UsbPdControlMessageReject:
            source_on_power_role_swap_refused(pe, event, UsbPdRequestResultRejected);
            break;

        case UsbPdControlMessageWait:
            source_on_power_role_swap_refused(pe, event, UsbPdRequestResultWait);
            break;

        case UsbPdControlMessageNotSupported:
            source_on_power_role_swap_refused(pe, event, UsbPdRequestResultNotSupported);
            break;

        default:
            break;
        }
    }
    if(policy_engine_timer_is_expired(pe, &pe->state_timer)) {
        policy_engine_answer_request(pe, UsbPdRequestPowerRoleSwap, UsbPdRequestResultNoResponse);
        pe->state = PolicyEngineStateReady;
        next_call_ms = POLICY_ENGINE_NEXT_CALL_NOW;
    }
    return next_call_ms;
}

static uint32_t source_switch_to_sink_and_send_ps_rdy(PolicyEngine* pe) {
    type_c_connection_swap_power_role(pe->port, UsbPdPowerRoleSink);
    protocol_layer_set_header_power_role(pe->port, UsbPdPowerRoleSink);
    if(policy_engine_send_control_message(
           pe,
           UsbPdControlMessagePsRdy,
           PolicyEngineStatePowerRoleSwapSourceToSinkWaitSourceOn,
           POLICY_ENGINE_MESSAGE_WITHIN_AMS) != ProtocolLayerTransmitResultAcknowledged) {
        return POLICY_ENGINE_NEXT_CALL_NOW;
    }
    return policy_engine_start_state_timer(pe, POLICY_ENGINE_T_PS_SOURCE_ON);
}

static uint32_t source_state_power_role_swap_transition_to_off(PolicyEngine* pe) {
    bool vbus_ready = (pe->current_ams == PolicyEngineAmsFastRoleSwap) ?
                          !policy_engine_vbus_is(pe, UsbPdVbusLevelAboveSafe5V) :
                          policy_engine_vbus_is(pe, UsbPdVbusLevelSafe0V);

    policy_engine_set_swap_ongoing(pe, true);
    if(vbus_ready) {
        return source_switch_to_sink_and_send_ps_rdy(pe);
    }
    if(policy_engine_timer_is_expired(pe, &pe->state_timer)) {
        pe->state = PolicyEngineStateErrorRecovery;
        return POLICY_ENGINE_NEXT_CALL_NOW;
    }
    return POLICY_ENGINE_NEXT_CALL_SOON;
}

static uint32_t source_state_power_role_swap_evaluate(PolicyEngine* pe) {
    UsbPdAnswer answer = pe->callbacks->evaluate_power_role_swap(pe->context, pe->port);

    if(answer == UsbPdAnswerAccept) {
        uint32_t next_call_ms =
            policy_engine_start_state_timer(pe, POLICY_ENGINE_T_SOURCE_TRANSITION);

        pe->fast_role_swap_signal_allowed = false;

        policy_engine_send_control_message(
            pe,
            UsbPdControlMessageAccept,
            PolicyEngineStatePowerRoleSwapSourceToSinkAccept,
            POLICY_ENGINE_MESSAGE_WITHIN_AMS);
        return next_call_ms;
    }
    if(answer == UsbPdAnswerWait) {
        policy_engine_send_control_message(
            pe, UsbPdControlMessageWait, PolicyEngineStateReady, POLICY_ENGINE_MESSAGE_WITHIN_AMS);
        return POLICY_ENGINE_NEXT_CALL_DEFAULT;
    }
    policy_engine_send_control_message(
        pe, UsbPdControlMessageReject, PolicyEngineStateReady, POLICY_ENGINE_MESSAGE_WITHIN_AMS);
    return POLICY_ENGINE_NEXT_CALL_DEFAULT;
}

static uint32_t source_state_power_role_swap_accept(PolicyEngine* pe) {
    if(!policy_engine_timer_is_expired(pe, &pe->state_timer)) {
        return POLICY_ENGINE_NEXT_CALL_DEFAULT;
    }
    policy_engine_set_power_state(pe, PolicyEnginePowerTransition);
    policy_engine_set_swap_ongoing(pe, true);
    policy_engine_set_power(pe, UsbPdPowerSourceOff);
    pe->state = PolicyEngineStatePowerRoleSwapSourceToSinkTransitionToOff;
    (void)policy_engine_start_state_timer(pe, POLICY_ENGINE_T_SOURCE_SWAP_STANDBY);
    return POLICY_ENGINE_NEXT_CALL_NOW;
}

static uint32_t
    source_state_power_role_swap_wait_source_on(PolicyEngine* pe, PolicyEngineRxEvent* event) {
    if((*event == PolicyEngineRxEventMessage) &&
       (policy_engine_get_control_message(pe) == UsbPdControlMessagePsRdy)) {
        policy_engine_consume_message(
            pe, event, PolicyEngineStatePowerRoleSwapSourceToSinkComplete);
        return POLICY_ENGINE_NEXT_CALL_NOW;
    }
    if(!policy_engine_timer_is_expired(pe, &pe->state_timer)) {
        return POLICY_ENGINE_NEXT_CALL_DEFAULT;
    }
    pe->state = PolicyEngineStateErrorRecovery;
    return POLICY_ENGINE_NEXT_CALL_NOW;
}

static void source_notify_role_swapped(PolicyEngine* pe, UsbPdPowerRole power_role) {
    UsbPdEvent event;

    memset(&event, 0, sizeof(event));
    event.type = (pe->current_ams == PolicyEngineAmsFastRoleSwap) ? UsbPdEventTypeFastRoleSwapped :
                                                                    UsbPdEventTypePowerRoleSwapped;
    event.power_role = power_role;
    policy_engine_notify(pe, &event);
}

static uint32_t source_state_power_role_swap_to_sink_complete(PolicyEngine* pe) {
    policy_engine_set_power_role(pe, UsbPdPowerRoleSink);
    source_notify_role_swapped(pe, UsbPdPowerRoleSink);
    policy_engine_reset_state_machine(pe);
    return POLICY_ENGINE_NEXT_CALL_NOW;
}

static uint32_t source_state_power_role_swap_to_source_complete(PolicyEngine* pe) {
    if(!policy_engine_timer_is_expired(pe, &pe->state_timer)) {
        return POLICY_ENGINE_NEXT_CALL_DEFAULT;
    }
    source_notify_role_swapped(pe, UsbPdPowerRoleSource);
    source_release_sink_tx(pe);
    policy_engine_reset_state_machine(pe);
    return POLICY_ENGINE_NEXT_CALL_DEFAULT;
}

static uint32_t source_state_give_pps_status(PolicyEngine* pe) {
    uint8_t* ppssdb = &pe->tx_buffer[USB_PD_EXTENDED_PAYLOAD_OFFSET];
    UsbPdPpsStatus pps_status;

    memset(&pps_status, 0, sizeof(pps_status));
    if(!pe->callbacks->get_pps_status(pe->context, pe->port, &pps_status)) {
        pe->state = PolicyEngineStateSendNotSupported;
        return POLICY_ENGINE_NEXT_CALL_DEFAULT;
    }
    ppssdb[0] = (uint8_t)pps_status.output_voltage_20mv;
    ppssdb[1] = (uint8_t)(pps_status.output_voltage_20mv >> 8);
    ppssdb[2] = pps_status.output_current_50ma;
    ppssdb[3] = pps_status.real_time_flags;
    (void)policy_engine_send_extended_message(
        pe,
        UsbPdExtendedMessagePpsStatus,
        USB_PD_EXTENDED_PAYLOAD_OFFSET + 4U,
        PolicyEngineStateReady,
        POLICY_ENGINE_MESSAGE_WITHIN_AMS);
    return POLICY_ENGINE_NEXT_CALL_DEFAULT;
}

static uint32_t
    source_state_fast_role_swap_evaluate(PolicyEngine* pe, PolicyEngineRxEvent* event) {
    uint32_t next_call_ms = POLICY_ENGINE_NEXT_CALL_DEFAULT;

    if((*event == PolicyEngineRxEventMessage) &&
       (policy_engine_get_control_message(pe) == UsbPdControlMessageFrSwap)) {
        policy_engine_consume_message(pe, event, pe->state);
        policy_engine_send_control_message(
            pe,
            UsbPdControlMessageAccept,
            PolicyEngineStatePowerRoleSwapSourceToSinkTransitionToOff,
            POLICY_ENGINE_MESSAGE_WITHIN_AMS);
        if(pe->state == PolicyEngineStatePowerRoleSwapSourceToSinkTransitionToOff) {
            (void)policy_engine_start_state_timer(pe, POLICY_ENGINE_T_SOURCE_SWAP_STANDBY);
            return POLICY_ENGINE_NEXT_CALL_NOW;
        }
    }
    if(policy_engine_timer_is_expired(pe, &pe->fast_role_swap_timer)) {
        pe->state = PolicyEngineStateErrorRecovery;
        next_call_ms = POLICY_ENGINE_NEXT_CALL_NOW;
    }
    return next_call_ms;
}

uint32_t policy_engine_source_run_state(PolicyEngine* pe, PolicyEngineRxEvent* event) {
    uint32_t next_call_ms = POLICY_ENGINE_NEXT_CALL_DEFAULT;

    switch(pe->state) {
    case PolicyEngineStateSourceStartup:
        return source_state_startup(pe);
    case PolicyEngineStateReady:
    case PolicyEngineStateReadyIdle:
        return source_state_ready(pe, event);
    case PolicyEngineStateSourceWaitSwapSourceStart:
        return source_state_wait_swap_source_start(pe);
    case PolicyEngineStateSourceSendCapabilities:
        return source_state_send_capabilities(pe);
    case PolicyEngineStateGiveSourceCap:
        return source_state_give_source_cap(pe);
    case PolicyEngineStateSourceDiscovery:
        return source_state_discovery(pe);
    case PolicyEngineStateSourceWaitRequest:
        return source_state_wait_request(pe, event);
    case PolicyEngineStateSourceNegotiateCapability:
        return source_state_negotiate_capability(pe);
    case PolicyEngineStateSourceTransitionSupply:
        return source_state_transition_supply(pe);
    case PolicyEngineStateSourceTransitionSupplyPsRdy:
        return source_state_transition_supply_ps_rdy(pe);
    case PolicyEngineStateSourceWaitNewCapabilities:
        return source_state_wait_new_capabilities(pe);
    case PolicyEngineStateSoftReset:
        return source_state_soft_reset(pe);
    case PolicyEngineStateSendSoftReset:
        return source_state_send_soft_reset(pe);
    case PolicyEngineStateSourceSendSoftResetWaitAccept:
        return source_state_send_soft_reset_wait_accept(pe, event);
    case PolicyEngineStateHardReset:
        return source_state_hard_reset(pe);
    case PolicyEngineStateHardResetReceived:
        return source_state_hard_reset_received(pe);
    case PolicyEngineStateSourceTransitionToDefault:
        return source_state_transition_to_default(pe);
    case PolicyEngineStateSourceTransitionToDefaultWaitVsafe0v:
        return source_state_transition_to_default_wait_vsafe0v(pe);
    case PolicyEngineStateSourceTransitionToDefaultWaitVsafe5v:
        return source_state_transition_to_default_wait_vsafe5v(pe);
    case PolicyEngineStateGetSourceCap:
        return source_state_get_source_cap(pe);
    case PolicyEngineStatePowerRoleSwapSend:
        return policy_engine_role_send_power_role_swap(
            pe, PolicyEngineStatePowerRoleSwapSourceToSinkWaitResponse);
    case PolicyEngineStatePowerRoleSwapSourceToSinkWaitResponse:
        return source_state_power_role_swap_wait_response(pe, event);
    case PolicyEngineStatePowerRoleSwapSourceToSinkTransitionToOff:
        return source_state_power_role_swap_transition_to_off(pe);
    case PolicyEngineStatePowerRoleSwapSourceToSinkWaitSourceOn:
        return source_state_power_role_swap_wait_source_on(pe, event);
    case PolicyEngineStatePowerRoleSwapEvaluate:
        return source_state_power_role_swap_evaluate(pe);
    case PolicyEngineStatePowerRoleSwapSourceToSinkAccept:
        return source_state_power_role_swap_accept(pe);
    case PolicyEngineStatePowerRoleSwapSourceToSinkComplete:
        return source_state_power_role_swap_to_sink_complete(pe);
    case PolicyEngineStatePowerRoleSwapSinkToSourceComplete:
        return source_state_power_role_swap_to_source_complete(pe);
    case PolicyEngineStateSourceGivePpsStatus:
        return source_state_give_pps_status(pe);
    case PolicyEngineStateFastRoleSwapSourceToSinkEvaluate:
        return source_state_fast_role_swap_evaluate(pe, event);
    default:
        break;
    }

    (void)policy_engine_role_run_shared_state(pe, event, &next_call_ms);
    return next_call_ms;
}
