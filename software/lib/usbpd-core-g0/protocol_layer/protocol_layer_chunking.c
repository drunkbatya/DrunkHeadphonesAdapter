#include "protocol_layer/protocol_layer_i.h"

uint16_t protocol_layer_chunk_data_size(uint16_t data_size, uint8_t chunk_number) {
    uint16_t offset = (uint16_t)(chunk_number * USB_PD_CHUNK_DATA_SIZE);
    uint16_t remaining;

    if(offset >= data_size) {
        return 0u;
    }
    remaining = (uint16_t)(data_size - offset);
    return (remaining < USB_PD_CHUNK_DATA_SIZE) ? remaining : USB_PD_CHUNK_DATA_SIZE;
}

static bool chunking_is_last(uint16_t data_size, uint8_t chunk_number) {
    return ((uint16_t)((chunk_number + 1u) * USB_PD_CHUNK_DATA_SIZE)) >= data_size;
}

static void chunking_build_frame(ProtocolLayer* prl, ProtocolLayerMessageHeader* header) {
    ProtocolLayerTxChunking* chunking = &prl->tx_chunking;
    uint16_t chunk_size =
        protocol_layer_chunk_data_size(chunking->data_size, chunking->chunk_number);
    uint8_t object_count =
        (uint8_t)((USB_PD_MESSAGE_HEADER_SIZE + chunk_size + (USB_PD_DATA_OBJECT_SIZE - 1u)) /
                  USB_PD_DATA_OBJECT_SIZE);

    prl->tx_extended_header.d16 = 0u;
    prl->tx_extended_header.b.data_size = chunking->data_size;
    prl->tx_extended_header.b.chunk_number = chunking->chunk_number;
    prl->tx_extended_header.b.chunked = 1u;
    memset(chunking->frame, 0, sizeof(chunking->frame));
    chunking->frame[2] = (uint8_t)prl->tx_extended_header.d16;
    chunking->frame[3] = (uint8_t)(prl->tx_extended_header.d16 >> 8);
    memcpy(
        &chunking->frame[USB_PD_EXTENDED_PAYLOAD_OFFSET],
        &chunking->message
             [USB_PD_EXTENDED_PAYLOAD_OFFSET + (chunking->chunk_number * USB_PD_CHUNK_DATA_SIZE)],
        chunk_size);
    header->b.data_object_count = object_count;
    prl->tx_frame = chunking->frame;
    prl->tx_size =
        (uint16_t)(USB_PD_MESSAGE_HEADER_SIZE + (object_count * USB_PD_DATA_OBJECT_SIZE));
}

void protocol_layer_chunking_prepare_transmit(
    ProtocolLayer* prl,
    const ProtocolLayerTransmitRequest* request,
    ProtocolLayerMessageHeader* header) {
    ProtocolLayerTxChunking* chunking = &prl->tx_chunking;
    uint16_t data_size = (request->size > USB_PD_EXTENDED_PAYLOAD_OFFSET) ?
                             (uint16_t)(request->size - USB_PD_EXTENDED_PAYLOAD_OFFSET) :
                             0u;

    chunking->message_type = request->message_type;
    chunking->message = request->message;
    chunking->size = request->size;
    chunking->data_size = data_size;
    chunking->chunk_number = 0u;
    chunking->chunk_number_requested = PROTOCOL_LAYER_CHUNK_NUMBER_NONE;
    chunking->discarded = false;
    chunking->active = !chunking_is_last(data_size, 0u);
    chunking_build_frame(prl, header);
}

bool protocol_layer_chunking_is_same_message(
    const ProtocolLayer* prl,
    const ProtocolLayerTransmitRequest* request) {
    const ProtocolLayerTxChunking* chunking = &prl->tx_chunking;

    return (chunking->message_type == request->message_type) &&
           (chunking->message == request->message) && (chunking->size == request->size);
}

bool protocol_layer_chunking_is_continuation(
    const ProtocolLayer* prl,
    const ProtocolLayerTransmitRequest* request) {
    return prl->tx_chunking.active && protocol_layer_chunking_is_same_message(prl, request);
}

bool protocol_layer_chunking_has_more_to_send(const ProtocolLayer* prl) {
    const ProtocolLayerTxChunking* chunking = &prl->tx_chunking;

    return chunking->active && !chunking_is_last(chunking->data_size, chunking->chunk_number);
}

ProtocolLayerChunkRequestCheck
    protocol_layer_chunking_check_request(ProtocolLayer* prl, ProtocolLayerMessageHeader* header) {
    ProtocolLayerTxChunking* chunking = &prl->tx_chunking;
    uint8_t chunk_number_requested = chunking->chunk_number_requested;

    if(chunk_number_requested == PROTOCOL_LAYER_CHUNK_NUMBER_NONE) {
        if(chunking->request_timer_running &&
           !protocol_layer_is_deadline_reached(chunking->request_deadline)) {
            return ProtocolLayerChunkRequestNone;
        }
        chunking->request_timer_running = false;
        return ProtocolLayerChunkRequestTimeout;
    }
    if(chunk_number_requested != (uint8_t)(chunking->chunk_number + 1u)) {
        return ProtocolLayerChunkRequestInvalid;
    }
    chunking->chunk_number = chunk_number_requested;
    chunking_build_frame(prl, header);
    return ProtocolLayerChunkRequestNext;
}

void protocol_layer_chunking_start_receive(
    ProtocolLayer* prl,
    ProtocolLayerMessageHeader header,
    ProtocolLayerExtendedHeader extended_header,
    uint8_t slot) {
    ProtocolLayerRxChunking* chunking = &prl->rx_chunking;

    chunking->message_type = header.b.message_type;
    chunking->slot = slot;
    chunking->data_size = extended_header.b.data_size;
    chunking->chunk_number_expected = 0u;
    chunking->state = ProtocolLayerRxChunkStateAckPending;
}

bool protocol_layer_chunking_matches_expected(
    const ProtocolLayer* prl,
    ProtocolLayerMessageHeader header,
    ProtocolLayerExtendedHeader extended_header) {
    const ProtocolLayerRxChunking* chunking = &prl->rx_chunking;

    return (header.b.message_type == chunking->message_type) &&
           (extended_header.b.chunk_number == chunking->chunk_number_expected) &&
           (extended_header.b.data_size == chunking->data_size);
}

void protocol_layer_chunking_store_chunk(ProtocolLayer* prl, uint8_t port) {
    ProtocolLayerRxChunking* chunking = &prl->rx_chunking;
    uint8_t chunk_number = chunking->chunk_number_expected;

    policy_engine_receive_queue_store_chunk(
        port,
        chunking->slot,
        (uint16_t)(USB_PD_EXTENDED_PAYLOAD_OFFSET + (chunk_number * USB_PD_CHUNK_DATA_SIZE)),
        &prl->rx_buffer[USB_PD_EXTENDED_PAYLOAD_OFFSET],
        protocol_layer_chunk_data_size(chunking->data_size, chunk_number));
    chunking->state = ProtocolLayerRxChunkStateAckPending;
}

void protocol_layer_chunking_on_chunk_acknowledged(ProtocolLayer* prl, bool sent) {
    ProtocolLayerRxChunking* chunking = &prl->rx_chunking;

    if(chunking->state != ProtocolLayerRxChunkStateAckPending) {
        return;
    }
    if(!sent) {
        chunking->state = ProtocolLayerRxChunkStateWaitChunk;
        return;
    }
    if(chunking_is_last(chunking->data_size, chunking->chunk_number_expected)) {
        chunking->state = ProtocolLayerRxChunkStateComplete;
    } else {
        chunking->state = ProtocolLayerRxChunkStateRequestPending;
    }
    chunking->chunk_number_expected++;
}

void protocol_layer_chunking_abort_receive(ProtocolLayer* prl) {
    prl->rx_chunking.state = ProtocolLayerRxChunkStateAborted;
}

static void chunking_send_request(ProtocolLayer* prl, uint8_t port, uint8_t slot) {
    ProtocolLayerRxChunking* chunking = &prl->rx_chunking;
    ProtocolLayerExtendedHeader request_header = {.d16 = 0u};
    const ProtocolLayerTransmitRequest request = {
        .port = port,
        .message_type = (uint8_t)(USB_PD_EXTENDED_MESSAGE_FLAG | chunking->message_type),
        .message = chunking->request,
        .size = sizeof(chunking->request),
        .first_message_of_ams = false,
        .chunk_request = true,
    };
    ProtocolLayerTransmitResult result;
    uint32_t state;

    request_header.b.chunk_number = chunking->chunk_number_expected;
    request_header.b.request_chunk = 1u;
    request_header.b.chunked = 1u;
    chunking->request[2] = (uint8_t)request_header.d16;
    chunking->request[3] = (uint8_t)(request_header.d16 >> 8);
    chunking->request[4] = 0u;
    chunking->request[5] = 0u;

    while(!usb_pd_platform_timer_is_expired(port, UsbPdPlatformTimerRetry)) {
    }
    result = protocol_layer_transmit(prl, &request);

    state = usb_pd_platform_disable_interrupts();
    if((chunking->slot == slot) && (chunking->state == ProtocolLayerRxChunkStateWaitChunk)) {
        if(result == ProtocolLayerTransmitResultAcknowledged) {
            chunking->response_deadline =
                usb_pd_platform_get_tick_ms() + PROTOCOL_LAYER_CHUNK_SENDER_RESPONSE_MS;
            chunking->response_timer_running = true;
        } else {
            chunking->state = ProtocolLayerRxChunkStateAborted;
        }
    }
    usb_pd_platform_restore_interrupts(state);
}

ProtocolLayerChunkedReceive protocol_layer_receive_chunks(uint8_t port, uint8_t slot) {
    ProtocolLayer* prl = &protocol_layers[port];
    ProtocolLayerRxChunking* chunking = &prl->rx_chunking;
    ProtocolLayerChunkedReceive result = ProtocolLayerChunkedReceivePending;
    bool request_needed = false;
    uint32_t state = usb_pd_platform_disable_interrupts();

    if(chunking->slot != slot) {
        result = ProtocolLayerChunkedReceiveAborted;
    } else {
        switch(chunking->state) {
        case ProtocolLayerRxChunkStateRequestPending:
            chunking->response_deadline =
                usb_pd_platform_get_tick_ms() + PROTOCOL_LAYER_CHUNK_SENDER_RESPONSE_MS;
            chunking->response_timer_running = true;
            chunking->state = ProtocolLayerRxChunkStateWaitChunk;
            request_needed = true;
            break;

        case ProtocolLayerRxChunkStateAckPending:
            break;

        case ProtocolLayerRxChunkStateWaitChunk:
            if(!chunking->response_timer_running ||
               protocol_layer_is_deadline_reached(chunking->response_deadline)) {
                chunking->response_timer_running = false;
                chunking->state = ProtocolLayerRxChunkStateIdle;
                result = ProtocolLayerChunkedReceiveAborted;
            }
            break;

        case ProtocolLayerRxChunkStateComplete:
            chunking->state = ProtocolLayerRxChunkStateIdle;
            result = ProtocolLayerChunkedReceiveComplete;
            break;

        default:
            chunking->state = ProtocolLayerRxChunkStateIdle;
            result = ProtocolLayerChunkedReceiveAborted;
            break;
        }
    }
    usb_pd_platform_restore_interrupts(state);

    if(request_needed) {
        chunking_send_request(prl, port, slot);
    }
    return result;
}
