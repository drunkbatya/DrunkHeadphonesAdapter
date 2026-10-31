#include <furi_hal_bus.h>
#include <furi.h>

#include <stm32g0xx_ll_bus.h>

/* Bus bitmask definitions */
#define FURI_HAL_BUS_IGNORE (0x0U)

#define FURI_HAL_BUS_IOP_GRP1                                                         \
    (LL_IOP_GRP1_PERIPH_GPIOA | LL_IOP_GRP1_PERIPH_GPIOB | LL_IOP_GRP1_PERIPH_GPIOC | \
     LL_IOP_GRP1_PERIPH_GPIOD | LL_IOP_GRP1_PERIPH_GPIOE | LL_IOP_GRP1_PERIPH_GPIOF)

#define FURI_HAL_BUS_AHB1_GRP1 \
    (LL_AHB1_GRP1_PERIPH_DMA1 | LL_AHB1_GRP1_PERIPH_DMA2 | LL_AHB1_GRP1_PERIPH_CRC)
//   LL_AHB1_GRP1_PERIPH_FLASH enabled by default

#define FURI_HAL_BUS_APB1_GRP1                                                             \
    (LL_APB1_GRP1_PERIPH_TIM2 | LL_APB1_GRP1_PERIPH_TIM3 | LL_APB1_GRP1_PERIPH_TIM4 |      \
     LL_APB1_GRP1_PERIPH_TIM6 | LL_APB1_GRP1_PERIPH_TIM7 | LL_APB1_GRP1_PERIPH_LPUART2 |   \
     LL_APB1_GRP1_PERIPH_USART5 | LL_APB1_GRP1_PERIPH_USART6 | LL_APB1_GRP1_PERIPH_FDCAN | \
     LL_APB1_GRP1_PERIPH_USB | LL_APB1_GRP1_PERIPH_SPI2 | LL_APB1_GRP1_PERIPH_SPI3 |       \
     LL_APB1_GRP1_PERIPH_CRS | LL_APB1_GRP1_PERIPH_USART2 | LL_APB1_GRP1_PERIPH_USART3 |   \
     LL_APB1_GRP1_PERIPH_USART4 | LL_APB1_GRP1_PERIPH_LPUART1 | LL_APB1_GRP1_PERIPH_I2C1 | \
     LL_APB1_GRP1_PERIPH_I2C2 | LL_APB1_GRP1_PERIPH_I2C3 | LL_APB1_GRP1_PERIPH_CEC |       \
     LL_APB1_GRP1_PERIPH_UCPD1 | LL_APB1_GRP1_PERIPH_UCPD2 | LL_APB1_GRP1_PERIPH_DBGMCU |  \
     LL_APB1_GRP1_PERIPH_PWR | LL_APB1_GRP1_PERIPH_DAC1 | LL_APB1_GRP1_PERIPH_LPTIM2 |     \
     LL_APB1_GRP1_PERIPH_LPTIM1)

#define FURI_HAL_BUS_APB2_GRP1                                                            \
    (LL_APB2_GRP1_PERIPH_SYSCFG | LL_APB2_GRP1_PERIPH_TIM1 | LL_APB2_GRP1_PERIPH_SPI1 |   \
     LL_APB2_GRP1_PERIPH_USART1 | LL_APB2_GRP1_PERIPH_TIM14 | LL_APB2_GRP1_PERIPH_TIM15 | \
     LL_APB2_GRP1_PERIPH_TIM16 | LL_APB2_GRP1_PERIPH_TIM17 | LL_APB2_GRP1_PERIPH_ADC)

/* Test macro definitions */
#define FURI_HAL_BUS_IS_ALL_CLEAR(reg, value) (READ_BIT((reg), (value)) == 0UL)
#define FURI_HAL_BUS_IS_ALL_SET(reg, value)   (READ_BIT((reg), (value)) == (value))

#define FURI_HAL_BUS_IS_CLOCK_ENABLED(bus, value, ...) \
    (FURI_HAL_BUS_IS_ALL_SET(RCC->bus##ENR##__VA_ARGS__, (value)))
#define FURI_HAL_BUS_IS_CLOCK_DISABLED(bus, value, ...) \
    (FURI_HAL_BUS_IS_ALL_CLEAR(RCC->bus##ENR##__VA_ARGS__, (value)))

#define FURI_HAL_BUS_IS_RESET_ASSERTED(bus, value, ...) \
    (FURI_HAL_BUS_IS_ALL_SET(RCC->bus##RSTR##__VA_ARGS__, (value)))
#define FURI_HAL_BUS_IS_RESET_DEASSERTED(bus, value, ...) \
    (FURI_HAL_BUS_IS_ALL_CLEAR(RCC->bus##RSTR##__VA_ARGS__, (value)))

#define FURI_HAL_BUS_IS_PERIPH_ENABLED(bus, value, ...)             \
    (FURI_HAL_BUS_IS_RESET_DEASSERTED(bus, (value), __VA_ARGS__) && \
     FURI_HAL_BUS_IS_CLOCK_ENABLED(bus, (value), __VA_ARGS__))

#define FURI_HAL_BUS_IS_PERIPH_DISABLED(bus, value, ...)          \
    (FURI_HAL_BUS_IS_CLOCK_DISABLED(bus, (value), __VA_ARGS__) && \
     FURI_HAL_BUS_IS_RESET_ASSERTED(bus, (value), __VA_ARGS__))

/* Control macro definitions */
#define FURI_HAL_BUS_RESET_ASSERT(bus, value, grp)   LL_##bus##_GRP##grp##_ForceReset(value)
#define FURI_HAL_BUS_RESET_DEASSERT(bus, value, grp) LL_##bus##_GRP##grp##_ReleaseReset(value)

#define FURI_HAL_BUS_CLOCK_ENABLE(bus, value, grp)  LL_##bus##_GRP##grp##_EnableClock(value)
#define FURI_HAL_BUS_CLOCK_DISABLE(bus, value, grp) LL_##bus##_GRP##grp##_DisableClock(value)

#define FURI_HAL_BUS_PERIPH_ENABLE(bus, value, grp) \
    FURI_HAL_BUS_CLOCK_ENABLE(bus, value, grp);     \
    FURI_HAL_BUS_RESET_DEASSERT(bus, value, grp)

#define FURI_HAL_BUS_PERIPH_DISABLE(bus, value, grp) \
    FURI_HAL_BUS_RESET_ASSERT(bus, value, grp);      \
    FURI_HAL_BUS_CLOCK_DISABLE(bus, value, grp)

#define FURI_HAL_BUS_PERIPH_RESET(bus, value, grp) \
    FURI_HAL_BUS_RESET_ASSERT(bus, value, grp);    \
    FURI_HAL_BUS_RESET_DEASSERT(bus, value, grp)

static const uint32_t furi_hal_bus[] = {
    [FuriHalBusIOP_GRP1] = FURI_HAL_BUS_IOP_GRP1,
    [FuriHalBusGPIOA] = LL_IOP_GRP1_PERIPH_GPIOA,
    [FuriHalBusGPIOB] = LL_IOP_GRP1_PERIPH_GPIOB,
    [FuriHalBusGPIOC] = LL_IOP_GRP1_PERIPH_GPIOC,
    [FuriHalBusGPIOD] = LL_IOP_GRP1_PERIPH_GPIOD,
    [FuriHalBusGPIOE] = LL_IOP_GRP1_PERIPH_GPIOE,
    [FuriHalBusGPIOF] = LL_IOP_GRP1_PERIPH_GPIOF,

    [FuriHalBusAHB1_GRP1] = FURI_HAL_BUS_AHB1_GRP1,
    [FuriHalBusDMA1] = LL_AHB1_GRP1_PERIPH_DMA1,
    [FuriHalBusDMA2] = LL_AHB1_GRP1_PERIPH_DMA2,
    [FuriHalBusFLASH] = LL_AHB1_GRP1_PERIPH_FLASH,
    [FuriHalBusCRC] = LL_AHB1_GRP1_PERIPH_CRC,

    [FuriHalBusAPB1_GRP1] = FURI_HAL_BUS_APB1_GRP1,
    [FuriHalBusTIM2] = LL_APB1_GRP1_PERIPH_TIM2,
    [FuriHalBusTIM3] = LL_APB1_GRP1_PERIPH_TIM3,
    [FuriHalBusTIM4] = LL_APB1_GRP1_PERIPH_TIM4,
    [FuriHalBusTIM6] = LL_APB1_GRP1_PERIPH_TIM6,
    [FuriHalBusTIM7] = LL_APB1_GRP1_PERIPH_TIM7,
    [FuriHalBusLPUART2] = LL_APB1_GRP1_PERIPH_LPUART2,
    [FuriHalBusUSART5] = LL_APB1_GRP1_PERIPH_USART5,
    [FuriHalBusUSART6] = LL_APB1_GRP1_PERIPH_USART6,
    [FuriHalBusFDCAN] = LL_APB1_GRP1_PERIPH_FDCAN,
    [FuriHalBusUSB] = LL_APB1_GRP1_PERIPH_USB,
    [FuriHalBusSPI2] = LL_APB1_GRP1_PERIPH_SPI2,
    [FuriHalBusSPI3] = LL_APB1_GRP1_PERIPH_SPI3,
    [FuriHalBusCRS] = LL_APB1_GRP1_PERIPH_CRS,
    [FuriHalBusUSART2] = LL_APB1_GRP1_PERIPH_USART2,
    [FuriHalBusUSART3] = LL_APB1_GRP1_PERIPH_USART3,
    [FuriHalBusUSART4] = LL_APB1_GRP1_PERIPH_USART4,
    [FuriHalBusLPUART1] = LL_APB1_GRP1_PERIPH_LPUART1,
    [FuriHalBusI2C1] = LL_APB1_GRP1_PERIPH_I2C1,
    [FuriHalBusI2C2] = LL_APB1_GRP1_PERIPH_I2C2,
    [FuriHalBusI2C3] = LL_APB1_GRP1_PERIPH_I2C3,
    [FuriHalBusCEC] = LL_APB1_GRP1_PERIPH_CEC,
    [FuriHalBusUCPD1] = LL_APB1_GRP1_PERIPH_UCPD1,
    [FuriHalBusUCPD2] = LL_APB1_GRP1_PERIPH_UCPD2,
    [FuriHalBusDBGMCU] = LL_APB1_GRP1_PERIPH_DBGMCU,
    [FuriHalBusPWR] = LL_APB1_GRP1_PERIPH_PWR,
    [FuriHalBusDAC1] = LL_APB1_GRP1_PERIPH_DAC1,
    [FuriHalBusLPTIM2] = LL_APB1_GRP1_PERIPH_LPTIM2,
    [FuriHalBusLPTIM1] = LL_APB1_GRP1_PERIPH_LPTIM1,

    [FuriHalBusAPB2_GRP1] = FURI_HAL_BUS_APB2_GRP1,
    [FuriHalBusSYSCFG] = LL_APB2_GRP1_PERIPH_SYSCFG,
    [FuriHalBusTIM1] = LL_APB2_GRP1_PERIPH_TIM1,
    [FuriHalBusSPI1] = LL_APB2_GRP1_PERIPH_SPI1,
    [FuriHalBusUSART1] = LL_APB2_GRP1_PERIPH_USART1,
    [FuriHalBusTIM14] = LL_APB2_GRP1_PERIPH_TIM14,
    [FuriHalBusTIM15] = LL_APB2_GRP1_PERIPH_TIM15,
    [FuriHalBusTIM16] = LL_APB2_GRP1_PERIPH_TIM16,
    [FuriHalBusTIM17] = LL_APB2_GRP1_PERIPH_TIM17,
    [FuriHalBusADC] = LL_APB2_GRP1_PERIPH_ADC,
};

void furi_hal_bus_init_early(void) {
    FURI_CRITICAL_ENTER();

    FURI_HAL_BUS_PERIPH_DISABLE(IOP, FURI_HAL_BUS_IOP_GRP1, 1);
    FURI_HAL_BUS_PERIPH_DISABLE(AHB1, FURI_HAL_BUS_AHB1_GRP1, 1);
    FURI_HAL_BUS_PERIPH_DISABLE(APB1, FURI_HAL_BUS_APB1_GRP1, 1);
    FURI_HAL_BUS_PERIPH_DISABLE(APB2, FURI_HAL_BUS_APB2_GRP1, 1);

    FURI_CRITICAL_EXIT();
}

void furi_hal_bus_enable(FuriHalBus bus) {
    furi_check(bus < FuriHalBusMAX);
    const uint32_t value = furi_hal_bus[bus];
    if(!value) {
        return;
    }

    FURI_CRITICAL_ENTER();
    if(bus < FuriHalBusAHB1_GRP1) {
        furi_check(FURI_HAL_BUS_IS_PERIPH_DISABLED(IOP, value));
        FURI_HAL_BUS_PERIPH_ENABLE(IOP, value, 1);
    } else if(bus < FuriHalBusAPB1_GRP1) {
        furi_check(FURI_HAL_BUS_IS_PERIPH_DISABLED(AHB, value));
        FURI_HAL_BUS_PERIPH_ENABLE(AHB1, value, 1);
    } else if(bus < FuriHalBusAPB2_GRP1) {
        furi_check(FURI_HAL_BUS_IS_PERIPH_DISABLED(APB, value, 1));
        FURI_HAL_BUS_PERIPH_ENABLE(APB1, value, 1);
    } else {
        furi_check(FURI_HAL_BUS_IS_PERIPH_DISABLED(APB, value, 2));
        FURI_HAL_BUS_PERIPH_ENABLE(APB2, value, 1);
    }
    FURI_CRITICAL_EXIT();
}

void furi_hal_bus_reset(FuriHalBus bus) {
    furi_check(bus < FuriHalBusMAX);
    const uint32_t value = furi_hal_bus[bus];
    if(!value) {
        return;
    }

    FURI_CRITICAL_ENTER();
    if(bus < FuriHalBusAHB1_GRP1) {
        furi_check(FURI_HAL_BUS_IS_PERIPH_ENABLED(IOP, value));
        FURI_HAL_BUS_PERIPH_RESET(IOP, value, 1);
    } else if(bus < FuriHalBusAPB1_GRP1) {
        furi_check(FURI_HAL_BUS_IS_PERIPH_ENABLED(AHB, value));
        FURI_HAL_BUS_PERIPH_RESET(AHB1, value, 1);
    } else if(bus < FuriHalBusAPB2_GRP1) {
        furi_check(FURI_HAL_BUS_IS_PERIPH_ENABLED(APB, value, 1));
        FURI_HAL_BUS_PERIPH_RESET(APB1, value, 1);
    } else {
        furi_check(FURI_HAL_BUS_IS_PERIPH_ENABLED(APB, value, 2));
        FURI_HAL_BUS_PERIPH_RESET(APB2, value, 1);
    }
    FURI_CRITICAL_EXIT();
}

void furi_hal_bus_disable(FuriHalBus bus) {
    furi_check(bus < FuriHalBusMAX);
    const uint32_t value = furi_hal_bus[bus];
    if(!value) {
        return;
    }

    FURI_CRITICAL_ENTER();
    if(bus < FuriHalBusAHB1_GRP1) {
        furi_check(FURI_HAL_BUS_IS_PERIPH_ENABLED(IOP, value));
        FURI_HAL_BUS_PERIPH_DISABLE(IOP, value, 1);
    } else if(bus < FuriHalBusAPB1_GRP1) {
        furi_check(FURI_HAL_BUS_IS_PERIPH_ENABLED(AHB, value));
        FURI_HAL_BUS_PERIPH_DISABLE(AHB1, value, 1);
    } else if(bus < FuriHalBusAPB2_GRP1) {
        furi_check(FURI_HAL_BUS_IS_PERIPH_ENABLED(APB, value, 1));
        FURI_HAL_BUS_PERIPH_DISABLE(APB1, value, 1);
    } else {
        furi_check(FURI_HAL_BUS_IS_PERIPH_ENABLED(APB, value, 2));
        FURI_HAL_BUS_PERIPH_DISABLE(APB2, value, 1);
    }
    FURI_CRITICAL_EXIT();
}

bool furi_hal_bus_is_enabled(FuriHalBus bus) {
    furi_check(bus < FuriHalBusMAX);
    const uint32_t value = furi_hal_bus[bus];
    if(value == FURI_HAL_BUS_IGNORE) {
        return true;
    }

    bool ret = false;
    FURI_CRITICAL_ENTER();
    if(bus < FuriHalBusAHB1_GRP1) {
        ret = FURI_HAL_BUS_IS_PERIPH_ENABLED(IOP, value);
    } else if(bus < FuriHalBusAPB1_GRP1) {
        ret = FURI_HAL_BUS_IS_PERIPH_ENABLED(AHB, value);
    } else if(bus < FuriHalBusAPB2_GRP1) {
        ret = FURI_HAL_BUS_IS_PERIPH_ENABLED(APB, value, 1);
    } else {
        ret = FURI_HAL_BUS_IS_PERIPH_ENABLED(APB, value, 2);
    }
    FURI_CRITICAL_EXIT();

    return ret;
}
