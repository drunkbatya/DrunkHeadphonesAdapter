#pragma once

#include <stdint.h>

#define CDC_DATA_SZ 64

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    CdcStateDisconnected,
    CdcStateConnected,
} CdcState;

typedef struct {
    void (*tx_ep_callback)(void* context);
    void (*rx_ep_callback)(void* context);
    void (*state_callback)(void* context, CdcState state);
} CdcCallbacks;

void furi_hal_cdc_set_callbacks(CdcCallbacks* cb, void* context);

void furi_hal_cdc_send(uint8_t* buf, uint16_t len);

int32_t furi_hal_cdc_receive(uint8_t* buf, uint16_t max_len);

#ifdef __cplusplus
}
#endif
