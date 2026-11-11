#include <furi_hal_usbpd.h>
#include <furi_hal_bus.h>
#include <furi_hal_gpio.h>
#include <furi_hal_interrupt.h>
#include <furi_hal_resources.h>

#include <stm32g0xx_ll_dma.h>
#include <stm32g0xx_ll_ucpd.h>
#include <furi.h>

typedef struct {
    UCPD_TypeDef* ucpd;
    FuriHalBus bus;
    FuriHalInterruptId interrupt_id;
    uint32_t strobe;
    uint32_t dma_rx_channel;
    uint32_t dma_rx_request;
    uint32_t dma_tx_channel;
    uint32_t dma_tx_request;
    const GpioPin* gpio_frstx;
} FuriHalUsbPdConfig;

typedef struct {
    FuriHalUsbPdCallback callback;
    void* context;
    uint8_t* rx_buffer;
    size_t rx_buffer_size;
    bool communicating;
    FuriHalUsbPdCcPin pin;
    volatile bool rx_ongoing;
    bool rx_overrun;
} FuriHalUsbPd;

static const FuriHalUsbPdConfig furi_hal_usbpd_config[FuriHalUsbPdIdMax] = {
    [FuriHalUsbPdIdPhone] =
        {
            .ucpd = UCPD1,
            .bus = FuriHalBusUCPD1,
            .interrupt_id = FuriHalInterruptIdUcpd1,
            .strobe = SYSCFG_CFGR1_UCPD1_STROBE,
            .dma_rx_channel = LL_DMA_CHANNEL_3,
            .dma_rx_request = LL_DMAMUX_REQ_UCPD1_RX,
            .dma_tx_channel = LL_DMA_CHANNEL_4,
            .dma_tx_request = LL_DMAMUX_REQ_UCPD1_TX,
            .gpio_frstx = &gpio_ucpd_phone_frstx,
        },
    [FuriHalUsbPdIdCharger] =
        {
            .ucpd = UCPD2,
            .bus = FuriHalBusUCPD2,
            .interrupt_id = FuriHalInterruptIdUcpd2,
            .strobe = SYSCFG_CFGR1_UCPD2_STROBE,
            .dma_rx_channel = LL_DMA_CHANNEL_5,
            .dma_rx_request = LL_DMAMUX_REQ_UCPD2_RX,
            .dma_tx_channel = LL_DMA_CHANNEL_6,
            .dma_tx_request = LL_DMAMUX_REQ_UCPD2_TX,
            .gpio_frstx = NULL,
        },
};

static FuriHalUsbPd furi_hal_usbpd[FuriHalUsbPdIdMax] = {0};

static void furi_hal_usbpd_isr(void* context) {
    UNUSED(context);
}

void furi_hal_usbpd_init(void) {
}

void furi_hal_usbpd_start(FuriHalUsbPdId id, FuriHalUsbPdCallback callback, void* context) {
    UNUSED(id);
    UNUSED(callback);
    UNUSED(context);
    UNUSED(furi_hal_usbpd_config);
    UNUSED(furi_hal_usbpd);
    UNUSED(furi_hal_usbpd_isr);
}

void furi_hal_usbpd_stop(FuriHalUsbPdId id) {
    UNUSED(id);
}

void furi_hal_usbpd_set_rp(FuriHalUsbPdId id, FuriHalUsbPdRp rp) {
    UNUSED(id);
    UNUSED(rp);
}

void furi_hal_usbpd_set_rd(FuriHalUsbPdId id) {
    UNUSED(id);
}

void furi_hal_usbpd_set_cc_open(FuriHalUsbPdId id) {
    UNUSED(id);
}

void furi_hal_usbpd_get_cc(FuriHalUsbPdId id, FuriHalUsbPdCcState* cc1, FuriHalUsbPdCcState* cc2) {
    UNUSED(id);
    UNUSED(cc1);
    UNUSED(cc2);
}

void furi_hal_usbpd_set_receive_buffer(FuriHalUsbPdId id, uint8_t* buffer, size_t size) {
    UNUSED(id);
    UNUSED(buffer);
    UNUSED(size);
}

void furi_hal_usbpd_start_communication(FuriHalUsbPdId id, FuriHalUsbPdCcPin pin) {
    UNUSED(id);
    UNUSED(pin);
}

void furi_hal_usbpd_stop_communication(FuriHalUsbPdId id) {
    UNUSED(id);
}

void furi_hal_usbpd_enable_receive(FuriHalUsbPdId id) {
    UNUSED(id);
}

void furi_hal_usbpd_disable_receive(FuriHalUsbPdId id) {
    UNUSED(id);
}

bool furi_hal_usbpd_send_message(FuriHalUsbPdId id, const uint8_t* data, size_t size) {
    UNUSED(id);
    UNUSED(data);
    UNUSED(size);
    return false;
}

void furi_hal_usbpd_send_hard_reset(FuriHalUsbPdId id) {
    UNUSED(id);
}

void furi_hal_usbpd_start_bist_carrier(FuriHalUsbPdId id) {
    UNUSED(id);
}

void furi_hal_usbpd_stop_bist_carrier(FuriHalUsbPdId id) {
    UNUSED(id);
}

void furi_hal_usbpd_set_sink_tx_ok(FuriHalUsbPdId id, bool ok) {
    UNUSED(id);
    UNUSED(ok);
}

bool furi_hal_usbpd_is_sink_tx_ok(FuriHalUsbPdId id) {
    UNUSED(id);
    return false;
}

void furi_hal_usbpd_send_fast_role_swap(FuriHalUsbPdId id) {
    UNUSED(id);
}

void furi_hal_usbpd_set_fast_role_swap_detection(FuriHalUsbPdId id, bool enabled) {
    UNUSED(id);
    UNUSED(enabled);
}
