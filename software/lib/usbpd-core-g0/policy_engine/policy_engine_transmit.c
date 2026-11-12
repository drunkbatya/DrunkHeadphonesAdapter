#include "policy_engine/policy_engine_i.h"

static ProtocolLayerTransmitResult transmit_handle_result(
    PolicyEngine* pe,
    uint8_t message_type,
    bool is_soft_reset,
    ProtocolLayerTransmitResult result,
    PolicyEngineState next_state) {
    PolicyEngineState error_state = PolicyEngineStateHardReset;

    switch(result) {
    case ProtocolLayerTransmitResultAcknowledged:
        pe->state = next_state;
        pe->flags.dpm_request_ongoing = 0U;
        return ProtocolLayerTransmitResultAcknowledged;

    case ProtocolLayerTransmitResultDiscarded:
        pe->flags.tx_discarded = 1U;
        return ProtocolLayerTransmitResultFailed;

    case ProtocolLayerTransmitResultNoGoodCrc:
        if(pe->request_pending) {
            policy_engine_answer_request(pe, pe->active_request, UsbPdRequestResultNoResponse);
        }
        if(pe->current_ams == PolicyEngineAmsFastRoleSwap) {
            pe->state = ((message_type == UsbPdControlMessageAccept) &&
                         (pe->power_role == UsbPdPowerRoleSource)) ?
                            PolicyEngineStateHardReset :
                            PolicyEngineStateErrorRecovery;
            return ProtocolLayerTransmitResultFailed;
        }
        if(pe->power == PolicyEnginePowerTransition) {
            if(!is_soft_reset) {
                error_state = PolicyEngineStateErrorRecovery;
            }
        } else if(!is_soft_reset) {
            error_state = PolicyEngineStateSendSoftReset;
        }
        pe->state = error_state;
        return ProtocolLayerTransmitResultFailed;

    case ProtocolLayerTransmitResultSinkTxBlocked:
        return ProtocolLayerTransmitResultSinkTxBlocked;

    default:
        return ProtocolLayerTransmitResultFailed;
    }
}

ProtocolLayerTransmitResult policy_engine_send_control_message(
    PolicyEngine* pe,
    UsbPdControlMessage message_type,
    PolicyEngineState next_state,
    bool first_message_of_ams) {
    return policy_engine_send_message(
        pe, (uint8_t)message_type, USB_PD_MESSAGE_HEADER_SIZE, next_state, first_message_of_ams);
}

ProtocolLayerTransmitResult policy_engine_send_data_message(
    PolicyEngine* pe,
    UsbPdDataMessage message_type,
    const uint32_t* data_objects,
    uint32_t data_object_count,
    PolicyEngineState next_state,
    bool first_message_of_ams) {
    uint32_t object_index;
    uint32_t byte_index;

    for(object_index = 0U; object_index < data_object_count; object_index++) {
        for(byte_index = 0U; byte_index < USB_PD_DATA_OBJECT_SIZE; byte_index++) {
            pe->tx_buffer
                [USB_PD_MESSAGE_HEADER_SIZE + (object_index * USB_PD_DATA_OBJECT_SIZE) +
                 byte_index] = (uint8_t)(data_objects[object_index] >> (8U * byte_index));
        }
    }

    return policy_engine_send_message(
        pe,
        (uint8_t)message_type,
        (uint16_t)(USB_PD_MESSAGE_HEADER_SIZE + (data_object_count * USB_PD_DATA_OBJECT_SIZE)),
        next_state,
        first_message_of_ams);
}

ProtocolLayerTransmitResult policy_engine_send_message(
    PolicyEngine* pe,
    uint8_t message_type,
    uint16_t size,
    PolicyEngineState next_state,
    bool first_message_of_ams) {
    ProtocolLayerTransmitResult result;
    bool is_soft_reset;

    result = protocol_layer_send_message(
        pe->port, message_type, pe->tx_buffer, size, first_message_of_ams);
    pe->now_ms = usb_pd_platform_get_tick_ms();
    if(result == ProtocolLayerTransmitResultFailed) {
        return ProtocolLayerTransmitResultFailed;
    }
    is_soft_reset = (message_type == UsbPdControlMessageSoftReset) &&
                    (size == USB_PD_MESSAGE_HEADER_SIZE);
    return transmit_handle_result(pe, message_type, is_soft_reset, result, next_state);
}

ProtocolLayerTransmitResult policy_engine_send_extended_message(
    PolicyEngine* pe,
    UsbPdExtendedMessage message_type,
    uint16_t size,
    PolicyEngineState next_state,
    bool first_message_of_ams) {
    return policy_engine_send_message(
        pe,
        (uint8_t)(message_type | USB_PD_EXTENDED_MESSAGE_FLAG),
        size,
        next_state,
        first_message_of_ams);
}
