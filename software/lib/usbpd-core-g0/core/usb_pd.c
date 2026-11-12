#include "usb_pd.h"
#include "usb_pd_platform.h"
#include "policy_engine/policy_engine.h"
#include "protocol_layer/protocol_layer.h"
#include "type_c_connection/type_c_connection.h"

typedef struct {
    const UsbPdCallbacks* callbacks;
    void* context;
    bool port_initialized[USB_PD_PORT_COUNT];
} UsbPd;

static UsbPd usb_pd;

static void
    usb_pd_on_type_c_event(void* context, uint8_t port, const TypeCConnectionEvent* event) {
    UsbPdEvent pd_event;

    (void)context;
    if(event->type == TypeCConnectionEventTypeAttached) {
        policy_engine_attach(port, event->power_role, event->pin);
        pd_event.type = UsbPdEventTypeAttached;
        pd_event.attach.power_role = event->power_role;
        pd_event.attach.pin = event->pin;
        pd_event.attach.partner_rp_current = event->partner_rp_current;
    } else {
        policy_engine_detach(port);
        pd_event.type = UsbPdEventTypeDetached;
    }
    usb_pd.callbacks->on_event(usb_pd.context, port, &pd_event);
}

void usb_pd_init(const UsbPdCallbacks* callbacks, void* context) {
    uint8_t port;

    usb_pd.callbacks = callbacks;
    usb_pd.context = context;
    for(port = 0u; port < USB_PD_PORT_COUNT; port++) {
        usb_pd.port_initialized[port] = false;
    }
}

bool usb_pd_port_init(uint8_t port, const UsbPdPortConfig* config) {
    if((port >= USB_PD_PORT_COUNT) || (usb_pd.callbacks == NULL)) {
        return false;
    }
    protocol_layer_init(port);
    policy_engine_init(port, config, usb_pd.callbacks, usb_pd.context);
    type_c_connection_init(port, config->mode, config->rp_current, usb_pd_on_type_c_event, NULL);
    usb_pd.port_initialized[port] = true;
    return true;
}

uint32_t usb_pd_process(void) {
    uint32_t next_call_ms = UINT32_MAX;
    uint8_t port;

    for(port = 0u; port < USB_PD_PORT_COUNT; port++) {
        uint32_t port_next_call_ms;

        if(!usb_pd.port_initialized[port]) {
            continue;
        }
        port_next_call_ms = type_c_connection_process(port);
        if(port_next_call_ms < next_call_ms) {
            next_call_ms = port_next_call_ms;
        }
        port_next_call_ms = policy_engine_process(port);
        if(port_next_call_ms < next_call_ms) {
            next_call_ms = port_next_call_ms;
        }
    }
    return next_call_ms;
}

void usb_pd_set_mode(uint8_t port, UsbPdPortMode mode) {
    type_c_connection_set_mode(port, mode);
    usb_pd.callbacks->wakeup(usb_pd.context);
}

void usb_pd_set_rp_current(uint8_t port, UsbPdRpCurrent current) {
    type_c_connection_set_rp_current(port, current);
    usb_pd.callbacks->wakeup(usb_pd.context);
}

void usb_pd_enter_error_recovery(uint8_t port) {
    type_c_connection_enter_error_recovery(port);
    usb_pd.callbacks->wakeup(usb_pd.context);
}

void usb_pd_power_ready(uint8_t port) {
    policy_engine_power_ready(port);
}

bool usb_pd_request(uint8_t port, UsbPdRequest request) {
    return policy_engine_request(port, request);
}

bool usb_pd_request_power(uint8_t port, uint32_t rdo) {
    return policy_engine_request_power(port, rdo);
}

bool usb_pd_send_source_capabilities(uint8_t port) {
    return policy_engine_send_source_capabilities(port);
}

bool usb_pd_send_hard_reset(uint8_t port) {
    return policy_engine_send_hard_reset_request(port);
}

bool usb_pd_signal_fast_role_swap(uint8_t port) {
    return policy_engine_signal_fast_role_swap(port);
}

void usb_pd_get_port_status(uint8_t port, UsbPdPortStatus* status) {
    policy_engine_get_port_status(port, status);
}

void usb_pd_on_cc_changed(uint8_t port) {
    (void)port;
    usb_pd.callbacks->wakeup(usb_pd.context);
}

void usb_pd_on_vbus_changed(uint8_t port) {
    (void)port;
    usb_pd.callbacks->wakeup(usb_pd.context);
}

void usb_pd_on_message_received(uint8_t port) {
    protocol_layer_on_message_received(port);
}

void usb_pd_on_hard_reset_received(uint8_t port) {
    protocol_layer_on_hard_reset_received(port);
}

void usb_pd_on_hard_reset_sent(uint8_t port) {
    protocol_layer_on_hard_reset_sent(port);
}

void usb_pd_on_transmit_completed(uint8_t port, bool sent) {
    protocol_layer_on_transmit_completed(port, sent);
}

bool usb_pd_on_fast_role_swap_received(uint8_t port) {
    return policy_engine_on_fast_role_swap_received(port);
}
