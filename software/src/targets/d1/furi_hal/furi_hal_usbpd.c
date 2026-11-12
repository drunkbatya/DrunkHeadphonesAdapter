#include <furi_hal_usbpd.h>
#include <furi_hal_bus.h>
#include <furi_hal_cortex.h>
#include <furi_hal_gpio.h>
#include <furi_hal_interrupt.h>
#include <furi_hal_power.h>
#include <furi_hal_resources.h>

#include <stm32g0xx_ll_dma.h>
#include <stm32g0xx_ll_ucpd.h>
#include <furi.h>

#define FURI_HAL_USBPD_DMA_INSTANCE     (DMA1)
#define FURI_HAL_USBPD_FRS_CONFIRM_US   150
#define FURI_HAL_USBPD_SET_RD_SETTLE_US 1000
#define FURI_HAL_USBPD_ATTACHED_INTERRUPT_MASK                                                 \
    (UCPD_IMR_TXMSGDISCIE | UCPD_IMR_TXMSGSENTIE | UCPD_IMR_HRSTDISCIE | UCPD_IMR_HRSTSENTIE | \
     UCPD_IMR_TXMSGABTIE | UCPD_IMR_TXUNDIE | UCPD_IMR_RXORDDETIE | UCPD_IMR_RXHRSTDETIE |     \
     UCPD_IMR_RXOVRIE | UCPD_IMR_RXMSGENDIE)

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
    bool started;
    bool communicating;
    FuriHalUsbPdCcPin pin;
    volatile bool receiving;
    volatile bool transmitting;
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

static const uint32_t furi_hal_usbpd_rp_resistor[] = {
    [FuriHalUsbPdRpDefault] = LL_UCPD_RESISTOR_DEFAULT,
    [FuriHalUsbPdRp1A5] = LL_UCPD_RESISTOR_1_5A,
    [FuriHalUsbPdRp3A0] = LL_UCPD_RESISTOR_3_0A,
};

static FuriHalUsbPd furi_hal_usbpd[FuriHalUsbPdIdMax] = {0};

static void furi_hal_usbpd_set_busy(volatile bool* busy, bool value) {
    FURI_CRITICAL_ENTER();
    if(*busy != value) {
        *busy = value;
        if(value) {
            furi_hal_power_insomnia_enter();
        } else {
            furi_hal_power_insomnia_exit();
        }
    }
    FURI_CRITICAL_EXIT();
}

static void furi_hal_usbpd_dma_disable(uint32_t channel) {
    LL_DMA_DisableChannel(FURI_HAL_USBPD_DMA_INSTANCE, channel);
    while(LL_DMA_IsEnabledChannel(FURI_HAL_USBPD_DMA_INSTANCE, channel)) {
    }
}

static void furi_hal_usbpd_dma_arm(uint32_t channel, const uint8_t* buffer, size_t size) {
    furi_hal_usbpd_dma_disable(channel);
    LL_DMA_SetMemoryAddress(FURI_HAL_USBPD_DMA_INSTANCE, channel, (uint32_t)buffer);
    LL_DMA_SetDataLength(FURI_HAL_USBPD_DMA_INSTANCE, channel, size);
    LL_DMA_EnableChannel(FURI_HAL_USBPD_DMA_INSTANCE, channel);
}

static void furi_hal_usbpd_dma_init(
    uint32_t channel,
    uint32_t request,
    uint32_t direction,
    uint32_t priority,
    uint32_t peripheral_address) {
    LL_DMA_InitTypeDef dma_init;

    LL_DMA_StructInit(&dma_init);
    dma_init.Direction = direction;
    dma_init.Mode = LL_DMA_MODE_NORMAL;
    dma_init.PeriphOrM2MSrcIncMode = LL_DMA_PERIPH_NOINCREMENT;
    dma_init.MemoryOrM2MDstIncMode = LL_DMA_MEMORY_INCREMENT;
    dma_init.PeriphOrM2MSrcDataSize = LL_DMA_PDATAALIGN_BYTE;
    dma_init.MemoryOrM2MDstDataSize = LL_DMA_MDATAALIGN_BYTE;
    dma_init.NbData = 0;
    dma_init.MemoryOrM2MDstAddress = 0;
    dma_init.PeriphOrM2MSrcAddress = peripheral_address;
    dma_init.PeriphRequest = request;
    dma_init.Priority = priority;
    furi_hal_usbpd_dma_disable(channel);
    furi_check(LL_DMA_Init(FURI_HAL_USBPD_DMA_INSTANCE, channel, &dma_init) == SUCCESS);
}

static void furi_hal_usbpd_strobe(FuriHalUsbPdId id) {
    SET_BIT(SYSCFG->CFGR1, furi_hal_usbpd_config[id].strobe);
}

static void furi_hal_usbpd_apply_cc_enable(FuriHalUsbPdId id) {
    UCPD_TypeDef* ucpd = furi_hal_usbpd_config[id].ucpd;
    const FuriHalUsbPd* port = &furi_hal_usbpd[id];

    if(!port->communicating) {
        LL_UCPD_SetccEnable(ucpd, LL_UCPD_CCENABLE_CC1CC2);
    } else {
        LL_UCPD_SetccEnable(
            ucpd, (port->pin == FuriHalUsbPdCcPin1) ? LL_UCPD_CCENABLE_CC1 : LL_UCPD_CCENABLE_CC2);
    }
}

static FuriHalUsbPdCcState furi_hal_usbpd_decode_cc(uint32_t control, uint32_t vstate) {
    if(control & UCPD_CR_ANAMODE) {
        switch(vstate) {
        case 1:
            return FuriHalUsbPdCcStateRpDefault;
        case 2:
            return FuriHalUsbPdCcStateRp1A5;
        case 3:
            return FuriHalUsbPdCcStateRp3A0;
        default:
            return FuriHalUsbPdCcStateOpen;
        }
    }
    if((control & UCPD_CR_ANASUBMODE) == 0) {
        return FuriHalUsbPdCcStateOpen;
    }
    switch(vstate) {
    case 0:
        return FuriHalUsbPdCcStateRa;
    case 1:
        return FuriHalUsbPdCcStateRd;
    default:
        return FuriHalUsbPdCcStateOpen;
    }
}

static void furi_hal_usbpd_notify(FuriHalUsbPdId id, FuriHalUsbPdEvent event) {
    furi_hal_usbpd[id].callback(event, furi_hal_usbpd[id].context);
}

static void furi_hal_usbpd_transmit_done(FuriHalUsbPdId id) {
    furi_hal_usbpd_dma_disable(furi_hal_usbpd_config[id].dma_tx_channel);
    furi_hal_usbpd_set_busy(&furi_hal_usbpd[id].transmitting, false);
}

static void furi_hal_usbpd_isr(void* context) {
    const FuriHalUsbPdId id = (FuriHalUsbPdId)(uintptr_t)context;
    const FuriHalUsbPdConfig* config = &furi_hal_usbpd_config[id];
    FuriHalUsbPd* port = &furi_hal_usbpd[id];
    UCPD_TypeDef* ucpd = config->ucpd;
    const uint32_t status = READ_REG(ucpd->SR);

    if((ucpd->IMR & status) == 0) {
        return;
    }
    if(status & UCPD_SR_TXMSGDISC) {
        LL_UCPD_ClearFlag_TxMSGDISC(ucpd);
        furi_hal_usbpd_transmit_done(id);
        furi_hal_usbpd_notify(id, FuriHalUsbPdEventTransmitFailed);
        return;
    }
    if(status & UCPD_SR_TXMSGSENT) {
        LL_UCPD_ClearFlag_TxMSGSENT(ucpd);
        furi_hal_usbpd_transmit_done(id);
        furi_hal_usbpd_notify(id, FuriHalUsbPdEventTransmitSent);
        return;
    }
    if(status & UCPD_SR_TXMSGABT) {
        LL_UCPD_ClearFlag_TxMSGABT(ucpd);
        furi_hal_usbpd_transmit_done(id);
        furi_hal_usbpd_notify(id, FuriHalUsbPdEventTransmitFailed);
        return;
    }
    if(status & UCPD_SR_HRSTDISC) {
        LL_UCPD_ClearFlag_TxHRSTDISC(ucpd);
        furi_hal_usbpd_set_busy(&port->transmitting, false);
        return;
    }
    if(status & UCPD_SR_HRSTSENT) {
        LL_UCPD_ClearFlag_TxHRSTSENT(ucpd);
        furi_hal_usbpd_set_busy(&port->transmitting, false);
        furi_hal_usbpd_notify(id, FuriHalUsbPdEventHardResetSent);
        return;
    }
    if(status & UCPD_SR_TXUND) {
        LL_UCPD_ClearFlag_TxUND(ucpd);
        return;
    }
    if(status & UCPD_SR_RXORDDET) {
        LL_UCPD_ClearFlag_RxOrderSet(ucpd);
        furi_hal_usbpd_set_busy(&port->receiving, true);
        return;
    }
    if(status & UCPD_SR_RXHRSTDET) {
        furi_hal_usbpd_set_busy(&port->receiving, false);
        furi_hal_usbpd_notify(id, FuriHalUsbPdEventHardResetReceived);
        LL_UCPD_ClearFlag_RxHRST(ucpd);
        return;
    }
    if(status & UCPD_SR_RXOVR) {
        port->rx_overrun = true;
        LL_UCPD_ClearFlag_RxOvr(ucpd);
        return;
    }
    if(status & UCPD_SR_RXMSGEND) {
        bool received = ((status & UCPD_SR_RXERR) == 0) && !port->rx_overrun &&
                        ((ucpd->RX_ORDSET & UCPD_RX_ORDSET_RXORDSET) == LL_UCPD_RXORDSET_SOP);

        LL_UCPD_ClearFlag_RxMsgEnd(ucpd);
        furi_hal_usbpd_dma_arm(config->dma_rx_channel, port->rx_buffer, port->rx_buffer_size);
        port->rx_overrun = false;
        furi_hal_usbpd_set_busy(&port->receiving, false);
        if(received) {
            furi_hal_usbpd_notify(id, FuriHalUsbPdEventMessageReceived);
        }
        return;
    }
    if(status & (UCPD_SR_TYPECEVT1 | UCPD_SR_TYPECEVT2)) {
        LL_UCPD_ClearFlag_TypeCEventCC1(ucpd);
        LL_UCPD_ClearFlag_TypeCEventCC2(ucpd);
        furi_hal_usbpd_notify(id, FuriHalUsbPdEventCcChanged);
        return;
    }
    if(status & UCPD_SR_FRSEVT) {
        LL_UCPD_ClearFlag_FRS(ucpd);
        furi_hal_cortex_delay_us(FURI_HAL_USBPD_FRS_CONFIRM_US);
        if(ucpd->SR & (UCPD_SR_TYPEC_VSTATE_CC1 | UCPD_SR_TYPEC_VSTATE_CC2)) {
            furi_hal_usbpd_notify(id, FuriHalUsbPdEventFastRoleSwapReceived);
        }
    }
}

void furi_hal_usbpd_init(void) {
    furi_hal_gpio_write(&gpio_ucpd_phone_frstx, false);
    furi_hal_gpio_init_simple(&gpio_ucpd_phone_frstx, GpioModeOutputPushPull);
}

void furi_hal_usbpd_start(FuriHalUsbPdId id, FuriHalUsbPdCallback callback, void* context) {
    furi_check(id < FuriHalUsbPdIdMax);
    furi_check(callback);
    const FuriHalUsbPdConfig* config = &furi_hal_usbpd_config[id];
    FuriHalUsbPd* port = &furi_hal_usbpd[id];
    UCPD_TypeDef* ucpd = config->ucpd;
    LL_UCPD_InitTypeDef ucpd_init;

    furi_check(!port->started);
    port->callback = callback;
    port->context = context;
    port->communicating = false;
    port->rx_overrun = false;

    furi_hal_bus_enable(config->bus);
    LL_UCPD_StructInit(&ucpd_init);
    furi_check(LL_UCPD_Init(ucpd, &ucpd_init) == SUCCESS);
    LL_UCPD_SetRxOrderSet(ucpd, LL_UCPD_ORDERSET_SOP | LL_UCPD_ORDERSET_HARDRST);
    LL_UCPD_SetccEnable(ucpd, LL_UCPD_CCENABLE_CC1CC2);
    LL_UCPD_WakeUpEnable(ucpd);
    LL_UCPD_Enable(ucpd);

    furi_hal_usbpd_dma_init(
        config->dma_rx_channel,
        config->dma_rx_request,
        LL_DMA_DIRECTION_PERIPH_TO_MEMORY,
        LL_DMA_PRIORITY_HIGH,
        (uint32_t)&ucpd->RXDR);
    furi_hal_usbpd_dma_init(
        config->dma_tx_channel,
        config->dma_tx_request,
        LL_DMA_DIRECTION_MEMORY_TO_PERIPH,
        LL_DMA_PRIORITY_MEDIUM,
        (uint32_t)&ucpd->TXDR);

    WRITE_REG(ucpd->IMR, UCPD_IMR_TYPECEVT1IE | UCPD_IMR_TYPECEVT2IE);
    furi_hal_interrupt_set_isr_ex(
        config->interrupt_id,
        FuriHalInterruptPriorityHighest,
        furi_hal_usbpd_isr,
        (void*)(uintptr_t)id);
    port->started = true;
}

void furi_hal_usbpd_stop(FuriHalUsbPdId id) {
    furi_check(id < FuriHalUsbPdIdMax);
    const FuriHalUsbPdConfig* config = &furi_hal_usbpd_config[id];
    FuriHalUsbPd* port = &furi_hal_usbpd[id];

    furi_check(port->started);
    if(port->communicating) {
        furi_hal_usbpd_stop_communication(id);
    }
    furi_hal_interrupt_set_isr(config->interrupt_id, NULL, NULL);
    WRITE_REG(config->ucpd->IMR, 0);
    LL_UCPD_Disable(config->ucpd);
    furi_hal_usbpd_dma_disable(config->dma_rx_channel);
    furi_hal_usbpd_dma_disable(config->dma_tx_channel);
    furi_hal_usbpd_set_busy(&port->transmitting, false);
    furi_hal_bus_disable(config->bus);
    port->started = false;
}

void furi_hal_usbpd_set_rp(FuriHalUsbPdId id, FuriHalUsbPdRp rp) {
    furi_check(id < FuriHalUsbPdIdMax);
    furi_check(rp < COUNT_OF(furi_hal_usbpd_rp_resistor));
    UCPD_TypeDef* ucpd = furi_hal_usbpd_config[id].ucpd;

    LL_UCPD_SetRpResistor(ucpd, furi_hal_usbpd_rp_resistor[rp]);
    LL_UCPD_SetccEnable(ucpd, LL_UCPD_CCENABLE_NONE);
    LL_UCPD_SetSRCRole(ucpd);
    furi_hal_usbpd_apply_cc_enable(id);
    furi_hal_usbpd_strobe(id);
}

void furi_hal_usbpd_set_rd(FuriHalUsbPdId id) {
    furi_check(id < FuriHalUsbPdIdMax);
    UCPD_TypeDef* ucpd = furi_hal_usbpd_config[id].ucpd;

    LL_UCPD_TypeCDetectionCC2Disable(ucpd);
    LL_UCPD_TypeCDetectionCC1Disable(ucpd);
    LL_UCPD_SetccEnable(ucpd, LL_UCPD_CCENABLE_NONE);
    LL_UCPD_SetSNKRole(ucpd);
    furi_hal_usbpd_apply_cc_enable(id);
    furi_hal_usbpd_strobe(id);
    furi_hal_cortex_delay_us(FURI_HAL_USBPD_SET_RD_SETTLE_US);
    LL_UCPD_TypeCDetectionCC2Enable(ucpd);
    LL_UCPD_TypeCDetectionCC1Enable(ucpd);
}

void furi_hal_usbpd_set_cc_open(FuriHalUsbPdId id) {
    furi_check(id < FuriHalUsbPdIdMax);
    UCPD_TypeDef* ucpd = furi_hal_usbpd_config[id].ucpd;

    LL_UCPD_SetSRCRole(ucpd);
    LL_UCPD_SetRpResistor(ucpd, LL_UCPD_RESISTOR_NONE);
    LL_UCPD_SetccEnable(ucpd, LL_UCPD_CCENABLE_NONE);
    furi_hal_usbpd_strobe(id);
    LL_UCPD_RxDisable(ucpd);
    LL_UCPD_FRSDetectionDisable(ucpd);
}

void furi_hal_usbpd_get_cc(FuriHalUsbPdId id, FuriHalUsbPdCcState* cc1, FuriHalUsbPdCcState* cc2) {
    furi_check(id < FuriHalUsbPdIdMax);
    furi_check(cc1);
    furi_check(cc2);
    UCPD_TypeDef* ucpd = furi_hal_usbpd_config[id].ucpd;
    const uint32_t control = READ_REG(ucpd->CR);
    const uint32_t status = READ_REG(ucpd->SR);

    *cc1 = furi_hal_usbpd_decode_cc(
        control, (status & UCPD_SR_TYPEC_VSTATE_CC1) >> UCPD_SR_TYPEC_VSTATE_CC1_Pos);
    *cc2 = furi_hal_usbpd_decode_cc(
        control, (status & UCPD_SR_TYPEC_VSTATE_CC2) >> UCPD_SR_TYPEC_VSTATE_CC2_Pos);
}

void furi_hal_usbpd_set_receive_buffer(FuriHalUsbPdId id, uint8_t* buffer, size_t size) {
    furi_check(id < FuriHalUsbPdIdMax);
    furi_check(buffer);
    furi_check(size > 0);
    furi_check(!furi_hal_usbpd[id].communicating);

    furi_hal_usbpd[id].rx_buffer = buffer;
    furi_hal_usbpd[id].rx_buffer_size = size;
}

void furi_hal_usbpd_start_communication(FuriHalUsbPdId id, FuriHalUsbPdCcPin pin) {
    furi_check(id < FuriHalUsbPdIdMax);
    const FuriHalUsbPdConfig* config = &furi_hal_usbpd_config[id];
    FuriHalUsbPd* port = &furi_hal_usbpd[id];
    UCPD_TypeDef* ucpd = config->ucpd;

    furi_check(port->started);
    furi_check(port->rx_buffer);

    furi_hal_usbpd_dma_arm(config->dma_rx_channel, port->rx_buffer, port->rx_buffer_size);
    MODIFY_REG(
        ucpd->IMR, FURI_HAL_USBPD_ATTACHED_INTERRUPT_MASK, FURI_HAL_USBPD_ATTACHED_INTERRUPT_MASK);

    port->pin = pin;
    port->rx_overrun = false;
    LL_UCPD_SetCCPin(ucpd, (pin == FuriHalUsbPdCcPin1) ? LL_UCPD_CCPIN_CC1 : LL_UCPD_CCPIN_CC2);
    if(config->gpio_frstx && (pin == FuriHalUsbPdCcPin1)) {
        furi_hal_gpio_init_ex(
            config->gpio_frstx,
            GpioModeAltFunctionPushPull,
            GpioPullNo,
            GpioSpeedLow,
            GpioAltFn1UCPD1);
    }
    port->communicating = true;
    furi_hal_usbpd_apply_cc_enable(id);

    LL_UCPD_SetRxMode(ucpd, LL_UCPD_RXMODE_NORMAL);
    LL_UCPD_RxDMAEnable(ucpd);
    LL_UCPD_TxDMAEnable(ucpd);
}

void furi_hal_usbpd_stop_communication(FuriHalUsbPdId id) {
    furi_check(id < FuriHalUsbPdIdMax);
    const FuriHalUsbPdConfig* config = &furi_hal_usbpd_config[id];
    FuriHalUsbPd* port = &furi_hal_usbpd[id];
    UCPD_TypeDef* ucpd = config->ucpd;

    LL_UCPD_RxDMADisable(ucpd);
    LL_UCPD_TxDMADisable(ucpd);
    LL_UCPD_RxDisable(ucpd);
    WRITE_REG(ucpd->IMR, UCPD_IMR_TYPECEVT1IE | UCPD_IMR_TYPECEVT2IE);
    furi_hal_usbpd_dma_disable(config->dma_rx_channel);
    furi_hal_usbpd_dma_disable(config->dma_tx_channel);

    port->communicating = false;
    furi_hal_usbpd_apply_cc_enable(id);
    LL_UCPD_FRSDetectionDisable(ucpd);
    if(config->gpio_frstx) {
        furi_hal_gpio_write(config->gpio_frstx, false);
        furi_hal_gpio_init_simple(config->gpio_frstx, GpioModeOutputPushPull);
    }
    furi_hal_usbpd_set_busy(&port->receiving, false);
    furi_hal_usbpd_set_busy(&port->transmitting, false);
    port->rx_overrun = false;
}

void furi_hal_usbpd_enable_receive(FuriHalUsbPdId id) {
    furi_check(id < FuriHalUsbPdIdMax);
    LL_UCPD_RxEnable(furi_hal_usbpd_config[id].ucpd);
}

void furi_hal_usbpd_disable_receive(FuriHalUsbPdId id) {
    furi_check(id < FuriHalUsbPdIdMax);
    LL_UCPD_RxDisable(furi_hal_usbpd_config[id].ucpd);
    furi_hal_usbpd_set_busy(&furi_hal_usbpd[id].receiving, false);
}

bool furi_hal_usbpd_send_message(FuriHalUsbPdId id, const uint8_t* data, size_t size) {
    furi_check(id < FuriHalUsbPdIdMax);
    furi_check(data);
    const FuriHalUsbPdConfig* config = &furi_hal_usbpd_config[id];
    FuriHalUsbPd* port = &furi_hal_usbpd[id];
    UCPD_TypeDef* ucpd = config->ucpd;
    bool busy;

    FURI_CRITICAL_ENTER();
    busy = port->receiving ||
           LL_DMA_IsEnabledChannel(FURI_HAL_USBPD_DMA_INSTANCE, config->dma_tx_channel);
    FURI_CRITICAL_EXIT();
    if(busy) {
        return false;
    }

    LL_UCPD_WriteTxOrderSet(ucpd, LL_UCPD_ORDERED_SET_SOP);
    LL_UCPD_SetTxMode(ucpd, LL_UCPD_TXMODE_NORMAL);
    furi_hal_usbpd_set_busy(&port->transmitting, true);
    furi_hal_usbpd_dma_arm(config->dma_tx_channel, data, size);
    LL_UCPD_WriteTxPaySize(ucpd, size);
    LL_UCPD_SendMessage(ucpd);
    return true;
}

void furi_hal_usbpd_send_hard_reset(FuriHalUsbPdId id) {
    furi_check(id < FuriHalUsbPdIdMax);
    furi_hal_usbpd_set_busy(&furi_hal_usbpd[id].transmitting, true);
    LL_UCPD_SendHardReset(furi_hal_usbpd_config[id].ucpd);
}

void furi_hal_usbpd_start_bist_carrier(FuriHalUsbPdId id) {
    furi_check(id < FuriHalUsbPdIdMax);
    UCPD_TypeDef* ucpd = furi_hal_usbpd_config[id].ucpd;

    furi_hal_usbpd_set_busy(&furi_hal_usbpd[id].transmitting, true);
    LL_UCPD_SetTxMode(ucpd, LL_UCPD_TXMODE_BIST_CARRIER2);
    LL_UCPD_SendMessage(ucpd);
}

void furi_hal_usbpd_stop_bist_carrier(FuriHalUsbPdId id) {
    furi_check(id < FuriHalUsbPdIdMax);
    UCPD_TypeDef* ucpd = furi_hal_usbpd_config[id].ucpd;
    const uint32_t control = READ_REG(ucpd->CR) & ~(UCPD_CR_TXMODE | UCPD_CR_TXSEND);

    LL_UCPD_Disable(ucpd);
    LL_UCPD_Enable(ucpd);
    WRITE_REG(ucpd->CR, control);
    furi_hal_usbpd_set_busy(&furi_hal_usbpd[id].transmitting, false);
}

void furi_hal_usbpd_set_sink_tx_ok(FuriHalUsbPdId id, bool ok) {
    furi_check(id < FuriHalUsbPdIdMax);
    LL_UCPD_SetRpResistor(
        furi_hal_usbpd_config[id].ucpd, ok ? LL_UCPD_RESISTOR_3_0A : LL_UCPD_RESISTOR_1_5A);
}

bool furi_hal_usbpd_is_sink_tx_ok(FuriHalUsbPdId id) {
    furi_check(id < FuriHalUsbPdIdMax);
    const FuriHalUsbPd* port = &furi_hal_usbpd[id];
    const uint32_t status = READ_REG(furi_hal_usbpd_config[id].ucpd->SR);

    if(!port->communicating) {
        return false;
    }
    if(port->pin == FuriHalUsbPdCcPin1) {
        return (status & UCPD_SR_TYPEC_VSTATE_CC1) == LL_UCPD_SNK_CC1_VRP30A;
    }
    return (status & UCPD_SR_TYPEC_VSTATE_CC2) == LL_UCPD_SNK_CC2_VRP30A;
}

void furi_hal_usbpd_send_fast_role_swap(FuriHalUsbPdId id) {
    furi_check(id < FuriHalUsbPdIdMax);
    furi_check(furi_hal_usbpd_config[id].gpio_frstx);
    LL_UCPD_SignalFRSTX(furi_hal_usbpd_config[id].ucpd);
}

void furi_hal_usbpd_set_fast_role_swap_detection(FuriHalUsbPdId id, bool enabled) {
    furi_check(id < FuriHalUsbPdIdMax);
    UCPD_TypeDef* ucpd = furi_hal_usbpd_config[id].ucpd;

    FURI_CRITICAL_ENTER();
    if(enabled) {
        LL_UCPD_FRSDetectionEnable(ucpd);
        SET_BIT(ucpd->IMR, UCPD_IMR_FRSEVTIE);
    } else {
        CLEAR_BIT(ucpd->IMR, UCPD_IMR_FRSEVTIE);
        LL_UCPD_FRSDetectionDisable(ucpd);
    }
    FURI_CRITICAL_EXIT();
}
