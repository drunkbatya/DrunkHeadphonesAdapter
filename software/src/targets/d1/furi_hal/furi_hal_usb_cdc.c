#include <furi_hal_usb_cdc.h>
#include <furi_hal_usb_i.h>

#include <furi.h>

#include <tusb.h>

#include <string.h>

_Static_assert(CDC_DATA_SZ == CFG_TUD_CDC_EP_BUFSIZE, "CDC_DATA_SZ != CFG_TUD_CDC_EP_BUFSIZE");

typedef struct {
    CdcCallbacks* callbacks;
    void* callbacks_context;
    bool connected;
    uint8_t rx_packet[CDC_DATA_SZ];
    uint16_t rx_length;
    bool rx_ready;
    uint8_t tx_packet[CDC_DATA_SZ];
    uint16_t tx_length;
    bool tx_pending;
} FuriHalUsbCdc;

static FuriHalUsbCdc usb_cdc = {0};

static CdcCallbacks* furi_hal_usb_cdc_get_callbacks(void** context) {
    FURI_CRITICAL_ENTER();
    CdcCallbacks* callbacks = usb_cdc.callbacks;
    *context = usb_cdc.callbacks_context;
    FURI_CRITICAL_EXIT();

    return callbacks;
}

static bool furi_hal_usb_cdc_is_connected(void) {
    FURI_CRITICAL_ENTER();
    const bool connected = usb_cdc.connected;
    FURI_CRITICAL_EXIT();

    return connected;
}

static bool furi_hal_usb_cdc_is_rx_ready(void) {
    FURI_CRITICAL_ENTER();
    const bool rx_ready = usb_cdc.rx_ready;
    FURI_CRITICAL_EXIT();

    return rx_ready;
}

static bool furi_hal_usb_cdc_is_tx_pending(void) {
    FURI_CRITICAL_ENTER();
    const bool tx_pending = usb_cdc.tx_pending;
    FURI_CRITICAL_EXIT();

    return tx_pending;
}

static void furi_hal_usb_cdc_notify_state(CdcState state) {
    void* context;
    CdcCallbacks* callbacks = furi_hal_usb_cdc_get_callbacks(&context);
    if(callbacks != NULL && callbacks->state_callback != NULL) {
        callbacks->state_callback(context, state);
    }
}

static void furi_hal_usb_cdc_notify_rx(void) {
    void* context;
    CdcCallbacks* callbacks = furi_hal_usb_cdc_get_callbacks(&context);
    if(callbacks != NULL && callbacks->rx_ep_callback != NULL) {
        callbacks->rx_ep_callback(context);
    }
}

static void furi_hal_usb_cdc_notify_tx(void) {
    void* context;
    CdcCallbacks* callbacks = furi_hal_usb_cdc_get_callbacks(&context);
    if(callbacks != NULL && callbacks->tx_ep_callback != NULL) {
        callbacks->tx_ep_callback(context);
    }
}

void furi_hal_usb_cdc_mount(void) {
    FURI_CRITICAL_ENTER();
    usb_cdc.connected = true;
    FURI_CRITICAL_EXIT();

    furi_hal_usb_cdc_notify_state(CdcStateConnected);
}

void furi_hal_usb_cdc_reset(void) {
    FURI_CRITICAL_ENTER();
    const bool connected = usb_cdc.connected;
    usb_cdc.connected = false;
    usb_cdc.rx_ready = false;
    usb_cdc.tx_pending = false;
    FURI_CRITICAL_EXIT();

    if(connected) {
        furi_hal_usb_cdc_notify_state(CdcStateDisconnected);
    }
}

void furi_hal_cdc_set_callbacks(CdcCallbacks* cb, void* context) {
    void* previous_context;
    CdcCallbacks* previous_callbacks = furi_hal_usb_cdc_get_callbacks(&previous_context);
    const bool connected = furi_hal_usb_cdc_is_connected();

    if(previous_callbacks != NULL) {
        if(previous_callbacks->state_callback != NULL) {
            if(connected == true)
                previous_callbacks->state_callback(previous_context, CdcStateDisconnected);
        }
    }

    FURI_CRITICAL_ENTER();
    usb_cdc.callbacks = cb;
    usb_cdc.callbacks_context = context;
    FURI_CRITICAL_EXIT();

    if(cb != NULL) {
        if(cb->state_callback != NULL) {
            if(connected == true) cb->state_callback(context, CdcStateConnected);
        }
    }
}

void furi_hal_cdc_send(uint8_t* buf, uint16_t len) {
    furi_check(buf);
    furi_check(len <= CDC_DATA_SZ);

    bool queued = false;

    FURI_CRITICAL_ENTER();
    if(usb_cdc.connected) {
        furi_check(!usb_cdc.tx_pending);
        memcpy(usb_cdc.tx_packet, buf, len);
        usb_cdc.tx_length = len;
        usb_cdc.tx_pending = true;
        queued = true;
    }
    FURI_CRITICAL_EXIT();

    if(queued) {
        furi_hal_usb_cdc_request_tx();
    }
}

int32_t furi_hal_cdc_receive(uint8_t* buf, uint16_t max_len) {
    furi_check(buf);
    furi_check(max_len >= CDC_DATA_SZ);

    int32_t len = 0;

    FURI_CRITICAL_ENTER();
    if(usb_cdc.rx_ready) {
        memcpy(buf, usb_cdc.rx_packet, usb_cdc.rx_length);
        len = usb_cdc.rx_length;
        usb_cdc.rx_ready = false;
    }
    FURI_CRITICAL_EXIT();

    if(len > 0) {
        furi_hal_usb_cdc_request_rx();
    }
    return len;
}

void furi_hal_usb_cdc_process_rx(void) {
    if(furi_hal_usb_cdc_is_rx_ready() || tud_cdc_available() == 0) {
        return;
    }

    const uint16_t rx_length = tud_cdc_read(usb_cdc.rx_packet, sizeof(usb_cdc.rx_packet));

    FURI_CRITICAL_ENTER();
    usb_cdc.rx_length = rx_length;
    usb_cdc.rx_ready = true;
    FURI_CRITICAL_EXIT();

    furi_hal_usb_cdc_notify_rx();
}

void furi_hal_usb_cdc_process_tx(void) {
    if(!furi_hal_usb_cdc_is_tx_pending() || tud_cdc_write_available() < usb_cdc.tx_length) {
        return;
    }

    furi_check(tud_cdc_write(usb_cdc.tx_packet, usb_cdc.tx_length) == usb_cdc.tx_length);
    tud_cdc_write_flush();

    FURI_CRITICAL_ENTER();
    usb_cdc.tx_pending = false;
    FURI_CRITICAL_EXIT();

    furi_hal_usb_cdc_notify_tx();
}

void tud_cdc_rx_cb(uint8_t itf) {
    UNUSED(itf);
    furi_hal_usb_cdc_process_rx();
}

void tud_cdc_tx_complete_cb(uint8_t itf) {
    UNUSED(itf);
    furi_hal_usb_cdc_process_tx();
}
