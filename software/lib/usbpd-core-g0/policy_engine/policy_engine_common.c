#include "policy_engine/policy_engine_i.h"

#define POLICY_ENGINE_SKEDB_VERSION                1U
#define POLICY_ENGINE_SKEDB_VBUS_POWERED           0x02U
#define POLICY_ENGINE_SKEDB_VERSION_OFFSET         10U
#define POLICY_ENGINE_SKEDB_MODES_OFFSET           17U
#define POLICY_ENGINE_SKEDB_OPERATIONAL_PDP_OFFSET 19U
#define POLICY_ENGINE_SKEDB_MAXIMUM_PDP_OFFSET     20U
#define POLICY_ENGINE_SIDO_MAXIMUM_PDP_POSITION    16U
#define POLICY_ENGINE_SIDO_PRESENT_PDP_POSITION    8U
#define POLICY_ENGINE_SIDO2_MAXIMUM_PDP_POSITION   9U
#define POLICY_ENGINE_MW_PER_W                     1000U
#define POLICY_ENGINE_MW_PER_HALF_W                500U

static void common_put_u16(uint8_t* bytes, uint16_t value) {
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
}

static void common_put_u32(uint8_t* bytes, uint32_t value) {
    common_put_u16(bytes, (uint16_t)value);
    common_put_u16(&bytes[2], (uint16_t)(value >> 16));
}

static uint32_t common_max_fixed_power_mw(const UsbPdCapabilities* capabilities) {
    uint32_t max_power_mw = 0u;
    uint8_t index;

    for(index = 0u; index < capabilities->count; index++) {
        uint32_t pdo = capabilities->objects[index];
        uint32_t voltage_50mv = (pdo & USB_PD_FIXED_PDO_VOLTAGE_MASK) >> 10U;
        uint32_t current_10ma = pdo & USB_PD_FIXED_PDO_CURRENT_MASK;
        uint32_t power_mw = (voltage_50mv * 50U * current_10ma * 10U) / 1000U;

        if((((pdo & USB_PD_PDO_TYPE_MASK) >> USB_PD_PDO_TYPE_POSITION) == UsbPdPdoTypeFixed) &&
           (power_mw > max_power_mw)) {
            max_power_mw = power_mw;
        }
    }
    return max_power_mw;
}

static uint32_t common_give_sink_capabilities_extended(PolicyEngine* pe) {
    uint8_t* skedb = &pe->tx_buffer[USB_PD_EXTENDED_PAYLOAD_OFFSET];
    UsbPdCapabilities sink_capabilities;
    uint32_t sink_pdp_w;

    memset(&sink_capabilities, 0, sizeof(sink_capabilities));
    pe->callbacks->get_sink_capabilities(pe->context, pe->port, &sink_capabilities);
    if(sink_capabilities.count == 0u) {
        pe->state = PolicyEngineStateSendNotSupported;
        return POLICY_ENGINE_NEXT_CALL_DEFAULT;
    }
    sink_pdp_w = (common_max_fixed_power_mw(&sink_capabilities) + POLICY_ENGINE_MW_PER_W - 1U) /
                 POLICY_ENGINE_MW_PER_W;
    memset(skedb, 0, POLICY_ENGINE_SINK_CAPABILITIES_EXTENDED_SIZE);
    common_put_u16(&skedb[0], pe->config.identity.vendor_id);
    common_put_u16(&skedb[2], pe->config.identity.product_id);
    common_put_u32(&skedb[4], pe->config.identity.xid);
    skedb[8] = pe->config.identity.firmware_version;
    skedb[9] = pe->config.identity.hardware_version;
    skedb[POLICY_ENGINE_SKEDB_VERSION_OFFSET] = POLICY_ENGINE_SKEDB_VERSION;
    skedb[POLICY_ENGINE_SKEDB_MODES_OFFSET] = POLICY_ENGINE_SKEDB_VBUS_POWERED;
    skedb[POLICY_ENGINE_SKEDB_OPERATIONAL_PDP_OFFSET] = (uint8_t)sink_pdp_w;
    skedb[POLICY_ENGINE_SKEDB_MAXIMUM_PDP_OFFSET] = (uint8_t)sink_pdp_w;
    (void)policy_engine_send_extended_message(
        pe,
        UsbPdExtendedMessageSinkCapabilitiesExtended,
        (uint16_t)(POLICY_ENGINE_SINK_CAPABILITIES_EXTENDED_SIZE + USB_PD_EXTENDED_PAYLOAD_OFFSET),
        PolicyEngineStateReady,
        POLICY_ENGINE_MESSAGE_WITHIN_AMS);
    return POLICY_ENGINE_NEXT_CALL_DEFAULT;
}

static uint32_t common_give_revision(PolicyEngine* pe) {
    static const uint32_t revision = USB_PD_REVISION_MESSAGE_DATA_OBJECT;

    (void)policy_engine_send_data_message(
        pe,
        UsbPdDataMessageRevision,
        &revision,
        1U,
        PolicyEngineStateReady,
        POLICY_ENGINE_MESSAGE_WITHIN_AMS);
    return POLICY_ENGINE_NEXT_CALL_DEFAULT;
}

static uint32_t common_give_source_info(PolicyEngine* pe) {
    UsbPdCapabilities source_capabilities;
    uint32_t power_mw;
    uint32_t power_w;
    uint32_t source_info[POLICY_ENGINE_SOURCE_INFO_OBJECT_COUNT];

    if(pe->power_role == UsbPdPowerRoleSource) {
        source_capabilities = pe->source_capabilities;
    } else {
        memset(&source_capabilities, 0, sizeof(source_capabilities));
        if(policy_engine_is_dual_role(pe)) {
            pe->callbacks->get_source_capabilities(pe->context, pe->port, &source_capabilities);
        }
    }
    if(source_capabilities.count == 0u) {
        pe->state = PolicyEngineStateSendNotSupported;
        return POLICY_ENGINE_NEXT_CALL_DEFAULT;
    }
    power_mw = common_max_fixed_power_mw(&source_capabilities);
    power_w = power_mw / POLICY_ENGINE_MW_PER_W;
    source_info[0] = (power_w << POLICY_ENGINE_SIDO_MAXIMUM_PDP_POSITION) |
                     (power_w << POLICY_ENGINE_SIDO_PRESENT_PDP_POSITION) | power_w;
    source_info[1] = (power_mw / POLICY_ENGINE_MW_PER_HALF_W)
                     << POLICY_ENGINE_SIDO2_MAXIMUM_PDP_POSITION;
    (void)policy_engine_send_data_message(
        pe,
        UsbPdDataMessageSourceInfo,
        source_info,
        POLICY_ENGINE_SOURCE_INFO_OBJECT_COUNT,
        PolicyEngineStateReady,
        POLICY_ENGINE_MESSAGE_WITHIN_AMS);
    return POLICY_ENGINE_NEXT_CALL_DEFAULT;
}

static uint32_t common_send_not_supported(PolicyEngine* pe) {
    (void)policy_engine_send_control_message(
        pe,
        (pe->spec_revision == UsbPdSpecRevision2) ? UsbPdControlMessageReject :
                                                    UsbPdControlMessageNotSupported,
        PolicyEngineStateReady,
        POLICY_ENGINE_MESSAGE_WITHIN_AMS);
    return POLICY_ENGINE_NEXT_CALL_DEFAULT;
}

static uint32_t common_get_sink_cap(PolicyEngine* pe) {
    if(policy_engine_send_control_message(
           pe,
           UsbPdControlMessageGetSinkCap,
           PolicyEngineStateGetSinkCapWaitResponse,
           POLICY_ENGINE_MESSAGE_FIRST_OF_AMS) != ProtocolLayerTransmitResultAcknowledged) {
        return POLICY_ENGINE_NEXT_CALL_DEFAULT;
    }
    pe->current_ams = PolicyEngineAmsGetSinkCap;
    return policy_engine_start_sender_response_timer(pe);
}

static void common_on_sink_capabilities(PolicyEngine* pe) {
    UsbPdCapabilities sink_capabilities;
    UsbPdEvent event;

    policy_engine_read_capabilities(pe, &sink_capabilities);
    if(pe->config.fast_role_swap && (pe->power_role == UsbPdPowerRoleSink) &&
       (pe->power == PolicyEnginePowerExplicitContract) &&
       policy_engine_sink_capabilities_request_fast_role_swap(&sink_capabilities)) {
        pe->fast_role_swap_armed = true;
        usb_pd_platform_set_fast_role_swap_detection(pe->port, true);
        policy_engine_update_vbus_detection(pe);
    }
    memset(&event, 0, sizeof(event));
    event.type = UsbPdEventTypeSinkCapabilities;
    event.capabilities = &sink_capabilities;
    policy_engine_notify(pe, &event);
    policy_engine_answer_request(pe, UsbPdRequestSinkCapabilities, UsbPdRequestResultAccepted);
}

static uint32_t common_get_sink_cap_wait_response(PolicyEngine* pe, PolicyEngineRxEvent* event) {
    if(*event == PolicyEngineRxEventMessage) {
        if(policy_engine_is_data_message(pe, UsbPdDataMessageSinkCapabilities)) {
            common_on_sink_capabilities(pe);
            policy_engine_consume_message(pe, event, PolicyEngineStateReady);
        }
        if(policy_engine_get_control_message(pe) == UsbPdControlMessageReject) {
            policy_engine_consume_message(pe, event, PolicyEngineStateReady);
            policy_engine_answer_request(
                pe, UsbPdRequestSinkCapabilities, UsbPdRequestResultRejected);
        }
        if(policy_engine_get_control_message(pe) == UsbPdControlMessageNotSupported) {
            policy_engine_consume_message(pe, event, PolicyEngineStateReady);
            policy_engine_answer_request(
                pe, UsbPdRequestSinkCapabilities, UsbPdRequestResultNotSupported);
        }
    }
    if(policy_engine_timer_is_expired(pe, &pe->state_timer)) {
        pe->state = PolicyEngineStateReady;
        policy_engine_answer_request(
            pe, UsbPdRequestSinkCapabilities, UsbPdRequestResultNoResponse);
        return POLICY_ENGINE_NEXT_CALL_NOW;
    }
    return POLICY_ENGINE_NEXT_CALL_DEFAULT;
}

static uint32_t common_give_sink_cap(PolicyEngine* pe) {
    UsbPdCapabilities sink_capabilities;

    memset(&sink_capabilities, 0, sizeof(sink_capabilities));
    pe->callbacks->get_sink_capabilities(pe->context, pe->port, &sink_capabilities);
    if(sink_capabilities.count == 0u) {
        pe->state = PolicyEngineStateSendNotSupported;
        return POLICY_ENGINE_NEXT_CALL_NOW;
    }
    (void)policy_engine_send_data_message(
        pe,
        UsbPdDataMessageSinkCapabilities,
        sink_capabilities.objects,
        sink_capabilities.count,
        PolicyEngineStateReady,
        POLICY_ENGINE_MESSAGE_WITHIN_AMS);
    return POLICY_ENGINE_NEXT_CALL_DEFAULT;
}

static uint32_t common_bist_carrier_mode(PolicyEngine* pe) {
    pe->current_ams = PolicyEngineAmsBist;
    (void)protocol_layer_start_bist_carrier_mode(pe->port);
    pe->state = PolicyEngineStateBistCarrierModeWaitExit;
    return policy_engine_start_state_timer(pe, POLICY_ENGINE_T_BIST_CONT_MODE);
}

static uint32_t common_bist_carrier_mode_wait_exit(PolicyEngine* pe) {
    if(!policy_engine_timer_is_expired(pe, &pe->state_timer)) {
        return POLICY_ENGINE_NEXT_CALL_DEFAULT;
    }
    (void)protocol_layer_stop_bist_carrier_mode(pe->port);
    policy_engine_timer_stop(&pe->state_timer);
    pe->state = PolicyEngineStateReady;
    return POLICY_ENGINE_NEXT_CALL_NOW;
}

static uint32_t common_data_role_swap_send(PolicyEngine* pe) {
    if(policy_engine_send_control_message(
           pe,
           UsbPdControlMessageDrSwap,
           PolicyEngineStateDataRoleSwapWaitResponse,
           POLICY_ENGINE_MESSAGE_FIRST_OF_AMS) != ProtocolLayerTransmitResultAcknowledged) {
        return POLICY_ENGINE_NEXT_CALL_DEFAULT;
    }
    pe->current_ams = PolicyEngineAmsDataRoleSwap;
    return policy_engine_start_sender_response_timer(pe);
}

static uint32_t common_data_role_swap_wait_response(PolicyEngine* pe, PolicyEngineRxEvent* event) {
    if(*event != PolicyEngineRxEventNone) {
        switch(policy_engine_get_control_message(pe)) {
        case UsbPdControlMessageAccept:
            policy_engine_swap_data_role(pe);
            policy_engine_consume_message(pe, event, PolicyEngineStateReady);
            policy_engine_answer_request(pe, UsbPdRequestDataRoleSwap, UsbPdRequestResultAccepted);
            break;

        case UsbPdControlMessageReject:
            policy_engine_consume_message(pe, event, PolicyEngineStateReady);
            policy_engine_answer_request(pe, UsbPdRequestDataRoleSwap, UsbPdRequestResultRejected);
            break;

        case UsbPdControlMessageWait:
            policy_engine_consume_message(pe, event, PolicyEngineStateReady);
            policy_engine_answer_request(pe, UsbPdRequestDataRoleSwap, UsbPdRequestResultWait);
            break;

        case UsbPdControlMessageNotSupported:
            policy_engine_consume_message(pe, event, PolicyEngineStateReady);
            policy_engine_answer_request(
                pe, UsbPdRequestDataRoleSwap, UsbPdRequestResultNotSupported);
            break;

        default:
            break;
        }
    }
    if(policy_engine_timer_is_expired(pe, &pe->state_timer)) {
        pe->state = PolicyEngineStateReady;
        policy_engine_answer_request(pe, UsbPdRequestDataRoleSwap, UsbPdRequestResultNoResponse);
        return POLICY_ENGINE_NEXT_CALL_NOW;
    }
    return POLICY_ENGINE_NEXT_CALL_DEFAULT;
}

uint32_t policy_engine_common_run(PolicyEngine* pe, PolicyEngineRxEvent* event) {
    switch(pe->state) {
    case PolicyEngineStateSendNotSupported:
        return common_send_not_supported(pe);
    case PolicyEngineStateGetSinkCap:
        return common_get_sink_cap(pe);
    case PolicyEngineStateGetSinkCapWaitResponse:
        return common_get_sink_cap_wait_response(pe, event);
    case PolicyEngineStateGiveSinkCap:
        return common_give_sink_cap(pe);
    case PolicyEngineStateBistCarrierMode:
        return common_bist_carrier_mode(pe);
    case PolicyEngineStateBistCarrierModeWaitExit:
        return common_bist_carrier_mode_wait_exit(pe);
    case PolicyEngineStateBistTestData:
        policy_engine_consume_message(pe, event, PolicyEngineStateBistTestData);
        return POLICY_ENGINE_NEXT_CALL_DEFAULT;
    case PolicyEngineStateGiveRevision:
        return common_give_revision(pe);
    case PolicyEngineStateGiveSinkCapExtended:
        return common_give_sink_capabilities_extended(pe);
    case PolicyEngineStateDataRoleSwapEvaluate:
        policy_engine_evaluate_data_role_swap(pe);
        return POLICY_ENGINE_NEXT_CALL_DEFAULT;
    case PolicyEngineStateDataRoleSwapSend:
        return common_data_role_swap_send(pe);
    case PolicyEngineStateDataRoleSwapWaitResponse:
        return common_data_role_swap_wait_response(pe, event);
    case PolicyEngineStateGiveSourceInfo:
        return common_give_source_info(pe);
    default:
        pe->state = PolicyEngineStateSendNotSupported;
        return POLICY_ENGINE_NEXT_CALL_DEFAULT;
    }
}

void policy_engine_evaluate_data_role_swap(PolicyEngine* pe) {
    PolicyEngineState next_state = PolicyEngineStateSendReject;

    switch(pe->callbacks->evaluate_data_role_swap(pe->context, pe->port)) {
    case UsbPdAnswerAccept:
        if(policy_engine_send_control_message(
               pe,
               UsbPdControlMessageAccept,
               PolicyEngineStateReady,
               POLICY_ENGINE_MESSAGE_WITHIN_AMS) == ProtocolLayerTransmitResultAcknowledged) {
            policy_engine_swap_data_role(pe);
        }
        return;

    case UsbPdAnswerWait:
        (void)policy_engine_send_control_message(
            pe, UsbPdControlMessageWait, PolicyEngineStateReady, POLICY_ENGINE_MESSAGE_WITHIN_AMS);
        return;

    case UsbPdAnswerReject:
        break;

    default:
        if(pe->spec_revision == UsbPdSpecRevision3) {
            next_state = PolicyEngineStateSendNotSupported;
        }
        break;
    }
    pe->state = next_state;
}
