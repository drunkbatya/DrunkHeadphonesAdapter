#include <furi_hal_i2s.h>
#include <furi_hal_bus.h>
#include <furi_hal_cortex.h>
#include <furi_hal_gpio.h>
#include <furi_hal_interrupt.h>
#include <furi_hal_power.h>
#include <furi_hal_resources.h>

#include <stm32g0xx_ll_dma.h>
#include <stm32g0xx_ll_rcc.h>
#include <stm32g0xx_ll_spi.h>
#include <furi.h>

#define TAG "FuriHalI2s"

#define FURI_HAL_I2S_DMA_INSTANCE       (DMA1)
#define FURI_HAL_I2S_DMA_FLAG_STRIDE    (DMA_ISR_GIF2_Pos - DMA_ISR_GIF1_Pos)
#define FURI_HAL_I2S_TX_STOP_TIMEOUT_US 1000

typedef struct {
    SPI_TypeDef* spi;
    FuriHalBus bus;
    uint32_t transfer_mode;
    uint32_t dma_channel;
    uint32_t dma_request;
    uint32_t dma_direction;
    uint32_t dma_priority;
    FuriHalInterruptId spi_interrupt_id;
    FuriHalInterruptId dma_interrupt_id;
    const GpioPin* gpio_ck;
    const GpioPin* gpio_ws;
    const GpioPin* gpio_sd;
    GpioAltFn alt_fn_ck;
    GpioAltFn alt_fn_ws;
    GpioAltFn alt_fn_sd;
    GpioSpeed speed_sd;
} FuriHalI2sConfig;

typedef struct {
    FuriHalI2sCallback callback;
    void* context;
    bool running;
} FuriHalI2s;

static const FuriHalI2sConfig furi_hal_i2s_config[FuriHalI2sIdMax] = {
    [FuriHalI2sIdHeadphones] =
        {
            .spi = SPI1,
            .bus = FuriHalBusSPI1,
            .transfer_mode = LL_I2S_MODE_SLAVE_TX,
            .dma_channel = LL_DMA_CHANNEL_1,
            .dma_request = LL_DMAMUX_REQ_SPI1_TX,
            .dma_direction = LL_DMA_DIRECTION_MEMORY_TO_PERIPH,
            .dma_priority = LL_DMA_PRIORITY_VERYHIGH,
            .spi_interrupt_id = FuriHalInterruptIdSpi1,
            .dma_interrupt_id = FuriHalInterruptIdDma1Ch1,
            .gpio_ck = &gpio_i2s_headphones_ck,
            .gpio_ws = &gpio_i2s_headphones_ws,
            .gpio_sd = &gpio_i2s_headphones_sd,
            .alt_fn_ck = GpioAltFn0I2S1,
            .alt_fn_ws = GpioAltFn0I2S1,
            .alt_fn_sd = GpioAltFn0I2S1,
            .speed_sd = GpioSpeedHigh,
        },
    [FuriHalI2sIdMic] =
        {
            .spi = SPI2,
            .bus = FuriHalBusSPI2,
            .transfer_mode = LL_I2S_MODE_SLAVE_RX,
            .dma_channel = LL_DMA_CHANNEL_2,
            .dma_request = LL_DMAMUX_REQ_SPI2_RX,
            .dma_direction = LL_DMA_DIRECTION_PERIPH_TO_MEMORY,
            .dma_priority = LL_DMA_PRIORITY_HIGH,
            .spi_interrupt_id = FuriHalInterruptIdSpi2,
            .dma_interrupt_id = FuriHalInterruptIdDma1Ch2,
            .gpio_ck = &gpio_i2s_mic_ck,
            .gpio_ws = &gpio_i2s_mic_ws,
            .gpio_sd = &gpio_i2s_mic_sd,
            .alt_fn_ck = GpioAltFn5I2S2,
            .alt_fn_ws = GpioAltFn5I2S2,
            .alt_fn_sd = GpioAltFn1I2S2,
            .speed_sd = GpioSpeedLow,
        },
};

static const uint32_t furi_hal_i2s_data_format[] = {
    [FuriHalI2sDataFormat16Bit] = LL_I2S_DATAFORMAT_16B,
    [FuriHalI2sDataFormat16BitIn32BitFrame] = LL_I2S_DATAFORMAT_16B_EXTENDED,
    [FuriHalI2sDataFormat24BitIn32BitFrame] = LL_I2S_DATAFORMAT_24B,
    [FuriHalI2sDataFormat32Bit] = LL_I2S_DATAFORMAT_32B,
};

static FuriHalI2s furi_hal_i2s[FuriHalI2sIdMax] = {0};

static void furi_hal_i2s_notify(FuriHalI2sId id, FuriHalI2sEvent event) {
    furi_hal_i2s[id].callback(event, furi_hal_i2s[id].context);
}

static void furi_hal_i2s_dma_clear_flags(uint32_t dma_channel, uint32_t flags_channel_1) {
    WRITE_REG(
        FURI_HAL_I2S_DMA_INSTANCE->IFCR,
        flags_channel_1 << (dma_channel * FURI_HAL_I2S_DMA_FLAG_STRIDE));
}

static void furi_hal_i2s_dma_isr(void* context) {
    const FuriHalI2sId id = (FuriHalI2sId)(uintptr_t)context;
    const uint32_t dma_channel = furi_hal_i2s_config[id].dma_channel;
    const uint32_t flags = READ_REG(FURI_HAL_I2S_DMA_INSTANCE->ISR) >>
                           (dma_channel * FURI_HAL_I2S_DMA_FLAG_STRIDE);

    if(flags & DMA_ISR_TEIF1) {
        furi_hal_i2s_dma_clear_flags(dma_channel, DMA_IFCR_CTEIF1);
        furi_hal_i2s_notify(id, FuriHalI2sEventDmaError);
    }
    if(flags & DMA_ISR_HTIF1) {
        furi_hal_i2s_dma_clear_flags(dma_channel, DMA_IFCR_CHTIF1);
        furi_hal_i2s_notify(id, FuriHalI2sEventHalfTransfer);
    }
    if(flags & DMA_ISR_TCIF1) {
        furi_hal_i2s_dma_clear_flags(dma_channel, DMA_IFCR_CTCIF1);
        furi_hal_i2s_notify(id, FuriHalI2sEventTransferComplete);
    }
}

static void furi_hal_i2s_spi_isr(void* context) {
    const FuriHalI2sId id = (FuriHalI2sId)(uintptr_t)context;
    SPI_TypeDef* spi = furi_hal_i2s_config[id].spi;
    const uint32_t status = READ_REG(spi->SR);

    LL_I2S_DisableIT_ERR(spi);

    if(status & SPI_SR_OVR) {
        LL_I2S_ClearFlag_OVR(spi);
        furi_hal_i2s_notify(id, FuriHalI2sEventOverrun);
    }
    if(status & SPI_SR_UDR) {
        furi_hal_i2s_notify(id, FuriHalI2sEventUnderrun);
    }
    if(status & SPI_SR_FRE) {
        furi_hal_i2s_notify(id, FuriHalI2sEventFrameError);
    }
}

void furi_hal_i2s_init(void) {
    LL_RCC_SetI2SClockSource(LL_RCC_I2S1_CLKSOURCE_SYSCLK);
    LL_RCC_SetI2SClockSource(LL_RCC_I2S2_CLKSOURCE_SYSCLK);

    FURI_LOG_I(TAG, "Init OK");
}

void furi_hal_i2s_start(
    FuriHalI2sId id,
    FuriHalI2sDataFormat data_format,
    uint16_t* buffer,
    size_t length,
    FuriHalI2sCallback callback,
    void* context) {
    furi_check(id < FuriHalI2sIdMax);
    furi_check(data_format < COUNT_OF(furi_hal_i2s_data_format));
    furi_check(buffer);
    furi_check(length > 0 && length <= UINT16_MAX);
    furi_check(callback);

    FuriHalI2s* i2s = &furi_hal_i2s[id];
    const FuriHalI2sConfig* config = &furi_hal_i2s_config[id];
    furi_check(!i2s->running);

    furi_hal_power_insomnia_enter();

    i2s->callback = callback;
    i2s->context = context;
    i2s->running = true;

    furi_hal_bus_enable(config->bus);

    furi_hal_gpio_init_ex(
        config->gpio_ck, GpioModeAltFunctionPushPull, GpioPullNo, GpioSpeedLow, config->alt_fn_ck);
    furi_hal_gpio_init_ex(
        config->gpio_ws, GpioModeAltFunctionPushPull, GpioPullNo, GpioSpeedLow, config->alt_fn_ws);
    furi_hal_gpio_init_ex(
        config->gpio_sd,
        GpioModeAltFunctionPushPull,
        GpioPullNo,
        config->speed_sd,
        config->alt_fn_sd);

    SET_BIT(config->spi->I2SCFGR, SPI_I2SCFGR_I2SMOD);
    LL_I2S_SetTransferMode(config->spi, config->transfer_mode);
    LL_I2S_SetStandard(config->spi, LL_I2S_STANDARD_PHILIPS);
    LL_I2S_SetDataFormat(config->spi, furi_hal_i2s_data_format[data_format]);
    LL_I2S_SetClockPolarity(config->spi, LL_I2S_POLARITY_LOW);

    LL_DMA_ConfigTransfer(
        FURI_HAL_I2S_DMA_INSTANCE,
        config->dma_channel,
        config->dma_direction | LL_DMA_MODE_CIRCULAR | LL_DMA_PERIPH_NOINCREMENT |
            LL_DMA_MEMORY_INCREMENT | LL_DMA_PDATAALIGN_HALFWORD | LL_DMA_MDATAALIGN_HALFWORD |
            config->dma_priority);
    LL_DMA_SetMemoryAddress(FURI_HAL_I2S_DMA_INSTANCE, config->dma_channel, (uint32_t)buffer);
    LL_DMA_SetPeriphAddress(
        FURI_HAL_I2S_DMA_INSTANCE, config->dma_channel, (uint32_t)&config->spi->DR);
    LL_DMA_SetDataLength(FURI_HAL_I2S_DMA_INSTANCE, config->dma_channel, length);
    LL_DMA_SetPeriphRequest(FURI_HAL_I2S_DMA_INSTANCE, config->dma_channel, config->dma_request);
    furi_hal_i2s_dma_clear_flags(config->dma_channel, DMA_IFCR_CGIF1);

    furi_hal_interrupt_set_isr(
        config->dma_interrupt_id, furi_hal_i2s_dma_isr, (void*)(uintptr_t)id);
    furi_hal_interrupt_set_isr(
        config->spi_interrupt_id, furi_hal_i2s_spi_isr, (void*)(uintptr_t)id);

    LL_DMA_EnableIT_TE(FURI_HAL_I2S_DMA_INSTANCE, config->dma_channel);
    LL_DMA_EnableIT_HT(FURI_HAL_I2S_DMA_INSTANCE, config->dma_channel);
    LL_DMA_EnableIT_TC(FURI_HAL_I2S_DMA_INSTANCE, config->dma_channel);
    LL_DMA_EnableChannel(FURI_HAL_I2S_DMA_INSTANCE, config->dma_channel);

    LL_I2S_EnableIT_ERR(config->spi);

    FURI_CRITICAL_ENTER();
    LL_I2S_Enable(config->spi);
    if(config->transfer_mode == LL_I2S_MODE_SLAVE_TX) {
        LL_I2S_EnableDMAReq_TX(config->spi);
    } else {
        LL_I2S_EnableDMAReq_RX(config->spi);
    }
    FURI_CRITICAL_EXIT();
}

void furi_hal_i2s_stop(FuriHalI2sId id) {
    furi_check(id < FuriHalI2sIdMax);

    FuriHalI2s* i2s = &furi_hal_i2s[id];
    const FuriHalI2sConfig* config = &furi_hal_i2s_config[id];
    furi_check(i2s->running);

    LL_I2S_DisableIT_ERR(config->spi);

    if(config->transfer_mode == LL_I2S_MODE_SLAVE_TX) {
        LL_I2S_DisableDMAReq_TX(config->spi);
        FuriHalCortexTimer timer = furi_hal_cortex_timer_get(FURI_HAL_I2S_TX_STOP_TIMEOUT_US);
        while(!LL_I2S_IsActiveFlag_TXE(config->spi) || LL_I2S_IsActiveFlag_BSY(config->spi)) {
            if(furi_hal_cortex_timer_is_expired(timer)) {
                break;
            }
        }
    } else {
        LL_I2S_DisableDMAReq_RX(config->spi);
    }
    LL_I2S_Disable(config->spi);

    LL_DMA_DisableChannel(FURI_HAL_I2S_DMA_INSTANCE, config->dma_channel);
    LL_DMA_DisableIT_TE(FURI_HAL_I2S_DMA_INSTANCE, config->dma_channel);
    LL_DMA_DisableIT_HT(FURI_HAL_I2S_DMA_INSTANCE, config->dma_channel);
    LL_DMA_DisableIT_TC(FURI_HAL_I2S_DMA_INSTANCE, config->dma_channel);
    furi_hal_i2s_dma_clear_flags(config->dma_channel, DMA_IFCR_CGIF1);

    furi_hal_interrupt_set_isr(config->spi_interrupt_id, NULL, NULL);
    furi_hal_interrupt_set_isr(config->dma_interrupt_id, NULL, NULL);

    furi_hal_gpio_init_ex(
        config->gpio_ck, GpioModeAnalog, GpioPullNo, GpioSpeedLow, GpioAltFnUnused);
    furi_hal_gpio_init_ex(
        config->gpio_ws, GpioModeAnalog, GpioPullNo, GpioSpeedLow, GpioAltFnUnused);
    furi_hal_gpio_init_ex(
        config->gpio_sd, GpioModeAnalog, GpioPullNo, GpioSpeedLow, GpioAltFnUnused);

    furi_hal_bus_disable(config->bus);

    i2s->callback = NULL;
    i2s->context = NULL;
    i2s->running = false;

    furi_hal_power_insomnia_exit();
}
