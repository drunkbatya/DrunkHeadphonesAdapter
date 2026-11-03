#include <furi_hal_interrupt.h>
#include <furi_hal_os.h>

#include <furi.h>

#include <stm32g0xx.h>

#define TAG "FuriHalInterrupt"

#define FURI_HAL_INTERRUPT_DEFAULT_PRIORITY 2

#define FURI_HAL_INTERRUPT_ACCOUNT_START() const uint32_t _isr_start = TIM2->CNT;
#define FURI_HAL_INTERRUPT_ACCOUNT_END()                  \
    const uint32_t _time_in_isr = TIM2->CNT - _isr_start; \
    furi_hal_interrupt.counter_time_in_isr_total += _time_in_isr;

typedef struct {
    FuriHalInterruptISR isr;
    void* context;
} FuriHalInterruptISRPair;

typedef struct {
    FuriHalInterruptISRPair isr[FuriHalInterruptIdMax];
    uint32_t counter_time_in_isr_total;
} FuriHalIterrupt;

static FuriHalIterrupt furi_hal_interrupt = {};

const IRQn_Type furi_hal_interrupt_irqn[FuriHalInterruptIdMax] = {
    // USB, UCPD1, UCPD2
    [FuriHalInterruptIdUsb] = USB_UCPD1_2_IRQn,
    [FuriHalInterruptIdUcpd1] = USB_UCPD1_2_IRQn,
    [FuriHalInterruptIdUcpd2] = USB_UCPD1_2_IRQn,

    // DMA1 Ch1
    [FuriHalInterruptIdDma1Ch1] = DMA1_Channel1_IRQn,

    // DMA1 Ch2, Ch3
    [FuriHalInterruptIdDma1Ch2] = DMA1_Channel2_3_IRQn,
    [FuriHalInterruptIdDma1Ch3] = DMA1_Channel2_3_IRQn,

    // DMA1 Ch4-Ch7, DMA2 Ch1-Ch5, DMAMUX1
    [FuriHalInterruptIdDma1Ch4] = DMA1_Ch4_7_DMA2_Ch1_5_DMAMUX1_OVR_IRQn,
    [FuriHalInterruptIdDma1Ch5] = DMA1_Ch4_7_DMA2_Ch1_5_DMAMUX1_OVR_IRQn,
    [FuriHalInterruptIdDma1Ch6] = DMA1_Ch4_7_DMA2_Ch1_5_DMAMUX1_OVR_IRQn,
    [FuriHalInterruptIdDma1Ch7] = DMA1_Ch4_7_DMA2_Ch1_5_DMAMUX1_OVR_IRQn,
    [FuriHalInterruptIdDma2Ch1] = DMA1_Ch4_7_DMA2_Ch1_5_DMAMUX1_OVR_IRQn,
    [FuriHalInterruptIdDma2Ch2] = DMA1_Ch4_7_DMA2_Ch1_5_DMAMUX1_OVR_IRQn,
    [FuriHalInterruptIdDma2Ch3] = DMA1_Ch4_7_DMA2_Ch1_5_DMAMUX1_OVR_IRQn,
    [FuriHalInterruptIdDma2Ch4] = DMA1_Ch4_7_DMA2_Ch1_5_DMAMUX1_OVR_IRQn,
    [FuriHalInterruptIdDma2Ch5] = DMA1_Ch4_7_DMA2_Ch1_5_DMAMUX1_OVR_IRQn,
    [FuriHalInterruptIdDmamux1Ovr] = DMA1_Ch4_7_DMA2_Ch1_5_DMAMUX1_OVR_IRQn,

    // ADC1, COMP
    [FuriHalInterruptIdAdc1] = ADC1_COMP_IRQn,
    [FuriHalInterruptIdCOMP] = ADC1_COMP_IRQn,

    // I2C1
    [FuriHalInterruptIdI2c1] = I2C1_IRQn,

    // SPI1
    [FuriHalInterruptIdSpi1] = SPI1_IRQn,

    // SPI2
    [FuriHalInterruptIdSpi2] = SPI2_3_IRQn,

    // LPUART2
    [FuriHalInterruptIdLpUart2] = USART2_LPUART2_IRQn,
};

FURI_ALWAYS_INLINE static void furi_hal_interrupt_call(FuriHalInterruptId index) {
    const FuriHalInterruptISRPair* isr_descr = &furi_hal_interrupt.isr[index];
    furi_check(isr_descr->isr);

    FURI_HAL_INTERRUPT_ACCOUNT_START();
    isr_descr->isr(isr_descr->context);
    FURI_HAL_INTERRUPT_ACCOUNT_END();
}

FURI_ALWAYS_INLINE static void
    furi_hal_interrupt_call_shared(FuriHalInterruptId first, FuriHalInterruptId last) {
    bool called = false;

    FURI_HAL_INTERRUPT_ACCOUNT_START();
    for(FuriHalInterruptId index = first; index <= last; index++) {
        const FuriHalInterruptISRPair* isr_descr = &furi_hal_interrupt.isr[index];
        if(isr_descr->isr) {
            isr_descr->isr(isr_descr->context);
            called = true;
        }
    }
    FURI_HAL_INTERRUPT_ACCOUNT_END();

    furi_check(called);
}

FURI_ALWAYS_INLINE static void
    furi_hal_interrupt_enable(FuriHalInterruptId index, uint16_t priority) {
    NVIC_SetPriority(
        furi_hal_interrupt_irqn[index],
        NVIC_EncodePriority(NVIC_GetPriorityGrouping(), priority, 0));
    NVIC_EnableIRQ(furi_hal_interrupt_irqn[index]);
}

FURI_ALWAYS_INLINE static void furi_hal_interrupt_clear_pending(FuriHalInterruptId index) {
    NVIC_ClearPendingIRQ(furi_hal_interrupt_irqn[index]);
}

FURI_ALWAYS_INLINE static void furi_hal_interrupt_disable(FuriHalInterruptId index) {
    NVIC_DisableIRQ(furi_hal_interrupt_irqn[index]);
}

static bool furi_hal_interrupt_is_vector_shared(FuriHalInterruptId index) {
    for(FuriHalInterruptId other = 0; other < FuriHalInterruptIdMax; other++) {
        if(other != index && furi_hal_interrupt.isr[other].isr &&
           furi_hal_interrupt_irqn[other] == furi_hal_interrupt_irqn[index]) {
            return true;
        }
    }
    return false;
}

void furi_hal_interrupt_init(void) {
    NVIC_SetPriority(SVCall_IRQn, NVIC_EncodePriority(NVIC_GetPriorityGrouping(), 0, 0));
    NVIC_SetPriority(PendSV_IRQn, NVIC_EncodePriority(NVIC_GetPriorityGrouping(), 15, 0));

    FURI_LOG_I(TAG, "Init OK");
}

void furi_hal_interrupt_set_isr(FuriHalInterruptId index, FuriHalInterruptISR isr, void* context) {
    furi_hal_interrupt_set_isr_ex(index, FuriHalInterruptPriorityNormal, isr, context);
}

void furi_hal_interrupt_set_isr_ex(
    FuriHalInterruptId index,
    FuriHalInterruptPriority priority,
    FuriHalInterruptISR isr,
    void* context) {
    furi_check(index < FuriHalInterruptIdMax);
    furi_check(
        priority >= FuriHalInterruptPriorityLowest && priority <= FuriHalInterruptPriorityHighest);

    uint16_t real_priority = FURI_HAL_INTERRUPT_DEFAULT_PRIORITY - priority;

    FuriHalInterruptISRPair* isr_descr = &furi_hal_interrupt.isr[index];
    bool vector_shared = furi_hal_interrupt_is_vector_shared(index);
    if(isr) {
        // Pre ISR set
        furi_check(isr_descr->isr == NULL);
        if(vector_shared) {
            furi_check(NVIC_GetPriority(furi_hal_interrupt_irqn[index]) == real_priority);
        }
    } else if(!vector_shared) {
        // Pre ISR clear
        furi_hal_interrupt_disable(index);
        furi_hal_interrupt_clear_pending(index);
    }

    isr_descr->isr = isr;
    isr_descr->context = context;
    __DMB();

    if(isr && !vector_shared) {
        // Post ISR set
        furi_hal_interrupt_clear_pending(index);
        furi_hal_interrupt_enable(index, real_priority);
    }
}

void USB_UCPD1_2_IRQHandler(void) {
    furi_hal_interrupt_call_shared(FuriHalInterruptIdUsb, FuriHalInterruptIdUcpd2);
}

/* DMA 1 */
void DMA1_Channel1_IRQHandler(void) {
    furi_hal_interrupt_call(FuriHalInterruptIdDma1Ch1);
}

void DMA1_Channel2_3_IRQHandler(void) {
    furi_hal_interrupt_call_shared(FuriHalInterruptIdDma1Ch2, FuriHalInterruptIdDma1Ch3);
}

/* DMA 1, DMA 2, DMAMUX 1 */
void DMA1_Ch4_7_DMA2_Ch1_5_DMAMUX1_OVR_IRQHandler(void) {
    furi_hal_interrupt_call_shared(FuriHalInterruptIdDma1Ch4, FuriHalInterruptIdDmamux1Ovr);
}

void ADC1_COMP_IRQHandler(void) {
    furi_hal_interrupt_call_shared(FuriHalInterruptIdAdc1, FuriHalInterruptIdCOMP);
}

void I2C1_IRQHandler(void) {
    furi_hal_interrupt_call(FuriHalInterruptIdI2c1);
}

void SPI1_IRQHandler(void) {
    furi_hal_interrupt_call(FuriHalInterruptIdSpi1);
}

void SPI2_3_IRQHandler(void) {
    furi_hal_interrupt_call(FuriHalInterruptIdSpi2);
}

void USART2_LPUART2_IRQHandler(void) {
    furi_hal_interrupt_call(FuriHalInterruptIdLpUart2);
}

void NMI_Handler(void) {
    furi_crash("NMI");
}

void HardFault_Handler(void) {
    furi_crash("HardFault");
}

void SysTick_Handler(void) {
    FURI_HAL_INTERRUPT_ACCOUNT_START();
    furi_hal_os_tick();
    FURI_HAL_INTERRUPT_ACCOUNT_END();
}

const char* furi_hal_interrupt_get_name(uint8_t exception_number) {
    int32_t id = (int32_t)exception_number - 16;

    switch(id) {
    case -14:
        return "NMI";
    case -13:
        return "HardFault";
    case -5:
        return "SVC";
    case -2:
        return "PendSV";
    case -1:
        return "SysTick";
    case 0:
        return "WWDG";
    case 1:
        return "PVD_VDDIO2";
    case 2:
        return "RTC_TAMP";
    case 3:
        return "FLASH";
    case 4:
        return "RCC_CRS";
    case 5:
        return "EXTI0_1";
    case 6:
        return "EXTI2_3";
    case 7:
        return "EXTI4_15";
    case 8:
        return "USB_UCPD1_2";
    case 9:
        return "DMA1_Ch1";
    case 10:
        return "DMA1_Ch2_3";
    case 11:
        return "DMA1_Ch4_7_DMA2_Ch1_5_DMAMUX1_OVR";
    case 12:
        return "ADC1_COMP";
    case 13:
        return "TIM1_BRK_UP_TRG_COM";
    case 14:
        return "TIM1_CC";
    case 15:
        return "TIM2";
    case 16:
        return "TIM3_TIM4";
    case 17:
        return "TIM6_DAC_LPTIM1";
    case 18:
        return "TIM7_LPTIM2";
    case 19:
        return "TIM14";
    case 20:
        return "TIM15";
    case 21:
        return "TIM16_FDCAN_IT0";
    case 22:
        return "TIM17_FDCAN_IT1";
    case 23:
        return "I2C1";
    case 24:
        return "I2C2_3";
    case 25:
        return "SPI1";
    case 26:
        return "SPI2_3";
    case 27:
        return "USART1";
    case 28:
        return "USART2_LPUART2";
    case 29:
        return "USART3_4_5_6_LPUART1";
    case 30:
        return "CEC";
    default:
        return NULL;
    }
}

uint32_t furi_hal_interrupt_get_time_in_isr_total(void) {
    return furi_hal_interrupt.counter_time_in_isr_total;
}
