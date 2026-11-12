#include "policy_engine/policy_engine_i.h"

#define POLICY_ENGINE_RX_SLOT_ALLOCATED 0x20u
#define POLICY_ENGINE_RX_SLOT_COMPLETE  0x40u
#define POLICY_ENGINE_RX_SLOT_CHUNKED   0x80u

static void receive_queue_open(PolicyEngine* pe, uint8_t slot) {
    pe->rx_message = pe->rx_buffers[slot];
    pe->rx_header = (uint16_t)(pe->rx_message[0] | (pe->rx_message[1] << 8));
}

PolicyEngineRxEvent policy_engine_receive_queue_get(PolicyEngine* pe) {
    uint32_t interrupts = usb_pd_platform_disable_interrupts();
    uint8_t slot;
    uint8_t slot_flags;

    if(pe->rx_event_slot == 0u) {
        usb_pd_platform_restore_interrupts(interrupts);
        return PolicyEngineRxEventNone;
    }
    slot = (uint8_t)(pe->rx_event_slot - 1u);
    slot_flags = pe->rx_slot_flags[slot];
    usb_pd_platform_restore_interrupts(interrupts);

    if((slot_flags & POLICY_ENGINE_RX_SLOT_COMPLETE) == 0u) {
        if((slot_flags & POLICY_ENGINE_RX_SLOT_CHUNKED) == 0u) {
            return PolicyEngineRxEventBusy;
        }
        switch(protocol_layer_receive_chunks(pe->port, slot)) {
        case ProtocolLayerChunkedReceivePending:
            return PolicyEngineRxEventBusy;
        case ProtocolLayerChunkedReceiveAborted:
            policy_engine_receive_queue_release(pe);
            return PolicyEngineRxEventNone;
        default:
            break;
        }
        interrupts = usb_pd_platform_disable_interrupts();
        pe->rx_slot_flags[slot] |= POLICY_ENGINE_RX_SLOT_COMPLETE;
        usb_pd_platform_restore_interrupts(interrupts);
    }
    receive_queue_open(pe, slot);
    return PolicyEngineRxEventMessage;
}

void policy_engine_receive_queue_release(PolicyEngine* pe) {
    uint32_t interrupts = usb_pd_platform_disable_interrupts();

    if(pe->rx_event_slot != 0u) {
        uint8_t slot = (uint8_t)(pe->rx_event_slot - 1u);
        uint8_t other_slot = (slot == 0u) ? 1u : 0u;

        pe->rx_slot_flags[slot] = 0u;
        if((pe->rx_slot_flags[other_slot] &
            (POLICY_ENGINE_RX_SLOT_COMPLETE | POLICY_ENGINE_RX_SLOT_CHUNKED)) != 0u) {
            pe->rx_event_slot = other_slot + 1u;
            usb_pd_platform_restore_interrupts(interrupts);
            policy_engine_wakeup(pe->port);
            return;
        }
        pe->rx_event_slot = 0u;
    }
    usb_pd_platform_restore_interrupts(interrupts);
}

bool policy_engine_receive_queue_has_pending(uint8_t port) {
    PolicyEngine* pe = &policy_engines[port];
    bool rx_event_pending = false;
    uint32_t interrupts = usb_pd_platform_disable_interrupts();

    if((pe->rx_event_slot != 0u) &&
       ((pe->rx_slot_flags[pe->rx_event_slot - 1u] & POLICY_ENGINE_RX_SLOT_CHUNKED) == 0u)) {
        rx_event_pending = true;
    }
    usb_pd_platform_restore_interrupts(interrupts);
    return rx_event_pending;
}

bool policy_engine_receive_queue_allocate(uint8_t port, uint8_t* slot) {
    PolicyEngine* pe = &policy_engines[port];
    uint8_t index;

    for(index = 0u; index < sizeof(pe->rx_slot_flags); index++) {
        if(pe->rx_slot_flags[index] == 0u) {
            pe->rx_slot_flags[index] = POLICY_ENGINE_RX_SLOT_ALLOCATED;
            *slot = index;
            return true;
        }
    }
    return false;
}

void policy_engine_receive_queue_store(uint8_t port, uint8_t slot, const uint8_t* message) {
    memcpy(
        policy_engines[port].rx_buffers[slot],
        message,
        sizeof(policy_engines[port].rx_buffers[slot]));
}

void policy_engine_receive_queue_store_chunk(
    uint8_t port,
    uint8_t slot,
    uint16_t offset,
    const uint8_t* data,
    uint16_t size) {
    uint8_t* buffer = policy_engines[port].rx_buffers[slot];

    if((offset > USB_PD_MESSAGE_BUFFER_SIZE) || (size > (USB_PD_MESSAGE_BUFFER_SIZE - offset))) {
        return;
    }
    memcpy(&buffer[offset], data, size);
}

void policy_engine_receive_queue_commit(uint8_t port, uint8_t slot, bool chunked) {
    PolicyEngine* pe = &policy_engines[port];

    if((pe->rx_slot_flags[slot] & POLICY_ENGINE_RX_SLOT_ALLOCATED) == 0u) {
        return;
    }
    pe->rx_slot_flags[slot] |= chunked ? POLICY_ENGINE_RX_SLOT_CHUNKED :
                                         POLICY_ENGINE_RX_SLOT_COMPLETE;
    if(pe->rx_event_slot == 0u) {
        pe->rx_event_slot = slot + 1u;
    }
    policy_engine_wakeup(port);
}

void policy_engine_receive_queue_free(uint8_t port, uint8_t slot) {
    policy_engines[port].rx_slot_flags[slot] = 0u;
}
