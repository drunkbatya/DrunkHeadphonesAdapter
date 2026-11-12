#ifndef PROTOCOL_LAYER_I_H_
#define PROTOCOL_LAYER_I_H_

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include "usb_pd_platform.h"
#include "protocol_layer/protocol_layer.h"
#include "policy_engine/policy_engine.h"

#define PROTOCOL_LAYER_MESSAGE_ID_NONE          0x0Fu
#define PROTOCOL_LAYER_MESSAGE_ID_MASK          0x07u
#define PROTOCOL_LAYER_CONTROL_MESSAGE_MASK     0xF01Fu
#define PROTOCOL_LAYER_SINK_TX_TIMER_MS         19u
#define PROTOCOL_LAYER_HARD_RESET_TIMEOUT_MS    5u
#define PROTOCOL_LAYER_CHUNK_SENDER_RESPONSE_MS 27u
#define PROTOCOL_LAYER_CHUNK_SENDER_REQUEST_MS  27u
#define PROTOCOL_LAYER_N_RETRY_COUNT_PD2        3u
#define PROTOCOL_LAYER_N_RETRY_COUNT_PD3        2u
#define PROTOCOL_LAYER_RETRY_TIMER_US           905u
#define PROTOCOL_LAYER_GOODCRC_MIN_DELAY_US     30u
#define PROTOCOL_LAYER_TIMER_MAX_US             10000u

#define PROTOCOL_LAYER_CHUNK_FRAME_SIZE   (USB_PD_EXTENDED_PAYLOAD_OFFSET + USB_PD_CHUNK_DATA_SIZE)
#define PROTOCOL_LAYER_CHUNK_REQUEST_SIZE (USB_PD_MESSAGE_HEADER_SIZE + USB_PD_DATA_OBJECT_SIZE)
#define PROTOCOL_LAYER_CHUNK_NUMBER_NONE  0xFFu

typedef enum {
    ProtocolLayerTxStateIdle,
    ProtocolLayerTxStateSourceSetSinkTxNg,
    ProtocolLayerTxStateSourceWaitSinkTx,
    ProtocolLayerTxStateSinkCheckSinkTxOk,
    ProtocolLayerTxStateCollisionAvoidance,
    ProtocolLayerTxStateConstructMessage,
    ProtocolLayerTxStateSendMessage,
    ProtocolLayerTxStateWaitGoodCrc,
    ProtocolLayerTxStateGoodCrcReceived,
    ProtocolLayerTxStateGoodCrcTimeout,
    ProtocolLayerTxStateDiscarded,
    ProtocolLayerTxStateRetransmit,
    ProtocolLayerTxStateWaitChunkRequest,
} ProtocolLayerTxState;

typedef enum {
    ProtocolLayerTxTypeNone,
    ProtocolLayerTxTypeMessage,
} ProtocolLayerTxType;

typedef enum {
    ProtocolLayerRxKindIgnored,
    ProtocolLayerRxKindMessage,
    ProtocolLayerRxKindFirstChunk,
    ProtocolLayerRxKindChunk,
    ProtocolLayerRxKindChunkRequest,
} ProtocolLayerRxKind;

typedef enum {
    ProtocolLayerRxChunkStateIdle,
    ProtocolLayerRxChunkStateAckPending,
    ProtocolLayerRxChunkStateRequestPending,
    ProtocolLayerRxChunkStateWaitChunk,
    ProtocolLayerRxChunkStateComplete,
    ProtocolLayerRxChunkStateAborted,
} ProtocolLayerRxChunkState;

typedef enum {
    ProtocolLayerChunkRequestNone,
    ProtocolLayerChunkRequestNext,
    ProtocolLayerChunkRequestInvalid,
    ProtocolLayerChunkRequestTimeout,
} ProtocolLayerChunkRequestCheck;

typedef struct {
    ProtocolLayerRxKind kind;
    uint8_t message_id;
    uint8_t slot;
    uint8_t chunk_number;
} ProtocolLayerRxPending;

typedef struct {
    volatile uint8_t state;
    bool response_timer_running;
    uint32_t response_deadline;
    uint8_t message_type;
    uint8_t slot;
    uint8_t chunk_number_expected;
    uint16_t data_size;
    uint8_t request[PROTOCOL_LAYER_CHUNK_REQUEST_SIZE];
} ProtocolLayerRxChunking;

typedef struct {
    bool active;
    volatile bool discarded;
    volatile uint8_t chunk_number_requested;
    bool request_timer_running;
    uint32_t request_deadline;
    uint8_t message_type;
    const uint8_t* message;
    uint16_t size;
    uint16_t data_size;
    uint8_t chunk_number;
    uint8_t frame[PROTOCOL_LAYER_CHUNK_FRAME_SIZE];
} ProtocolLayerTxChunking;

typedef struct {
    ProtocolLayerMessageHeader tx_header;
    ProtocolLayerExtendedHeader tx_extended_header;
    uint8_t* tx_frame;
    uint16_t tx_size;
    volatile uint8_t tx_state;
    volatile uint8_t tx_type;
    volatile bool goodcrc_in_flight;
    uint8_t retry_counter;
    uint8_t tx_message_id;
    uint8_t rx_message_id;
    volatile uint8_t rx_ongoing;
    bool sink_tx_timer_running;
    uint32_t sink_tx_deadline;
    volatile uint8_t reset_completed;
    ProtocolLayerMessageHeader rx_goodcrc_header;
    uint8_t goodcrc_message[2];
    ProtocolLayerRxPending rx_pending;
    ProtocolLayerRxChunking rx_chunking;
    ProtocolLayerTxChunking tx_chunking;
    uint8_t rx_buffer[USB_PD_MESSAGE_BUFFER_SIZE];
} ProtocolLayer;

typedef struct {
    uint8_t port;
    uint8_t message_type;
    uint8_t* message;
    uint16_t size;
    bool first_message_of_ams;
    bool chunk_request;
} ProtocolLayerTransmitRequest;

extern ProtocolLayer protocol_layers[USB_PD_PORT_COUNT];

bool protocol_layer_is_deadline_reached(uint32_t deadline);
void protocol_layer_clear_transfers(ProtocolLayer* prl);
void protocol_layer_reset_message_ids(ProtocolLayer* prl);

void protocol_layer_on_goodcrc_sent(ProtocolLayer* prl, uint8_t port, bool sent);

ProtocolLayerTransmitResult
    protocol_layer_transmit(ProtocolLayer* prl, const ProtocolLayerTransmitRequest* request);

uint16_t protocol_layer_chunk_data_size(uint16_t data_size, uint8_t chunk_number);
void protocol_layer_chunking_prepare_transmit(
    ProtocolLayer* prl,
    const ProtocolLayerTransmitRequest* request,
    ProtocolLayerMessageHeader* header);
bool protocol_layer_chunking_is_same_message(
    const ProtocolLayer* prl,
    const ProtocolLayerTransmitRequest* request);
bool protocol_layer_chunking_is_continuation(
    const ProtocolLayer* prl,
    const ProtocolLayerTransmitRequest* request);
bool protocol_layer_chunking_has_more_to_send(const ProtocolLayer* prl);
ProtocolLayerChunkRequestCheck
    protocol_layer_chunking_check_request(ProtocolLayer* prl, ProtocolLayerMessageHeader* header);
void protocol_layer_chunking_start_receive(
    ProtocolLayer* prl,
    ProtocolLayerMessageHeader header,
    ProtocolLayerExtendedHeader extended_header,
    uint8_t slot);
bool protocol_layer_chunking_matches_expected(
    const ProtocolLayer* prl,
    ProtocolLayerMessageHeader header,
    ProtocolLayerExtendedHeader extended_header);
void protocol_layer_chunking_store_chunk(ProtocolLayer* prl, uint8_t port);
void protocol_layer_chunking_on_chunk_acknowledged(ProtocolLayer* prl, bool sent);
void protocol_layer_chunking_abort_receive(ProtocolLayer* prl);
bool protocol_layer_send_goodcrc(uint8_t port, ProtocolLayerMessageHeader rx_header);

#endif
