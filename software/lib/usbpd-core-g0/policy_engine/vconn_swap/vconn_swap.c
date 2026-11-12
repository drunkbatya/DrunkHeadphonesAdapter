#include "policy_engine/policy_engine_i.h"

static void vconn_swap_complete(PolicyEngine* pe, bool vconn_source) {
    UsbPdEvent event;

    policy_engine_set_vconn(pe, vconn_source);
    memset(&event, 0, sizeof(event));
    event.type = UsbPdEventTypeVconnSwapped;
    event.vconn_source = vconn_source;
    policy_engine_notify(pe, &event);
}

static uint32_t vconn_swap_abort(PolicyEngine* pe) {
    pe->state = PolicyEngineStateReady;
    policy_engine_answer_request(pe, UsbPdRequestVconnSwap, UsbPdRequestResultNoResponse);
    return POLICY_ENGINE_NEXT_CALL_NOW;
}

static uint32_t vconn_swap_evaluate(PolicyEngine* pe) {
    UsbPdAnswer answer;

    if((pe->power != PolicyEnginePowerExplicitContract) || !pe->connected) {
        pe->state = PolicyEngineStateReady;
        return POLICY_ENGINE_NEXT_CALL_NOW;
    }

    answer = pe->callbacks->evaluate_vconn_swap(pe->context, pe->port);
    if(answer == UsbPdAnswerAccept) {
        pe->state = PolicyEngineStateVconnSwapAccept;
        return POLICY_ENGINE_NEXT_CALL_DEFAULT;
    }
    if(answer == UsbPdAnswerWait) {
        policy_engine_send_control_message(
            pe, UsbPdControlMessageWait, PolicyEngineStateReady, POLICY_ENGINE_MESSAGE_WITHIN_AMS);
        return POLICY_ENGINE_NEXT_CALL_DEFAULT;
    }
    policy_engine_send_control_message(
        pe, UsbPdControlMessageReject, PolicyEngineStateReady, POLICY_ENGINE_MESSAGE_WITHIN_AMS);
    pe->state = PolicyEngineStateReady;
    return POLICY_ENGINE_NEXT_CALL_NOW;
}

static uint32_t vconn_swap_accept(PolicyEngine* pe) {
    uint32_t next_call_ms = POLICY_ENGINE_NEXT_CALL_DEFAULT;

    if(policy_engine_send_control_message(
           pe,
           UsbPdControlMessageAccept,
           PolicyEngineStateVconnSwapAccept,
           POLICY_ENGINE_MESSAGE_WITHIN_AMS) != ProtocolLayerTransmitResultAcknowledged) {
        return POLICY_ENGINE_NEXT_CALL_DEFAULT;
    }
    if(pe->vconn_source) {
        pe->state = PolicyEngineStateVconnSwapWaitForVconn;
        next_call_ms = policy_engine_start_state_timer(pe, POLICY_ENGINE_T_VCONN_SOURCE_TIMEOUT);
    } else {
        pe->state = PolicyEngineStateVconnSwapTurnOnVconn;
    }
    return next_call_ms;
}

static uint32_t vconn_swap_wait_for_vconn(PolicyEngine* pe, PolicyEngineRxEvent* event) {
    if(policy_engine_timer_is_expired(pe, &pe->state_timer)) {
        return vconn_swap_abort(pe);
    }
    if((*event == PolicyEngineRxEventMessage) &&
       (policy_engine_get_control_message(pe) == UsbPdControlMessagePsRdy)) {
        policy_engine_consume_message(pe, event, PolicyEngineStateReady);
        vconn_swap_complete(pe, false);
    }
    return POLICY_ENGINE_NEXT_CALL_DEFAULT;
}

static uint32_t vconn_swap_turn_on_vconn(PolicyEngine* pe) {
    policy_engine_set_vconn(pe, true);
    pe->state = PolicyEngineStateVconnSwapSendPsRdy;
    return POLICY_ENGINE_NEXT_CALL_DEFAULT;
}

static uint32_t vconn_swap_send_ps_rdy(PolicyEngine* pe) {
    if(policy_engine_send_control_message(
           pe,
           UsbPdControlMessagePsRdy,
           PolicyEngineStateReady,
           POLICY_ENGINE_MESSAGE_WITHIN_AMS) == ProtocolLayerTransmitResultAcknowledged) {
        vconn_swap_complete(pe, true);
    }
    return POLICY_ENGINE_NEXT_CALL_DEFAULT;
}

static uint32_t vconn_swap_send(PolicyEngine* pe) {
    if(policy_engine_send_control_message(
           pe,
           UsbPdControlMessageVconnSwap,
           PolicyEngineStateVconnSwapWaitResponse,
           POLICY_ENGINE_MESSAGE_FIRST_OF_AMS) != ProtocolLayerTransmitResultAcknowledged) {
        return POLICY_ENGINE_NEXT_CALL_DEFAULT;
    }
    pe->current_ams = PolicyEngineAmsVconnSwap;
    return policy_engine_start_sender_response_timer(pe);
}

static uint32_t vconn_swap_on_response(PolicyEngine* pe, PolicyEngineRxEvent* event) {
    switch(policy_engine_get_control_message(pe)) {
    case UsbPdControlMessageAccept: {
        uint32_t next_call_ms = POLICY_ENGINE_NEXT_CALL_DEFAULT;

        if(pe->vconn_source) {
            pe->state = PolicyEngineStateVconnSwapWaitForVconn;
            next_call_ms =
                policy_engine_start_state_timer(pe, POLICY_ENGINE_T_VCONN_SOURCE_TIMEOUT);
        } else {
            pe->state = PolicyEngineStateVconnSwapTurnOnVconn;
        }
        policy_engine_consume_message(pe, event, pe->state);
        policy_engine_answer_request(pe, UsbPdRequestVconnSwap, UsbPdRequestResultAccepted);
        return next_call_ms;
    }

    case UsbPdControlMessageWait:
        policy_engine_consume_message(pe, event, PolicyEngineStateReady);
        policy_engine_answer_request(pe, UsbPdRequestVconnSwap, UsbPdRequestResultWait);
        return POLICY_ENGINE_NEXT_CALL_DEFAULT;

    case UsbPdControlMessageReject:
        policy_engine_consume_message(pe, event, PolicyEngineStateReady);
        policy_engine_answer_request(pe, UsbPdRequestVconnSwap, UsbPdRequestResultRejected);
        return POLICY_ENGINE_NEXT_CALL_DEFAULT;

    case UsbPdControlMessageNotSupported: {
        uint32_t next_call_ms = POLICY_ENGINE_NEXT_CALL_DEFAULT;

        if(!pe->vconn_source) {
            pe->state = PolicyEngineStateVconnSwapForceVconn;
        } else {
            pe->state = PolicyEngineStateReady;
            next_call_ms = POLICY_ENGINE_NEXT_CALL_NOW;
        }
        policy_engine_consume_message(pe, event, pe->state);
        policy_engine_answer_request(pe, UsbPdRequestVconnSwap, UsbPdRequestResultNotSupported);
        return next_call_ms;
    }

    default:
        return POLICY_ENGINE_NEXT_CALL_DEFAULT;
    }
}

static uint32_t vconn_swap_wait_response(PolicyEngine* pe, PolicyEngineRxEvent* event) {
    uint32_t next_call_ms = POLICY_ENGINE_NEXT_CALL_DEFAULT;

    if(*event == PolicyEngineRxEventMessage) {
        next_call_ms = vconn_swap_on_response(pe, event);
    }
    if(policy_engine_timer_is_expired(pe, &pe->state_timer)) {
        next_call_ms = vconn_swap_abort(pe);
    }
    return next_call_ms;
}

static uint32_t vconn_swap_force_vconn(PolicyEngine* pe) {
    vconn_swap_complete(pe, true);
    pe->state = PolicyEngineStateReady;
    return POLICY_ENGINE_NEXT_CALL_NOW;
}

uint32_t vconn_swap_run(PolicyEngine* pe, PolicyEngineRxEvent* event) {
    switch(pe->state) {
    case PolicyEngineStateVconnSwapEvaluate:
        return vconn_swap_evaluate(pe);
    case PolicyEngineStateVconnSwapAccept:
        return vconn_swap_accept(pe);
    case PolicyEngineStateVconnSwapWaitForVconn:
        return vconn_swap_wait_for_vconn(pe, event);
    case PolicyEngineStateVconnSwapTurnOnVconn:
        return vconn_swap_turn_on_vconn(pe);
    case PolicyEngineStateVconnSwapSendPsRdy:
        return vconn_swap_send_ps_rdy(pe);
    case PolicyEngineStateVconnSwapSend:
        return vconn_swap_send(pe);
    case PolicyEngineStateVconnSwapWaitResponse:
        return vconn_swap_wait_response(pe, event);
    case PolicyEngineStateVconnSwapForceVconn:
        return vconn_swap_force_vconn(pe);
    default:
        return POLICY_ENGINE_NEXT_CALL_DEFAULT;
    }
}
