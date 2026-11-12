#include "policy_engine/policy_engine_i.h"

#define POLICY_ENGINE_VSAFE5V_50MV 100U

void policy_engine_dispatch_message(PolicyEngine* pe) {
    if(POLICY_ENGINE_HEADER_IS_EXTENDED(pe->rx_header) == 1U) {
        pe->state = PolicyEngineStateSendNotSupported;
    } else if(POLICY_ENGINE_HEADER_IS_CONTROL(pe->rx_header)) {
        policy_engine_dispatch_control_message(pe);
    } else {
        policy_engine_dispatch_data_message(pe);
    }
}

void policy_engine_dispatch_control_message(PolicyEngine* pe) {
    PolicyEngineState next_state = PolicyEngineStateSendNotSupported;
    uint8_t message_supported = 1U;

    switch(POLICY_ENGINE_HEADER_MESSAGE_TYPE(pe->rx_header)) {
    case UsbPdControlMessageGoodCrc:
        return;

    case UsbPdControlMessageGotoMin:
        if(pe->power_role == UsbPdPowerRoleSource) {
            return;
        }
        break;

    case UsbPdControlMessageAccept:
    case UsbPdControlMessageReject:
    case UsbPdControlMessagePsRdy:
    case UsbPdControlMessageWait:
        next_state = PolicyEngineStateSendSoftReset;
        break;

    case UsbPdControlMessagePing:
        return;

    case UsbPdControlMessageGetSourceCap:
        pe->current_ams = PolicyEngineAmsGetSourceCap;
        if((pe->power_role == UsbPdPowerRoleSource) || policy_engine_is_dual_role(pe)) {
            next_state = PolicyEngineStateGiveSourceCap;
        } else {
            message_supported = 0U;
        }
        break;

    case UsbPdControlMessageGetSinkCap:
        if((pe->power == PolicyEnginePowerExplicitContract) &&
           ((pe->power_role == UsbPdPowerRoleSink) || policy_engine_is_dual_role(pe))) {
            pe->current_ams = PolicyEngineAmsGetSinkCap;
            pe->state = PolicyEngineStateGiveSinkCap;
            return;
        }
        message_supported = 0U;
        break;

    case UsbPdControlMessageDrSwap:
        if(pe->config.data_role_swap) {
            pe->current_ams = PolicyEngineAmsDataRoleSwap;
            next_state = PolicyEngineStateDataRoleSwapEvaluate;
        } else {
            message_supported = 0U;
        }
        break;

    case UsbPdControlMessagePrSwap:
        if(policy_engine_is_dual_role(pe)) {
            pe->current_ams = PolicyEngineAmsPowerRoleSwap;
            next_state = PolicyEngineStatePowerRoleSwapEvaluate;
        } else {
            message_supported = 0U;
        }
        break;

    case UsbPdControlMessageVconnSwap:
        if(pe->config.vconn_swap) {
            pe->current_ams = PolicyEngineAmsVconnSwap;
            next_state = PolicyEngineStateVconnSwapEvaluate;
        } else {
            message_supported = 0U;
        }
        break;

    case UsbPdControlMessageSoftReset:
        pe->current_ams = PolicyEngineAmsSoftReset;
        policy_engine_notify_type(pe, UsbPdEventTypeSoftReset);
        next_state = PolicyEngineStateSoftReset;
        break;

    case UsbPdControlMessageNotSupported:
        return;

    case UsbPdControlMessageFrSwap:
        pe->current_ams = PolicyEngineAmsFastRoleSwap;
        break;

    case UsbPdControlMessageGetPpsStatus:
        pe->current_ams = PolicyEngineAmsGetPpsStatus;
        if(pe->power_role == UsbPdPowerRoleSource) {
            next_state = PolicyEngineStateSourceGivePpsStatus;
        } else {
            next_state = PolicyEngineStateSendSoftReset;
        }
        break;

    case UsbPdControlMessageGetSinkCapExtended:
        if((pe->power_role == UsbPdPowerRoleSink) || policy_engine_is_dual_role(pe)) {
            pe->current_ams = PolicyEngineAmsGetSinkCapExtended;
            pe->state = PolicyEngineStateGiveSinkCapExtended;
            return;
        }
        break;

    case UsbPdControlMessageGetSourceInfo:
        pe->current_ams = PolicyEngineAmsGetSourceInfo;
        next_state = PolicyEngineStateGiveSourceInfo;
        break;

    case UsbPdControlMessageGetRevision:
        pe->current_ams = PolicyEngineAmsGetRevision;
        next_state = PolicyEngineStateGiveRevision;
        break;

    default:
        message_supported = 0U;
        break;
    }

    if((message_supported == 0U) && (pe->spec_revision != UsbPdSpecRevision3)) {
        next_state = PolicyEngineStateSendReject;
    }
    pe->state = next_state;
}

static PolicyEngineState dispatch_source_capabilities(PolicyEngine* pe) {
    if(pe->power_role == UsbPdPowerRoleSource) {
        return pe->state;
    }
    policy_engine_adopt_spec_revision(
        pe, (UsbPdSpecRevision)POLICY_ENGINE_HEADER_SPEC_REVISION(pe->rx_header));
    policy_engine_store_source_capabilities(pe);
    return PolicyEngineStateSinkEvaluateCapability;
}

static PolicyEngineState dispatch_request(PolicyEngine* pe) {
    if(pe->power_role == UsbPdPowerRoleSink) {
        return pe->state;
    }
    pe->rdo = usb_pd_message_read_u32(&pe->rx_message[USB_PD_MESSAGE_HEADER_SIZE]);
    pe->current_ams = PolicyEngineAmsPowerNegotiation;
    policy_engine_timer_stop(&pe->pps_timer);
    return PolicyEngineStateSourceNegotiateCapability;
}

static bool dispatch_contract_is_vsafe5v(PolicyEngine* pe) {
    uint32_t pdo = pe->contract_pdo;

    return (pe->power == PolicyEnginePowerExplicitContract) &&
           (((pdo & USB_PD_PDO_TYPE_MASK) >> USB_PD_PDO_TYPE_POSITION) == UsbPdPdoTypeFixed) &&
           (((pdo & USB_PD_FIXED_PDO_VOLTAGE_MASK) >> 10U) == POLICY_ENGINE_VSAFE5V_50MV);
}

static PolicyEngineState dispatch_bist(PolicyEngine* pe) {
    uint32_t bist_do = usb_pd_message_read_u32(&pe->rx_message[USB_PD_MESSAGE_HEADER_SIZE]);

    if(!dispatch_contract_is_vsafe5v(pe)) {
        return PolicyEngineStateReady;
    }
    switch(bist_do >> 28U) {
    case UsbPdBistModeCarrier2:
        return PolicyEngineStateBistCarrierMode;

    case UsbPdBistModeTestData:
        return PolicyEngineStateBistTestData;

    case UsbPdBistModeSharedTestModeEntry:
    case UsbPdBistModeSharedTestModeExit:
        return PolicyEngineStateReady;

    default:
        return pe->state;
    }
}

void policy_engine_dispatch_data_message(PolicyEngine* pe) {
    PolicyEngineState next_state = PolicyEngineStateSendSoftReset;

    switch(POLICY_ENGINE_HEADER_MESSAGE_TYPE(pe->rx_header)) {
    case UsbPdDataMessageSourceCapabilities:
        next_state = dispatch_source_capabilities(pe);
        break;

    case UsbPdDataMessageRequest:
        next_state = dispatch_request(pe);
        break;

    case UsbPdDataMessageBist:
        next_state = dispatch_bist(pe);
        break;

    case UsbPdDataMessageAlert:
    case UsbPdDataMessageGetCountryInfo:
    case UsbPdDataMessageEnterUsb:
    case UsbPdDataMessageEprRequest:
    case UsbPdDataMessageEprMode:
        next_state = PolicyEngineStateSendNotSupported;
        break;

    case UsbPdDataMessageVendorDefined:
        if(pe->spec_revision != UsbPdSpecRevision3) {
            return;
        }
        next_state = PolicyEngineStateSendNotSupported;
        break;

    default:
        break;
    }

    pe->state = next_state;
}

bool policy_engine_resolve_ams_conflict(PolicyEngine* pe) {
    ProtocolLayerMessageHeader rx_header;

    if(policy_engine_get_control_message(pe) == UsbPdControlMessageSoftReset) {
        pe->state = PolicyEngineStateSoftReset;
        return true;
    }

    switch(pe->current_ams) {
    case PolicyEngineAmsNone:
        pe->state = PolicyEngineStateReady;
        return false;

    case PolicyEngineAmsSoftReset:
        pe->state = PolicyEngineStateHardReset;
        return true;

    case PolicyEngineAmsHardReset:
        return true;

    case PolicyEngineAmsBist:
        if(pe->state == PolicyEngineStateBistCarrierModeWaitExit) {
            (void)protocol_layer_stop_bist_carrier_mode(pe->port);
        }
        pe->state = PolicyEngineStateHardReset;
        return true;

    case PolicyEngineAmsPowerNegotiation:
    case PolicyEngineAmsGetSourceCap:
    case PolicyEngineAmsGetSinkCap:
    case PolicyEngineAmsFastRoleSwap:
        pe->state = PolicyEngineStateErrorRecovery;
        return true;

    case PolicyEngineAmsPowerRoleSwap:
    case PolicyEngineAmsGetPpsStatus:
    case PolicyEngineAmsDataRoleSwap:
    case PolicyEngineAmsVconnSwap:
    case PolicyEngineAmsGetSinkCapExtended:
        rx_header.d16 = pe->rx_header;
        policy_engine_adopt_spec_revision(pe, (UsbPdSpecRevision)rx_header.b.spec_revision);
        pe->state = PolicyEngineStateSendSoftReset;
        return true;

    default:
        return true;
    }
}

void policy_engine_consume_message(
    PolicyEngine* pe,
    PolicyEngineRxEvent* event,
    PolicyEngineState next_state) {
    if(*event != PolicyEngineRxEventNone) {
        *event = PolicyEngineRxEventNone;
        policy_engine_receive_queue_release(pe);
    }
    pe->state = next_state;
}

bool policy_engine_is_data_message(PolicyEngine* pe, UsbPdDataMessage message_type) {
    uint16_t header = pe->rx_header;

    if((POLICY_ENGINE_HEADER_IS_EXTENDED(header) == 0U) &&
       (POLICY_ENGINE_HEADER_DATA_OBJECT_COUNT(header) != 0U) &&
       (POLICY_ENGINE_HEADER_MESSAGE_TYPE(header) == (uint16_t)message_type)) {
        return true;
    }
    return false;
}

uint16_t policy_engine_get_control_message(PolicyEngine* pe) {
    if(POLICY_ENGINE_HEADER_IS_CONTROL(pe->rx_header)) {
        return POLICY_ENGINE_HEADER_MESSAGE_TYPE(pe->rx_header);
    }
    return 0xFFFFU;
}
