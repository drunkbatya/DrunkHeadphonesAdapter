#include <furi_hal_serial.h>
#include "furi_hal_serial_types_i.h"

#include <stdbool.h>
#include <stm32g0xx_ll_lpuart.h>
#include <stm32g0xx_ll_rcc.h>
#include <stm32g0xx_ll_dma.h>
#include <furi_hal_resources.h>
#include <furi_hal_interrupt.h>
#include <furi_hal_bus.h>

#include <furi.h>

#define FURI_HAL_SERIAL_LPUART_DMA_INSTANCE (DMA1)
#define FURI_HAL_SERIAL_LPUART_DMA_CHANNEL  (LL_DMA_CHANNEL_7)

typedef struct {
    uint8_t* buffer_rx_ptr;
    size_t buffer_rx_index_write;
    size_t buffer_rx_index_read;
    bool enabled;
    FuriHalSerialHandle* handle;
    FuriHalSerialAsyncRxCallback rx_byte_callback;
    FuriHalSerialDmaRxCallback rx_dma_callback;
    void* context;
} FuriHalSerial;

typedef void (*FuriHalSerialControlFunc)(USART_TypeDef*);

typedef struct {
    USART_TypeDef* periph;
    GpioAltFn alt_fn;
    const GpioPin* gpio[FuriHalSerialDirectionMax];
    FuriHalSerialControlFunc enable[FuriHalSerialDirectionMax];
    FuriHalSerialControlFunc disable[FuriHalSerialDirectionMax];
} FuriHalSerialConfig;

static const FuriHalSerialConfig furi_hal_serial_config[FuriHalSerialIdMax] = {
    [FuriHalSerialIdLpuart] =
        {
            .periph = LPUART2,
            .alt_fn = GpioAltFn3LPUART2,
            .gpio =
                {
                    [FuriHalSerialDirectionTx] = &gpio_lpuart_tx,
                    [FuriHalSerialDirectionRx] = &gpio_lpuart_rx,
                },
            .enable =
                {
                    [FuriHalSerialDirectionTx] = LL_LPUART_EnableDirectionTx,
                    [FuriHalSerialDirectionRx] = LL_LPUART_EnableDirectionRx,
                },
            .disable =
                {
                    [FuriHalSerialDirectionTx] = LL_LPUART_DisableDirectionTx,
                    [FuriHalSerialDirectionRx] = LL_LPUART_DisableDirectionRx,
                },
        },
};

static FuriHalSerial furi_hal_serial[FuriHalSerialIdMax] = {0};

static size_t furi_hal_serial_dma_bytes_available(FuriHalSerialId ch);

static void furi_hal_serial_async_rx_configure(
    FuriHalSerialHandle* handle,
    FuriHalSerialAsyncRxCallback callback,
    void* context);

static void furi_hal_serial_lpuart_irq_callback(void* context) {
    UNUSED(context);

    FuriHalSerialRxEvent event = 0;
    // Notification flags
    if(LPUART2->ISR & USART_ISR_RXNE_RXFNE) {
        event |= FuriHalSerialRxEventData;
    }
    if(LPUART2->ISR & USART_ISR_IDLE) {
        LPUART2->ICR = USART_ICR_IDLECF;
        event |= FuriHalSerialRxEventIdle;
    }
    // Error flags
    if(LPUART2->ISR & USART_ISR_ORE) {
        LPUART2->ICR = USART_ICR_ORECF;
        event |= FuriHalSerialRxEventOverrunError;
    }
    if(LPUART2->ISR & USART_ISR_NE) {
        LPUART2->ICR = USART_ICR_NECF;
        event |= FuriHalSerialRxEventNoiseError;
    }
    if(LPUART2->ISR & USART_ISR_FE) {
        LPUART2->ICR = USART_ICR_FECF;
        event |= FuriHalSerialRxEventFrameError;
    }
    if(LPUART2->ISR & USART_ISR_PE) {
        LPUART2->ICR = USART_ICR_PECF;
        event |= FuriHalSerialRxEventParityError;
    }

    if(furi_hal_serial[FuriHalSerialIdLpuart].buffer_rx_ptr == NULL) {
        if(furi_hal_serial[FuriHalSerialIdLpuart].rx_byte_callback) {
            furi_hal_serial[FuriHalSerialIdLpuart].rx_byte_callback(
                furi_hal_serial[FuriHalSerialIdLpuart].handle,
                event,
                furi_hal_serial[FuriHalSerialIdLpuart].context);
        }
    } else {
        if(furi_hal_serial[FuriHalSerialIdLpuart].rx_dma_callback) {
            furi_hal_serial[FuriHalSerialIdLpuart].rx_dma_callback(
                furi_hal_serial[FuriHalSerialIdLpuart].handle,
                event,
                furi_hal_serial_dma_bytes_available(FuriHalSerialIdLpuart),
                furi_hal_serial[FuriHalSerialIdLpuart].context);
        }
    }
}

static void furi_hal_serial_lpuart_dma_rx_isr(void* context) {
    UNUSED(context);
#if FURI_HAL_SERIAL_LPUART_DMA_CHANNEL == LL_DMA_CHANNEL_7
    if(LL_DMA_IsActiveFlag_HT7(FURI_HAL_SERIAL_LPUART_DMA_INSTANCE)) {
        LL_DMA_ClearFlag_HT7(FURI_HAL_SERIAL_LPUART_DMA_INSTANCE);
        furi_hal_serial[FuriHalSerialIdLpuart].buffer_rx_index_write =
            FURI_HAL_SERIAL_DMA_BUFFER_SIZE -
            LL_DMA_GetDataLength(
                FURI_HAL_SERIAL_LPUART_DMA_INSTANCE, FURI_HAL_SERIAL_LPUART_DMA_CHANNEL);
        if((furi_hal_serial[FuriHalSerialIdLpuart].buffer_rx_index_read >
            furi_hal_serial[FuriHalSerialIdLpuart].buffer_rx_index_write) ||
           (furi_hal_serial[FuriHalSerialIdLpuart].buffer_rx_index_read <
            FURI_HAL_SERIAL_DMA_BUFFER_SIZE / 4)) {
            if(furi_hal_serial[FuriHalSerialIdLpuart].rx_dma_callback) {
                furi_hal_serial[FuriHalSerialIdLpuart].rx_dma_callback(
                    furi_hal_serial[FuriHalSerialIdLpuart].handle,
                    FuriHalSerialRxEventData,
                    furi_hal_serial_dma_bytes_available(FuriHalSerialIdLpuart),
                    furi_hal_serial[FuriHalSerialIdLpuart].context);
            }
        }

    } else if(LL_DMA_IsActiveFlag_TC7(FURI_HAL_SERIAL_LPUART_DMA_INSTANCE)) {
        LL_DMA_ClearFlag_TC7(FURI_HAL_SERIAL_LPUART_DMA_INSTANCE);

        if(furi_hal_serial[FuriHalSerialIdLpuart].buffer_rx_index_read <
           FURI_HAL_SERIAL_DMA_BUFFER_SIZE * 3 / 4) {
            if(furi_hal_serial[FuriHalSerialIdLpuart].rx_dma_callback) {
                furi_hal_serial[FuriHalSerialIdLpuart].rx_dma_callback(
                    furi_hal_serial[FuriHalSerialIdLpuart].handle,
                    FuriHalSerialRxEventData,
                    furi_hal_serial_dma_bytes_available(FuriHalSerialIdLpuart),
                    furi_hal_serial[FuriHalSerialIdLpuart].context);
            }
        }
    }
#else
#error Update this code. Would you kindly?
#endif
}

static void furi_hal_serial_lpuart_init_dma_rx(void) {
    /* LPUART2_RX_DMA Init */
    furi_check(furi_hal_serial[FuriHalSerialIdLpuart].buffer_rx_ptr == NULL);
    furi_hal_serial[FuriHalSerialIdLpuart].buffer_rx_index_write = 0;
    furi_hal_serial[FuriHalSerialIdLpuart].buffer_rx_index_read = 0;
    furi_hal_serial[FuriHalSerialIdLpuart].buffer_rx_ptr = malloc(FURI_HAL_SERIAL_DMA_BUFFER_SIZE);
    LL_DMA_SetMemoryAddress(
        FURI_HAL_SERIAL_LPUART_DMA_INSTANCE,
        FURI_HAL_SERIAL_LPUART_DMA_CHANNEL,
        (uint32_t)furi_hal_serial[FuriHalSerialIdLpuart].buffer_rx_ptr);
    LL_DMA_SetPeriphAddress(
        FURI_HAL_SERIAL_LPUART_DMA_INSTANCE,
        FURI_HAL_SERIAL_LPUART_DMA_CHANNEL,
        (uint32_t)&(LPUART2->RDR));

    LL_DMA_ConfigTransfer(
        FURI_HAL_SERIAL_LPUART_DMA_INSTANCE,
        FURI_HAL_SERIAL_LPUART_DMA_CHANNEL,
        LL_DMA_DIRECTION_PERIPH_TO_MEMORY | LL_DMA_MODE_CIRCULAR | LL_DMA_PERIPH_NOINCREMENT |
            LL_DMA_MEMORY_INCREMENT | LL_DMA_PDATAALIGN_BYTE | LL_DMA_MDATAALIGN_BYTE |
            LL_DMA_PRIORITY_HIGH);
    LL_DMA_SetDataLength(
        FURI_HAL_SERIAL_LPUART_DMA_INSTANCE,
        FURI_HAL_SERIAL_LPUART_DMA_CHANNEL,
        FURI_HAL_SERIAL_DMA_BUFFER_SIZE);
    LL_DMA_SetPeriphRequest(
        FURI_HAL_SERIAL_LPUART_DMA_INSTANCE,
        FURI_HAL_SERIAL_LPUART_DMA_CHANNEL,
        LL_DMAMUX_REQ_LPUART2_RX);

    furi_hal_interrupt_set_isr(FuriHalInterruptIdDma1Ch7, furi_hal_serial_lpuart_dma_rx_isr, NULL);

#if FURI_HAL_SERIAL_LPUART_DMA_CHANNEL == LL_DMA_CHANNEL_7
    if(LL_DMA_IsActiveFlag_HT7(FURI_HAL_SERIAL_LPUART_DMA_INSTANCE))
        LL_DMA_ClearFlag_HT7(FURI_HAL_SERIAL_LPUART_DMA_INSTANCE);
    if(LL_DMA_IsActiveFlag_TC7(FURI_HAL_SERIAL_LPUART_DMA_INSTANCE))
        LL_DMA_ClearFlag_TC7(FURI_HAL_SERIAL_LPUART_DMA_INSTANCE);
    if(LL_DMA_IsActiveFlag_TE7(FURI_HAL_SERIAL_LPUART_DMA_INSTANCE))
        LL_DMA_ClearFlag_TE7(FURI_HAL_SERIAL_LPUART_DMA_INSTANCE);
#else
#error Update this code. Would you kindly?
#endif

    LL_DMA_EnableIT_TC(FURI_HAL_SERIAL_LPUART_DMA_INSTANCE, FURI_HAL_SERIAL_LPUART_DMA_CHANNEL);
    LL_DMA_EnableIT_HT(FURI_HAL_SERIAL_LPUART_DMA_INSTANCE, FURI_HAL_SERIAL_LPUART_DMA_CHANNEL);

    LL_DMA_EnableChannel(FURI_HAL_SERIAL_LPUART_DMA_INSTANCE, FURI_HAL_SERIAL_LPUART_DMA_CHANNEL);
    LL_LPUART_EnableDMAReq_RX(LPUART2);

    LL_LPUART_EnableIT_IDLE(LPUART2);
}

static void furi_hal_serial_lpuart_deinit_dma_rx(void) {
    if(furi_hal_serial[FuriHalSerialIdLpuart].buffer_rx_ptr != NULL) {
        LL_DMA_DisableChannel(
            FURI_HAL_SERIAL_LPUART_DMA_INSTANCE, FURI_HAL_SERIAL_LPUART_DMA_CHANNEL);
        LL_LPUART_DisableDMAReq_RX(LPUART2);

        LL_LPUART_DisableIT_IDLE(LPUART2);
        LL_DMA_DisableIT_TC(
            FURI_HAL_SERIAL_LPUART_DMA_INSTANCE, FURI_HAL_SERIAL_LPUART_DMA_CHANNEL);
        LL_DMA_DisableIT_HT(
            FURI_HAL_SERIAL_LPUART_DMA_INSTANCE, FURI_HAL_SERIAL_LPUART_DMA_CHANNEL);

        LL_DMA_ClearFlag_TC7(FURI_HAL_SERIAL_LPUART_DMA_INSTANCE);
        LL_DMA_ClearFlag_HT7(FURI_HAL_SERIAL_LPUART_DMA_INSTANCE);

        LL_DMA_DeInit(FURI_HAL_SERIAL_LPUART_DMA_INSTANCE, FURI_HAL_SERIAL_LPUART_DMA_CHANNEL);
        furi_hal_interrupt_set_isr(FuriHalInterruptIdDma1Ch7, NULL, NULL);
        free(furi_hal_serial[FuriHalSerialIdLpuart].buffer_rx_ptr);
        furi_hal_serial[FuriHalSerialIdLpuart].buffer_rx_ptr = NULL;
    }
}

static void furi_hal_serial_lpuart_init(FuriHalSerialHandle* handle, uint32_t baud) {
    furi_hal_bus_enable(FuriHalBusLPUART2);
    LL_RCC_SetLPUARTClockSource(LL_RCC_LPUART2_CLKSOURCE_PCLK1);

    furi_hal_gpio_init_ex(
        &gpio_lpuart_rx,
        GpioModeAltFunctionPushPull,
        GpioPullUp,
        GpioSpeedVeryHigh,
        GpioAltFn3LPUART2);
    furi_hal_gpio_init_ex(
        &gpio_lpuart_tx,
        GpioModeAltFunctionPushPull,
        GpioPullUp,
        GpioSpeedVeryHigh,
        GpioAltFn3LPUART2);

    LL_LPUART_InitTypeDef LPUART_InitStruct;
    LPUART_InitStruct.PrescalerValue = LL_LPUART_PRESCALER_DIV1;
    LPUART_InitStruct.BaudRate = baud;
    LPUART_InitStruct.DataWidth = LL_LPUART_DATAWIDTH_8B;
    LPUART_InitStruct.StopBits = LL_LPUART_STOPBITS_1;
    LPUART_InitStruct.Parity = LL_LPUART_PARITY_NONE;
    LPUART_InitStruct.TransferDirection = LL_LPUART_DIRECTION_TX_RX;
    LPUART_InitStruct.HardwareFlowControl = LL_LPUART_HWCONTROL_NONE;
    LL_LPUART_Init(LPUART2, &LPUART_InitStruct);
    LL_LPUART_EnableFIFO(LPUART2);

    LL_LPUART_Enable(LPUART2);

    while(!LL_LPUART_IsActiveFlag_TEACK(LPUART2) || !LL_LPUART_IsActiveFlag_REACK(LPUART2))
        ;

    furi_hal_serial_set_br(handle, baud);
    LL_LPUART_DisableIT_ERROR(LPUART2);
    furi_hal_serial[handle->id].enabled = true;
}

void furi_hal_serial_init(FuriHalSerialHandle* handle, uint32_t baud) {
    furi_check(handle);
    furi_hal_serial_lpuart_init(handle, baud);
}

bool furi_hal_serial_is_baud_rate_supported(FuriHalSerialHandle* handle, uint32_t baud) {
    furi_check(handle);
    return baud >= 9600UL && baud <= 4000000UL;
}

static uint32_t furi_hal_serial_get_prescaler(FuriHalSerialHandle* handle, uint32_t baud) {
    UNUSED(handle);
    uint32_t uartclk = LL_RCC_GetLPUARTClockFreq(LL_RCC_LPUART2_CLKSOURCE);
    uint32_t divisor = (uartclk / baud);
    uint32_t prescaler = 0;
    divisor >>= 12;
    if(divisor < 1) {
        prescaler = LL_LPUART_PRESCALER_DIV1;
    } else if(divisor < 2) {
        prescaler = LL_LPUART_PRESCALER_DIV2;
    } else if(divisor < 4) {
        prescaler = LL_LPUART_PRESCALER_DIV4;
    } else if(divisor < 6) {
        prescaler = LL_LPUART_PRESCALER_DIV6;
    } else if(divisor < 8) {
        prescaler = LL_LPUART_PRESCALER_DIV8;
    } else if(divisor < 10) {
        prescaler = LL_LPUART_PRESCALER_DIV10;
    } else if(divisor < 12) {
        prescaler = LL_LPUART_PRESCALER_DIV12;
    } else if(divisor < 16) {
        prescaler = LL_LPUART_PRESCALER_DIV16;
    } else if(divisor < 32) {
        prescaler = LL_LPUART_PRESCALER_DIV32;
    } else if(divisor < 64) {
        prescaler = LL_LPUART_PRESCALER_DIV64;
    } else if(divisor < 128) {
        prescaler = LL_LPUART_PRESCALER_DIV128;
    } else {
        prescaler = LL_LPUART_PRESCALER_DIV256;
    }

    return prescaler;
}

void furi_hal_serial_set_br(FuriHalSerialHandle* handle, uint32_t baud) {
    furi_check(handle);
    uint32_t prescaler = furi_hal_serial_get_prescaler(handle, baud);
    if(LL_LPUART_IsEnabled(LPUART2)) {
        // Wait for transfer complete flag
        while(!LL_LPUART_IsActiveFlag_TC(LPUART2))
            ;
        LL_LPUART_Disable(LPUART2);
        uint32_t uartclk = LL_RCC_GetLPUARTClockFreq(LL_RCC_LPUART2_CLKSOURCE);
        LL_LPUART_SetPrescaler(LPUART2, prescaler);
        LL_LPUART_SetBaudRate(LPUART2, uartclk, prescaler, baud);
        LL_LPUART_Enable(LPUART2);
    }
}

static const uint32_t serial_data_bits_lut[] = {
    [FuriHalSerialDataBits7] = LL_LPUART_DATAWIDTH_7B,
    [FuriHalSerialDataBits8] = LL_LPUART_DATAWIDTH_8B,
    [FuriHalSerialDataBits9] = LL_LPUART_DATAWIDTH_9B,
};

static const uint32_t serial_parity_lut[] = {
    [FuriHalSerialParityNone] = LL_LPUART_PARITY_NONE,
    [FuriHalSerialParityEven] = LL_LPUART_PARITY_EVEN,
    [FuriHalSerialParityOdd] = LL_LPUART_PARITY_ODD,
};

static const uint32_t serial_stop_bits_lut[] = {
    [FuriHalSerialStopBits1] = LL_LPUART_STOPBITS_1,
    [FuriHalSerialStopBits2] = LL_LPUART_STOPBITS_2,
};

static void furi_hal_serial_lpuart_configure_framing(
    FuriHalSerialDataBits data_bits,
    FuriHalSerialParity parity,
    FuriHalSerialStopBits stop_bits) {
    LL_LPUART_SetDataWidth(LPUART2, serial_data_bits_lut[data_bits]);
    LL_LPUART_SetParity(LPUART2, serial_parity_lut[parity]);
    // Unsupported non-whole stop bit numbers have been furi_check'ed away
    LL_LPUART_SetStopBitsLength(LPUART2, serial_stop_bits_lut[stop_bits]);
}

void furi_hal_serial_configure_framing(
    FuriHalSerialHandle* handle,
    FuriHalSerialDataBits data_bits,
    FuriHalSerialParity parity,
    FuriHalSerialStopBits stop_bits) {
    furi_check(handle);

    // Unsupported combinations
    if(data_bits == FuriHalSerialDataBits9) furi_check(parity == FuriHalSerialParityNone);
    if(data_bits == FuriHalSerialDataBits6) furi_check(parity != FuriHalSerialParityNone);

    // Extend data word to account for parity bit
    if(parity != FuriHalSerialParityNone) data_bits++;

    // Unsupported configurations
    furi_check(stop_bits == FuriHalSerialStopBits1 || stop_bits == FuriHalSerialStopBits2);

    if(LL_LPUART_IsEnabled(LPUART2)) {
        // Wait for transfer complete flag
        while(!LL_LPUART_IsActiveFlag_TC(LPUART2))
            ;
        LL_LPUART_Disable(LPUART2);
        furi_hal_serial_lpuart_configure_framing(data_bits, parity, stop_bits);
        LL_LPUART_Enable(LPUART2);
    }
}

void furi_hal_serial_deinit(FuriHalSerialHandle* handle) {
    furi_check(handle);
    furi_hal_serial_async_rx_configure(handle, NULL, NULL);
    if(furi_hal_bus_is_enabled(FuriHalBusLPUART2)) {
        furi_hal_bus_disable(FuriHalBusLPUART2);
    }
    if(LL_LPUART_IsEnabled(LPUART2)) {
        LL_LPUART_Disable(LPUART2);
    }
    furi_hal_serial_lpuart_deinit_dma_rx();
    furi_hal_gpio_init(&gpio_lpuart_rx, GpioModeAnalog, GpioPullNo, GpioSpeedLow);
    furi_hal_gpio_init(&gpio_lpuart_tx, GpioModeAnalog, GpioPullNo, GpioSpeedLow);
    furi_hal_serial[handle->id].enabled = false;
}

void furi_hal_serial_suspend(FuriHalSerialHandle* handle) {
    furi_check(handle);
    if(LL_LPUART_IsEnabled(LPUART2)) {
        LL_LPUART_Disable(LPUART2);
    }
    furi_hal_serial[handle->id].enabled = false;
}

void furi_hal_serial_resume(FuriHalSerialHandle* handle) {
    furi_check(handle);
    if(!furi_hal_serial[handle->id].enabled) {
        LL_LPUART_Enable(LPUART2);
        furi_hal_serial[handle->id].enabled = true;
    }
}

void furi_hal_serial_tx(FuriHalSerialHandle* handle, const uint8_t* buffer, size_t buffer_size) {
    furi_check(handle);

    if(LL_LPUART_IsEnabled(LPUART2) == 0) return;

    while(buffer_size > 0) {
        while(!LL_LPUART_IsActiveFlag_TXE(LPUART2))
            ;

        LL_LPUART_TransmitData8(LPUART2, *buffer);

        buffer++;
        buffer_size--;
    }
}

void furi_hal_serial_tx_wait_complete(FuriHalSerialHandle* handle) {
    furi_check(handle);
    if(LL_LPUART_IsEnabled(LPUART2) == 0) return;

    while(!LL_LPUART_IsActiveFlag_TC(LPUART2))
        ;
}

static void furi_hal_serial_event_init(FuriHalSerialHandle* handle, bool report_errors) {
    UNUSED(handle);
    LL_LPUART_EnableIT_IDLE(LPUART2);

    if(report_errors) {
        LL_LPUART_EnableIT_ERROR(LPUART2);
    }
}

static void furi_hal_serial_event_deinit(FuriHalSerialHandle* handle) {
    UNUSED(handle);
    if(LL_LPUART_IsEnabledIT_IDLE(LPUART2)) LL_LPUART_DisableIT_IDLE(LPUART2);
    if(LL_LPUART_IsEnabledIT_ERROR(LPUART2)) LL_LPUART_DisableIT_ERROR(LPUART2);
}

static void furi_hal_serial_async_rx_configure(
    FuriHalSerialHandle* handle,
    FuriHalSerialAsyncRxCallback callback,
    void* context) {
    // Disable RXFNE interrupts before unsetting the user callback that reads data
    // Otherwise interrupt runs without reading data and without clearing RXFNE flag
    // This would cause a system hang as the same interrupt runs in loop forever
    if(!callback) {
        LL_LPUART_DisableIT_RXNE_RXFNE(LPUART2);
        furi_hal_interrupt_set_isr(FuriHalInterruptIdLpUart2, NULL, NULL);
        furi_hal_serial_lpuart_deinit_dma_rx();
    }

    // Handle must be configured before enabling RX interrupt
    // as it might be triggered right away on a misconfigured handle
    furi_hal_serial[handle->id].rx_byte_callback = callback;
    furi_hal_serial[handle->id].handle = handle;
    furi_hal_serial[handle->id].rx_dma_callback = NULL;
    furi_hal_serial[handle->id].context = context;

    if(callback) {
        furi_hal_serial_lpuart_deinit_dma_rx();
        furi_hal_interrupt_set_isr(
            FuriHalInterruptIdLpUart2, furi_hal_serial_lpuart_irq_callback, NULL);
        LL_LPUART_EnableIT_RXNE_RXFNE(LPUART2);
    }
}

void furi_hal_serial_async_rx_start(
    FuriHalSerialHandle* handle,
    FuriHalSerialAsyncRxCallback callback,
    void* context,
    bool report_errors) {
    furi_check(handle);
    furi_check(callback);

    furi_hal_serial_event_init(handle, report_errors);
    furi_hal_serial_async_rx_configure(handle, callback, context);
}

void furi_hal_serial_async_rx_stop(FuriHalSerialHandle* handle) {
    furi_check(handle);
    furi_hal_serial_event_deinit(handle);
    furi_hal_serial_async_rx_configure(handle, NULL, NULL);
}

bool furi_hal_serial_async_rx_available(FuriHalSerialHandle* handle) {
    furi_check(FURI_IS_IRQ_MODE());
    furi_check(handle->id < FuriHalSerialIdMax);

    return LL_LPUART_IsActiveFlag_RXNE_RXFNE(LPUART2);
}

uint8_t furi_hal_serial_async_rx(FuriHalSerialHandle* handle) {
    furi_check(FURI_IS_IRQ_MODE());
    furi_check(handle->id < FuriHalSerialIdMax);

    return LL_LPUART_ReceiveData8(LPUART2);
}

static size_t furi_hal_serial_dma_bytes_available(FuriHalSerialId ch) {
    size_t dma_remain = 0;
    if(ch == FuriHalSerialIdLpuart) {
        dma_remain = LL_DMA_GetDataLength(
            FURI_HAL_SERIAL_LPUART_DMA_INSTANCE, FURI_HAL_SERIAL_LPUART_DMA_CHANNEL);
    } else {
        furi_crash();
    }

    furi_hal_serial[ch].buffer_rx_index_write = FURI_HAL_SERIAL_DMA_BUFFER_SIZE - dma_remain;
    if(furi_hal_serial[ch].buffer_rx_index_write >= furi_hal_serial[ch].buffer_rx_index_read) {
        return furi_hal_serial[ch].buffer_rx_index_write -
               furi_hal_serial[ch].buffer_rx_index_read;
    } else {
        return FURI_HAL_SERIAL_DMA_BUFFER_SIZE - furi_hal_serial[ch].buffer_rx_index_read +
               furi_hal_serial[ch].buffer_rx_index_write;
    }
}

static uint8_t furi_hal_serial_dma_rx_read_byte(FuriHalSerialHandle* handle) {
    uint8_t data = 0;
    data =
        furi_hal_serial[handle->id].buffer_rx_ptr[furi_hal_serial[handle->id].buffer_rx_index_read];
    furi_hal_serial[handle->id].buffer_rx_index_read++;
    if(furi_hal_serial[handle->id].buffer_rx_index_read >= FURI_HAL_SERIAL_DMA_BUFFER_SIZE) {
        furi_hal_serial[handle->id].buffer_rx_index_read = 0;
    }
    return data;
}

size_t furi_hal_serial_dma_rx(FuriHalSerialHandle* handle, uint8_t* data, size_t len) {
    furi_check(FURI_IS_IRQ_MODE());
    furi_check(furi_hal_serial[handle->id].buffer_rx_ptr != NULL);
    size_t i = 0;
    size_t available = furi_hal_serial_dma_bytes_available(handle->id);
    if(available < len) {
        len = available;
    }
    for(i = 0; i < len; i++) {
        data[i] = furi_hal_serial_dma_rx_read_byte(handle);
    }
    return i;
}

static void furi_hal_serial_dma_configure(
    FuriHalSerialHandle* handle,
    FuriHalSerialDmaRxCallback callback,
    void* context) {
    // Disable RXFNE interrupts before unsetting the user callback that reads data
    // Otherwise interrupt runs without reading data and without clearing RXFNE flag
    // This would cause a system hang as the same interrupt runs in loop forever
    if(!callback) {
        LL_LPUART_DisableIT_RXNE_RXFNE(LPUART2);
        furi_hal_interrupt_set_isr(FuriHalInterruptIdLpUart2, NULL, NULL);
        furi_hal_serial_lpuart_deinit_dma_rx();
    }

    // Handle must be configured before enabling RX interrupt
    // as it might be triggered right away on a misconfigured handle
    furi_hal_serial[handle->id].rx_byte_callback = NULL;
    furi_hal_serial[handle->id].handle = handle;
    furi_hal_serial[handle->id].rx_dma_callback = callback;
    furi_hal_serial[handle->id].context = context;

    if(callback) {
        furi_hal_serial_lpuart_init_dma_rx();
        furi_hal_interrupt_set_isr(
            FuriHalInterruptIdLpUart2, furi_hal_serial_lpuart_irq_callback, NULL);
    }
}

void furi_hal_serial_dma_rx_start(
    FuriHalSerialHandle* handle,
    FuriHalSerialDmaRxCallback callback,
    void* context,
    bool report_errors) {
    furi_check(handle);
    furi_check(callback);

    furi_hal_serial_event_init(handle, report_errors);
    furi_hal_serial_dma_configure(handle, callback, context);
}

void furi_hal_serial_dma_rx_stop(FuriHalSerialHandle* handle) {
    furi_check(handle);
    furi_hal_serial_event_deinit(handle);
    furi_hal_serial_dma_configure(handle, NULL, NULL);
}

void furi_hal_serial_enable_direction(
    FuriHalSerialHandle* handle,
    FuriHalSerialDirection direction) {
    furi_check(handle);
    furi_check(handle->id < FuriHalSerialIdMax);
    furi_check(direction < FuriHalSerialDirectionMax);

    USART_TypeDef* periph = furi_hal_serial_config[handle->id].periph;
    furi_hal_serial_config[handle->id].enable[direction](periph);

    const GpioPin* gpio = furi_hal_serial_config[handle->id].gpio[direction];
    const GpioAltFn alt_fn = furi_hal_serial_config[handle->id].alt_fn;

    furi_hal_gpio_init_ex(
        gpio, GpioModeAltFunctionPushPull, GpioPullUp, GpioSpeedVeryHigh, alt_fn);
}

void furi_hal_serial_disable_direction(
    FuriHalSerialHandle* handle,
    FuriHalSerialDirection direction) {
    furi_check(handle);
    furi_check(handle->id < FuriHalSerialIdMax);
    furi_check(direction < FuriHalSerialDirectionMax);

    USART_TypeDef* periph = furi_hal_serial_config[handle->id].periph;
    furi_hal_serial_config[handle->id].disable[direction](periph);

    const GpioPin* gpio = furi_hal_serial_config[handle->id].gpio[direction];

    furi_hal_gpio_init(gpio, GpioModeAnalog, GpioPullNo, GpioSpeedLow);
}

const GpioPin*
    furi_hal_serial_get_gpio_pin(FuriHalSerialHandle* handle, FuriHalSerialDirection direction) {
    furi_check(handle);
    furi_check(handle->id < FuriHalSerialIdMax);
    furi_check(direction < FuriHalSerialDirectionMax);

    return furi_hal_serial_config[handle->id].gpio[direction];
}
