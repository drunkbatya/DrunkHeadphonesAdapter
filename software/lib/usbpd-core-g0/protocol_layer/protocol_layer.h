#ifndef PROTOCOL_LAYER_H_
#define PROTOCOL_LAYER_H_

#include <stdbool.h>
#include <stdint.h>

#include "message/usb_pd_message.h"

typedef union {
    uint16_t d16;
    struct {
        uint16_t message_type                  : 5;
        uint16_t port_data_role                : 1;
        uint16_t spec_revision                 : 2;
        uint16_t port_power_role_or_cable_plug : 1;
        uint16_t message_id                    : 3;
        uint16_t data_object_count             : 3;
        uint16_t extended                      : 1;
    } b;
} ProtocolLayerMessageHeader;

typedef union {
    uint16_t d16;
    struct {
        uint16_t data_size     : 9;
        uint16_t reserved      : 1;
        uint16_t request_chunk : 1;
        uint16_t chunk_number  : 4;
        uint16_t chunked       : 1;
    } b;
} ProtocolLayerExtendedHeader;

typedef enum {
    ProtocolLayerChunkedReceivePending,
    ProtocolLayerChunkedReceiveComplete,
    ProtocolLayerChunkedReceiveAborted,
} ProtocolLayerChunkedReceive;

typedef enum {
    ProtocolLayerTransmitResultAcknowledged,
    ProtocolLayerTransmitResultDiscarded,
    ProtocolLayerTransmitResultNoGoodCrc,
    ProtocolLayerTransmitResultSinkTxBlocked,
    ProtocolLayerTransmitResultWaitChunkRequest,
    ProtocolLayerTransmitResultFailed,
    ProtocolLayerTransmitResultBusy,
} ProtocolLayerTransmitResult;

void protocol_layer_init(uint8_t port);
void protocol_layer_set_header(
    uint8_t port,
    UsbPdPowerRole power_role,
    UsbPdDataRole data_role,
    UsbPdSpecRevision spec_revision);
void protocol_layer_set_header_power_role(uint8_t port, UsbPdPowerRole power_role);
void protocol_layer_set_header_data_role(uint8_t port, UsbPdDataRole data_role);
void protocol_layer_set_header_spec_revision(uint8_t port, UsbPdSpecRevision spec_revision);
void protocol_layer_source_set_sink_tx_ng(uint8_t port);
void protocol_layer_source_set_sink_tx_ok(uint8_t port);
bool protocol_layer_is_sink_tx_ok(uint8_t port);
ProtocolLayerTransmitResult protocol_layer_send_message(
    uint8_t port,
    uint8_t message_type,
    uint8_t* message,
    uint16_t size,
    bool first_message_of_ams);
void protocol_layer_send_hard_reset(uint8_t port);
bool protocol_layer_start_bist_carrier_mode(uint8_t port);
bool protocol_layer_stop_bist_carrier_mode(uint8_t port);
void protocol_layer_reset(uint8_t port);
void protocol_layer_disable_receive(uint8_t port);
ProtocolLayerChunkedReceive protocol_layer_receive_chunks(uint8_t port, uint8_t slot);

void protocol_layer_on_message_received(uint8_t port);
void protocol_layer_on_hard_reset_received(uint8_t port);
void protocol_layer_on_hard_reset_sent(uint8_t port);
void protocol_layer_on_transmit_completed(uint8_t port, bool sent);

#endif
