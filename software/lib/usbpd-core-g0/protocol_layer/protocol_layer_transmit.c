#include "protocol_layer/protocol_layer_i.h"

static ProtocolLayerTransmitResult
    transmit_finish(ProtocolLayer* prl, ProtocolLayerTransmitResult result) {
    prl->tx_state = ProtocolLayerTxStateIdle;
    prl->tx_chunking.active = false;
    return result;
}

static void transmit_advance_message_id(ProtocolLayer* prl) {
    prl->tx_message_id = (prl->tx_message_id + 1u) & PROTOCOL_LAYER_MESSAGE_ID_MASK;
}

static void transmit_discard_if_receiving(ProtocolLayer* prl, uint8_t port) {
    uint32_t state = usb_pd_platform_disable_interrupts();

    if(policy_engine_receive_queue_has_pending(port) || (prl->rx_ongoing != 0u)) {
        switch(prl->tx_state) {
        case ProtocolLayerTxStateGoodCrcReceived:
            break;

        case ProtocolLayerTxStateWaitGoodCrc:
        case ProtocolLayerTxStateGoodCrcTimeout:
        case ProtocolLayerTxStateRetransmit:
            transmit_advance_message_id(prl);
            prl->tx_state = ProtocolLayerTxStateDiscarded;
            break;

        default:
            prl->tx_state = ProtocolLayerTxStateDiscarded;
            break;
        }
    }
    usb_pd_platform_restore_interrupts(state);
}

static ProtocolLayerTransmitResult
    transmit_start(ProtocolLayer* prl, const ProtocolLayerTransmitRequest* request) {
    prl->tx_chunking.active = false;
    if(request->first_message_of_ams && (prl->tx_header.b.spec_revision == UsbPdSpecRevision3)) {
        prl->tx_state = ProtocolLayerTxStateCollisionAvoidance;
    } else {
        prl->tx_state = ProtocolLayerTxStateConstructMessage;
    }
    return ProtocolLayerTransmitResultBusy;
}

static ProtocolLayerTransmitResult
    transmit_source_set_sink_tx_ng(ProtocolLayer* prl, uint8_t port) {
    usb_pd_platform_set_sink_tx_ok(port, false);
    prl->tx_state = ProtocolLayerTxStateSourceWaitSinkTx;
    prl->sink_tx_deadline = usb_pd_platform_get_tick_ms() + PROTOCOL_LAYER_SINK_TX_TIMER_MS;
    prl->sink_tx_timer_running = true;
    return ProtocolLayerTransmitResultBusy;
}

static ProtocolLayerTransmitResult transmit_source_wait_sink_tx(ProtocolLayer* prl) {
    if(prl->sink_tx_timer_running && !protocol_layer_is_deadline_reached(prl->sink_tx_deadline)) {
        return ProtocolLayerTransmitResultSinkTxBlocked;
    }
    prl->sink_tx_timer_running = false;
    prl->tx_state = ProtocolLayerTxStateConstructMessage;
    return ProtocolLayerTransmitResultBusy;
}

static ProtocolLayerTransmitResult
    transmit_sink_check_sink_tx_ok(ProtocolLayer* prl, uint8_t port) {
    if(usb_pd_platform_is_sink_tx_ok(port)) {
        prl->tx_state = ProtocolLayerTxStateConstructMessage;
        return ProtocolLayerTransmitResultBusy;
    }
    return ProtocolLayerTransmitResultSinkTxBlocked;
}

static ProtocolLayerTransmitResult transmit_collision_avoidance(ProtocolLayer* prl) {
    if(prl->tx_header.b.port_power_role_or_cable_plug == UsbPdPowerRoleSource) {
        prl->tx_state = ProtocolLayerTxStateSourceSetSinkTxNg;
    } else {
        prl->tx_state = ProtocolLayerTxStateSinkCheckSinkTxOk;
    }
    return ProtocolLayerTransmitResultBusy;
}

static ProtocolLayerTransmitResult
    transmit_construct_message(ProtocolLayer* prl, const ProtocolLayerTransmitRequest* request) {
    ProtocolLayerMessageHeader* header = &prl->tx_header;

    header->b.message_type = request->message_type & 0x1Fu;
    header->b.extended = ((request->message_type & USB_PD_EXTENDED_MESSAGE_FLAG) != 0u) ? 1u : 0u;
    prl->retry_counter = 0u;

    if(request->chunk_request) {
        prl->tx_extended_header.d16 = (uint16_t)(request->message[2] | (request->message[3] << 8));
        header->b.data_object_count = 1u;
        prl->tx_frame = request->message;
        prl->tx_size = request->size;
    } else if(header->b.extended != 0u) {
        protocol_layer_chunking_prepare_transmit(prl, request, header);
    } else {
        header->b.data_object_count = (request->size - USB_PD_MESSAGE_HEADER_SIZE) >> 2;
        prl->tx_frame = request->message;
        prl->tx_size = request->size;
        if((request->message_type == UsbPdControlMessageSoftReset) &&
           (header->b.data_object_count == 0u)) {
            protocol_layer_reset_message_ids(prl);
        }
    }
    prl->tx_state = ProtocolLayerTxStateSendMessage;
    return ProtocolLayerTransmitResultBusy;
}

static ProtocolLayerTransmitResult transmit_send_message(ProtocolLayer* prl, uint8_t port) {
    ProtocolLayerMessageHeader* header = &prl->tx_header;

    header->b.message_id = prl->tx_message_id;
    prl->tx_frame[0] = (uint8_t)header->d16;
    prl->tx_frame[1] = (uint8_t)(header->d16 >> 8);
    prl->tx_chunking.chunk_number_requested = PROTOCOL_LAYER_CHUNK_NUMBER_NONE;
    prl->tx_state = ProtocolLayerTxStateWaitGoodCrc;
    prl->tx_type = ProtocolLayerTxTypeMessage;
    usb_pd_platform_timer_start(port, UsbPdPlatformTimerRetry, PROTOCOL_LAYER_TIMER_MAX_US);
    if(!usb_pd_platform_send_message(port, prl->tx_frame, prl->tx_size)) {
        prl->tx_state = ProtocolLayerTxStateDiscarded;
    }
    return ProtocolLayerTransmitResultBusy;
}

static ProtocolLayerTransmitResult transmit_wait_goodcrc(ProtocolLayer* prl, uint8_t port) {
    uint32_t state = usb_pd_platform_disable_interrupts();

    if((prl->tx_state == ProtocolLayerTxStateWaitGoodCrc) &&
       usb_pd_platform_timer_is_expired(port, UsbPdPlatformTimerRetry)) {
        prl->tx_state = ProtocolLayerTxStateGoodCrcTimeout;
    }
    usb_pd_platform_restore_interrupts(state);
    return ProtocolLayerTransmitResultBusy;
}

static void transmit_wait_retry_timer(uint8_t port) {
    while(!usb_pd_platform_timer_is_expired(port, UsbPdPlatformTimerRetry)) {
    }
}

static ProtocolLayerTransmitResult transmit_goodcrc_received(ProtocolLayer* prl, uint8_t port) {
    if((prl->tx_message_id == prl->rx_goodcrc_header.b.message_id) &&
       (prl->rx_goodcrc_header.b.message_type == UsbPdControlMessageGoodCrc)) {
        transmit_advance_message_id(prl);
        if(protocol_layer_chunking_has_more_to_send(prl)) {
            prl->tx_chunking.request_deadline =
                usb_pd_platform_get_tick_ms() + PROTOCOL_LAYER_CHUNK_SENDER_REQUEST_MS;
            prl->tx_chunking.request_timer_running = true;
            prl->tx_state = ProtocolLayerTxStateWaitChunkRequest;
            return ProtocolLayerTransmitResultWaitChunkRequest;
        }
        return transmit_finish(prl, ProtocolLayerTransmitResultAcknowledged);
    }
    transmit_wait_retry_timer(port);
    prl->tx_state = ProtocolLayerTxStateGoodCrcTimeout;
    return ProtocolLayerTransmitResultBusy;
}

static ProtocolLayerTransmitResult transmit_goodcrc_timeout(ProtocolLayer* prl, uint8_t port) {
    ProtocolLayerMessageHeader* header = &prl->tx_header;
    uint8_t max_retry;

    prl->retry_counter++;
    if(header->b.spec_revision == UsbPdSpecRevision2) {
        max_retry = PROTOCOL_LAYER_N_RETRY_COUNT_PD2;
    } else {
        max_retry = PROTOCOL_LAYER_N_RETRY_COUNT_PD3;
    }
    transmit_wait_retry_timer(port);
    if(((header->b.extended != 0u) && (prl->tx_extended_header.b.chunked == 0u) &&
        (prl->tx_extended_header.b.data_size > USB_PD_CHUNK_DATA_SIZE)) ||
       (prl->retry_counter > max_retry)) {
        transmit_advance_message_id(prl);
        return transmit_finish(prl, ProtocolLayerTransmitResultNoGoodCrc);
    }
    prl->tx_state = ProtocolLayerTxStateRetransmit;
    return ProtocolLayerTransmitResultBusy;
}

static ProtocolLayerTransmitResult transmit_retransmit(ProtocolLayer* prl, uint8_t port) {
    prl->tx_state = ProtocolLayerTxStateWaitGoodCrc;
    prl->tx_type = ProtocolLayerTxTypeMessage;
    usb_pd_platform_timer_start(port, UsbPdPlatformTimerRetry, PROTOCOL_LAYER_TIMER_MAX_US);
    (void)usb_pd_platform_send_message(port, prl->tx_frame, prl->tx_size);
    return ProtocolLayerTransmitResultBusy;
}

static ProtocolLayerTransmitResult transmit_wait_chunk_request(ProtocolLayer* prl) {
    switch(protocol_layer_chunking_check_request(prl, &prl->tx_header)) {
    case ProtocolLayerChunkRequestNext:
        prl->retry_counter = 0u;
        prl->tx_state = ProtocolLayerTxStateSendMessage;
        return ProtocolLayerTransmitResultBusy;
    case ProtocolLayerChunkRequestInvalid:
        return transmit_finish(prl, ProtocolLayerTransmitResultNoGoodCrc);
    case ProtocolLayerChunkRequestTimeout:
        return transmit_finish(prl, ProtocolLayerTransmitResultAcknowledged);
    default:
        return ProtocolLayerTransmitResultWaitChunkRequest;
    }
}

static ProtocolLayerTransmitResult
    transmit_step(ProtocolLayer* prl, const ProtocolLayerTransmitRequest* request) {
    switch(prl->tx_state) {
    case ProtocolLayerTxStateIdle:
        return transmit_start(prl, request);
    case ProtocolLayerTxStateSourceSetSinkTxNg:
        return transmit_source_set_sink_tx_ng(prl, request->port);
    case ProtocolLayerTxStateSourceWaitSinkTx:
        return transmit_source_wait_sink_tx(prl);
    case ProtocolLayerTxStateSinkCheckSinkTxOk:
        return transmit_sink_check_sink_tx_ok(prl, request->port);
    case ProtocolLayerTxStateCollisionAvoidance:
        return transmit_collision_avoidance(prl);
    case ProtocolLayerTxStateConstructMessage:
        return transmit_construct_message(prl, request);
    case ProtocolLayerTxStateSendMessage:
        return transmit_send_message(prl, request->port);
    case ProtocolLayerTxStateWaitGoodCrc:
        return transmit_wait_goodcrc(prl, request->port);
    case ProtocolLayerTxStateGoodCrcReceived:
        return transmit_goodcrc_received(prl, request->port);
    case ProtocolLayerTxStateGoodCrcTimeout:
        return transmit_goodcrc_timeout(prl, request->port);
    case ProtocolLayerTxStateDiscarded:
        return transmit_finish(prl, ProtocolLayerTransmitResultDiscarded);
    case ProtocolLayerTxStateRetransmit:
        return transmit_retransmit(prl, request->port);
    case ProtocolLayerTxStateWaitChunkRequest:
        return transmit_wait_chunk_request(prl);
    default:
        return transmit_finish(prl, ProtocolLayerTransmitResultFailed);
    }
}

ProtocolLayerTransmitResult
    protocol_layer_transmit(ProtocolLayer* prl, const ProtocolLayerTransmitRequest* request) {
    ProtocolLayerTransmitResult result;
    bool started = false;

    if(!request->chunk_request && prl->tx_chunking.discarded) {
        prl->tx_chunking.discarded = false;
        if(protocol_layer_chunking_is_same_message(prl, request)) {
            return transmit_finish(prl, ProtocolLayerTransmitResultDiscarded);
        }
    }
    if((prl->tx_state == ProtocolLayerTxStateWaitChunkRequest) &&
       !protocol_layer_chunking_is_continuation(prl, request)) {
        (void)transmit_finish(prl, ProtocolLayerTransmitResultFailed);
    }
    do {
        if(!policy_engine_is_connected(request->port)) {
            return ProtocolLayerTransmitResultFailed;
        }
        if(started && (prl->tx_state == ProtocolLayerTxStateIdle)) {
            return ProtocolLayerTransmitResultDiscarded;
        }
        transmit_discard_if_receiving(prl, request->port);
        result = transmit_step(prl, request);
        started = true;
    } while(result == ProtocolLayerTransmitResultBusy);

    return result;
}

ProtocolLayerTransmitResult protocol_layer_send_message(
    uint8_t port,
    uint8_t message_type,
    uint8_t* message,
    uint16_t size,
    bool first_message_of_ams) {
    const ProtocolLayerTransmitRequest request = {
        .port = port,
        .message_type = message_type,
        .message = message,
        .size = size,
        .first_message_of_ams = first_message_of_ams,
        .chunk_request = false,
    };

    return protocol_layer_transmit(&protocol_layers[port], &request);
}

void protocol_layer_on_transmit_completed(uint8_t port, bool sent) {
    ProtocolLayer* prl = &protocol_layers[port];
    uint8_t tx_type = prl->tx_type;

    prl->tx_type = ProtocolLayerTxTypeNone;
    if(prl->goodcrc_in_flight) {
        prl->goodcrc_in_flight = false;
        if(sent) {
            usb_pd_platform_timer_start(
                port, UsbPdPlatformTimerRetry, PROTOCOL_LAYER_RETRY_TIMER_US);
        }
        protocol_layer_on_goodcrc_sent(prl, port, sent);
    } else if(tx_type == ProtocolLayerTxTypeMessage) {
        usb_pd_platform_timer_start(port, UsbPdPlatformTimerRetry, PROTOCOL_LAYER_RETRY_TIMER_US);
    }
}
