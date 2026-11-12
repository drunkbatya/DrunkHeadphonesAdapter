#include "protocol_layer/protocol_layer_i.h"

ProtocolLayer protocol_layers[USB_PD_PORT_COUNT];

bool protocol_layer_is_deadline_reached(uint32_t deadline) {
    return (int32_t)(usb_pd_platform_get_tick_ms() - deadline) >= 0;
}

void protocol_layer_reset_message_ids(ProtocolLayer* prl) {
    prl->tx_message_id = 0u;
    prl->rx_message_id = PROTOCOL_LAYER_MESSAGE_ID_NONE;
}

void protocol_layer_clear_transfers(ProtocolLayer* prl) {
    prl->tx_state = ProtocolLayerTxStateIdle;
    prl->tx_type = ProtocolLayerTxTypeNone;
    prl->goodcrc_in_flight = false;
    prl->tx_chunking.active = false;
    prl->tx_chunking.discarded = false;
    prl->rx_chunking.state = ProtocolLayerRxChunkStateIdle;
    prl->rx_pending.kind = ProtocolLayerRxKindIgnored;
    prl->rx_ongoing = 0u;
}

void protocol_layer_init(uint8_t port) {
    ProtocolLayer* prl = &protocol_layers[port];

    memset(prl, 0, sizeof(ProtocolLayer));
    protocol_layer_reset_message_ids(prl);
    usb_pd_platform_set_receive_buffer(port, prl->rx_buffer, sizeof(prl->rx_buffer));
}

void protocol_layer_set_header(
    uint8_t port,
    UsbPdPowerRole power_role,
    UsbPdDataRole data_role,
    UsbPdSpecRevision spec_revision) {
    ProtocolLayerMessageHeader* header = &protocol_layers[port].tx_header;

    header->b.port_power_role_or_cable_plug = power_role;
    header->b.port_data_role = data_role;
    header->b.spec_revision = spec_revision;
}

void protocol_layer_set_header_power_role(uint8_t port, UsbPdPowerRole power_role) {
    protocol_layers[port].tx_header.b.port_power_role_or_cable_plug = power_role;
}

void protocol_layer_set_header_data_role(uint8_t port, UsbPdDataRole data_role) {
    protocol_layers[port].tx_header.b.port_data_role = data_role;
}

void protocol_layer_set_header_spec_revision(uint8_t port, UsbPdSpecRevision spec_revision) {
    protocol_layers[port].tx_header.b.spec_revision = spec_revision;
}

void protocol_layer_source_set_sink_tx_ng(uint8_t port) {
    usb_pd_platform_set_sink_tx_ok(port, false);
}

void protocol_layer_source_set_sink_tx_ok(uint8_t port) {
    ProtocolLayer* prl = &protocol_layers[port];

    usb_pd_platform_set_sink_tx_ok(port, true);
    prl->tx_state = ProtocolLayerTxStateIdle;
    prl->tx_chunking.active = false;
}

bool protocol_layer_is_sink_tx_ok(uint8_t port) {
    return usb_pd_platform_is_sink_tx_ok(port);
}

void protocol_layer_send_hard_reset(uint8_t port) {
    ProtocolLayer* prl = &protocol_layers[port];
    uint32_t reset_deadline;

    prl->reset_completed = 0u;
    protocol_layer_reset_message_ids(prl);
    protocol_layer_clear_transfers(prl);
    protocol_layer_disable_receive(port);
    usb_pd_platform_send_hard_reset(port);
    reset_deadline = usb_pd_platform_get_tick_ms() + PROTOCOL_LAYER_HARD_RESET_TIMEOUT_MS;
    while((prl->reset_completed != 1u) && !protocol_layer_is_deadline_reached(reset_deadline)) {
    }
}

bool protocol_layer_start_bist_carrier_mode(uint8_t port) {
    return usb_pd_platform_start_bist_carrier(port);
}

bool protocol_layer_stop_bist_carrier_mode(uint8_t port) {
    return usb_pd_platform_stop_bist_carrier(port);
}

void protocol_layer_reset(uint8_t port) {
    ProtocolLayer* prl = &protocol_layers[port];

    protocol_layer_clear_transfers(prl);
    protocol_layer_reset_message_ids(prl);
    usb_pd_platform_enable_receive(port);
}

void protocol_layer_disable_receive(uint8_t port) {
    usb_pd_platform_disable_receive(port);
}

void protocol_layer_on_hard_reset_received(uint8_t port) {
    ProtocolLayer* prl = &protocol_layers[port];

    protocol_layer_reset_message_ids(prl);
    protocol_layer_clear_transfers(prl);
    protocol_layer_disable_receive(port);
    policy_engine_on_hard_reset_received(port);
}

void protocol_layer_on_hard_reset_sent(uint8_t port) {
    protocol_layers[port].reset_completed = 1u;
}
