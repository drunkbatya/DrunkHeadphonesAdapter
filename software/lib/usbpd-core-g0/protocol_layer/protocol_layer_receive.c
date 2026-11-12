#include "protocol_layer/protocol_layer_i.h"

bool protocol_layer_send_goodcrc(uint8_t port, ProtocolLayerMessageHeader rx_header) {
    static const ProtocolLayerMessageHeader goodcrc_header_template = {
        .b.message_type = UsbPdControlMessageGoodCrc};
    ProtocolLayer* prl = &protocol_layers[port];
    ProtocolLayerMessageHeader goodcrc_header = goodcrc_header_template;

    goodcrc_header.b.port_data_role = prl->tx_header.b.port_data_role;
    goodcrc_header.b.port_power_role_or_cable_plug =
        prl->tx_header.b.port_power_role_or_cable_plug;
    if((rx_header.b.spec_revision == UsbPdSpecRevision3) ||
       (rx_header.b.spec_revision == UsbPdSpecRevision2)) {
        goodcrc_header.b.spec_revision = rx_header.b.spec_revision;
    } else {
        goodcrc_header.b.spec_revision = UsbPdSpecRevision2;
    }
    goodcrc_header.b.message_id = rx_header.b.message_id;
    prl->goodcrc_message[0] = (uint8_t)goodcrc_header.d16;
    prl->goodcrc_message[1] = (uint8_t)(goodcrc_header.d16 >> 8);
    prl->goodcrc_in_flight = true;

    while(!usb_pd_platform_timer_is_expired(port, UsbPdPlatformTimerGoodCrc)) {
    }
    return usb_pd_platform_send_message(port, prl->goodcrc_message, sizeof(prl->goodcrc_message));
}

static bool receive_acknowledge(
    ProtocolLayer* prl,
    uint8_t port,
    ProtocolLayerMessageHeader header,
    const ProtocolLayerRxPending* pending) {
    prl->rx_pending = *pending;
    if(!protocol_layer_send_goodcrc(port, header)) {
        prl->goodcrc_in_flight = false;
        protocol_layer_on_goodcrc_sent(prl, port, false);
        return false;
    }
    return true;
}

static void receive_goodcrc(ProtocolLayer* prl, uint8_t port, ProtocolLayerMessageHeader header) {
    if(prl->tx_state != ProtocolLayerTxStateWaitGoodCrc) {
        return;
    }
    if(usb_pd_platform_timer_is_expired(port, UsbPdPlatformTimerRetry)) {
        return;
    }
    prl->rx_goodcrc_header = header;
    prl->tx_state = ProtocolLayerTxStateGoodCrcReceived;
}

static void receive_chunk_request(
    ProtocolLayer* prl,
    uint8_t port,
    ProtocolLayerMessageHeader header,
    ProtocolLayerExtendedHeader extended_header) {
    const ProtocolLayerTxChunking* chunking = &prl->tx_chunking;
    ProtocolLayerRxPending pending = {
        .kind = ProtocolLayerRxKindChunkRequest,
        .message_id = header.b.message_id,
        .chunk_number = extended_header.b.chunk_number,
    };

    if(!chunking->active || (header.b.message_type != (chunking->message_type & 0x1Fu))) {
        pending.kind = ProtocolLayerRxKindIgnored;
    }
    (void)receive_acknowledge(prl, port, header, &pending);
}

static void receive_chunk(
    ProtocolLayer* prl,
    uint8_t port,
    ProtocolLayerMessageHeader header,
    ProtocolLayerExtendedHeader extended_header) {
    ProtocolLayerRxPending pending = {
        .kind = ProtocolLayerRxKindChunk,
        .message_id = header.b.message_id,
        .chunk_number = extended_header.b.chunk_number,
    };

    if(!protocol_layer_chunking_matches_expected(prl, header, extended_header)) {
        protocol_layer_chunking_abort_receive(prl);
        pending.kind = ProtocolLayerRxKindIgnored;
    }
    if(receive_acknowledge(prl, port, header, &pending) &&
       (pending.kind == ProtocolLayerRxKindChunk)) {
        protocol_layer_chunking_store_chunk(prl, port);
    }
}

static bool receive_is_acceptable_extended(ProtocolLayerExtendedHeader extended_header) {
    if(extended_header.b.data_size > USB_PD_MAX_EXTENDED_DATA_SIZE) {
        return false;
    }
    return (extended_header.b.chunked == 0u) || (extended_header.b.chunk_number == 0u);
}

static void receive_message(
    ProtocolLayer* prl,
    uint8_t port,
    ProtocolLayerMessageHeader header,
    ProtocolLayerExtendedHeader extended_header) {
    ProtocolLayerRxPending pending = {
        .kind = ProtocolLayerRxKindMessage,
        .message_id = header.b.message_id,
    };

    if(header.b.extended != 0u) {
        if(!receive_is_acceptable_extended(extended_header)) {
            pending.kind = ProtocolLayerRxKindIgnored;
            (void)receive_acknowledge(prl, port, header, &pending);
            return;
        }
        if((extended_header.b.chunked != 0u) &&
           (extended_header.b.data_size > USB_PD_CHUNK_DATA_SIZE)) {
            if(prl->rx_chunking.state == ProtocolLayerRxChunkStateComplete) {
                return;
            }
            pending.kind = ProtocolLayerRxKindFirstChunk;
        }
    }

    if(!policy_engine_receive_queue_allocate(port, &pending.slot)) {
        return;
    }
    if(prl->tx_chunking.active) {
        prl->tx_chunking.discarded = true;
    }
    prl->rx_ongoing = 1u;
    if(!receive_acknowledge(prl, port, header, &pending)) {
        return;
    }
    policy_engine_receive_queue_store(port, pending.slot, prl->rx_buffer);
    if(pending.kind == ProtocolLayerRxKindFirstChunk) {
        protocol_layer_chunking_start_receive(prl, header, extended_header, pending.slot);
    }
}

void protocol_layer_on_message_received(uint8_t port) {
    ProtocolLayer* prl = &protocol_layers[port];
    ProtocolLayerMessageHeader header;
    ProtocolLayerExtendedHeader extended_header;

    usb_pd_platform_timer_start(
        port, UsbPdPlatformTimerGoodCrc, PROTOCOL_LAYER_GOODCRC_MIN_DELAY_US);
    header.d16 = (uint16_t)(prl->rx_buffer[0] | (prl->rx_buffer[1] << 8));
    extended_header.d16 = (uint16_t)(prl->rx_buffer[2] | (prl->rx_buffer[3] << 8));

    if((header.d16 & PROTOCOL_LAYER_CONTROL_MESSAGE_MASK) == UsbPdControlMessageGoodCrc) {
        receive_goodcrc(prl, port, header);
        return;
    }
    if((header.d16 & PROTOCOL_LAYER_CONTROL_MESSAGE_MASK) == UsbPdControlMessageSoftReset) {
        protocol_layer_reset_message_ids(prl);
    }

    if(prl->rx_message_id == header.b.message_id) {
        const ProtocolLayerRxPending duplicate = {
            .kind = ProtocolLayerRxKindIgnored,
            .message_id = header.b.message_id,
        };

        (void)receive_acknowledge(prl, port, header, &duplicate);
        return;
    }

    if((header.b.extended != 0u) && (extended_header.b.request_chunk != 0u)) {
        receive_chunk_request(prl, port, header, extended_header);
        return;
    }
    if((prl->rx_chunking.state == ProtocolLayerRxChunkStateWaitChunk) &&
       (header.b.extended != 0u) && (extended_header.b.chunked != 0u) &&
       (extended_header.b.chunk_number != 0u)) {
        receive_chunk(prl, port, header, extended_header);
        return;
    }
    if((prl->rx_chunking.state == ProtocolLayerRxChunkStateWaitChunk) ||
       (prl->rx_chunking.state == ProtocolLayerRxChunkStateRequestPending)) {
        protocol_layer_chunking_abort_receive(prl);
    }
    receive_message(prl, port, header, extended_header);
}

void protocol_layer_on_goodcrc_sent(ProtocolLayer* prl, uint8_t port, bool sent) {
    ProtocolLayerRxPending* pending = &prl->rx_pending;

    if(sent) {
        prl->rx_message_id = pending->message_id;
    }

    switch(pending->kind) {
    case ProtocolLayerRxKindMessage:
    case ProtocolLayerRxKindFirstChunk:
        if(sent) {
            if(pending->kind == ProtocolLayerRxKindFirstChunk) {
                protocol_layer_chunking_on_chunk_acknowledged(prl, true);
            }
            policy_engine_receive_queue_commit(
                port, pending->slot, pending->kind == ProtocolLayerRxKindFirstChunk);
        } else {
            if(pending->kind == ProtocolLayerRxKindFirstChunk) {
                prl->rx_chunking.state = ProtocolLayerRxChunkStateIdle;
            }
            policy_engine_receive_queue_free(port, pending->slot);
        }
        prl->rx_ongoing = 0u;
        break;

    case ProtocolLayerRxKindChunk:
        protocol_layer_chunking_on_chunk_acknowledged(prl, sent);
        if(sent) {
            policy_engine_wakeup(port);
        }
        break;

    case ProtocolLayerRxKindChunkRequest:
        if(sent && prl->tx_chunking.active) {
            prl->tx_chunking.chunk_number_requested = pending->chunk_number;
        }
        break;

    default:
        break;
    }
    pending->kind = ProtocolLayerRxKindIgnored;
}
