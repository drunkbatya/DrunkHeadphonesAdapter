#ifndef POLICY_ENGINE_H_
#define POLICY_ENGINE_H_

#include <stdbool.h>
#include <stdint.h>

#include "usb_pd.h"

void policy_engine_init(
    uint8_t port,
    const UsbPdPortConfig* config,
    const UsbPdCallbacks* callbacks,
    void* context);
void policy_engine_attach(uint8_t port, UsbPdPowerRole power_role, UsbPdCcPin pin);
void policy_engine_detach(uint8_t port);
uint32_t policy_engine_process(uint8_t port);

void policy_engine_power_ready(uint8_t port);
bool policy_engine_request(uint8_t port, UsbPdRequest request);
bool policy_engine_request_power(uint8_t port, uint32_t rdo);
bool policy_engine_send_source_capabilities(uint8_t port);
bool policy_engine_send_hard_reset_request(uint8_t port);
bool policy_engine_signal_fast_role_swap(uint8_t port);
bool policy_engine_on_fast_role_swap_received(uint8_t port);
void policy_engine_get_port_status(uint8_t port, UsbPdPortStatus* status);

bool policy_engine_receive_queue_has_pending(uint8_t port);
bool policy_engine_is_connected(uint8_t port);
void policy_engine_wakeup(uint8_t port);
void policy_engine_on_hard_reset_received(uint8_t port);
bool policy_engine_receive_queue_allocate(uint8_t port, uint8_t* slot);
void policy_engine_receive_queue_store(uint8_t port, uint8_t slot, const uint8_t* message);
void policy_engine_receive_queue_store_chunk(
    uint8_t port,
    uint8_t slot,
    uint16_t offset,
    const uint8_t* data,
    uint16_t size);
void policy_engine_receive_queue_commit(uint8_t port, uint8_t slot, bool chunked);
void policy_engine_receive_queue_free(uint8_t port, uint8_t slot);

#endif
