#include <furi_hal_clock.h>
#include <furi_hal_bus.h>
#include <furi.h>

#include <stm32g0xx_ll_rcc.h>
#include <stm32g0xx_ll_crs.h>
#include <stm32g0xx_ll_cortex.h>
#include <stm32g0xx_ll_system.h>
#include <stm32g0xx_ll_utils.h>

#define CPU_CLOCK_HSI16_HZ 16000000
#define CPU_CLOCK_PLL_HZ   64000000

#define TICK_INT_PRIORITY 15U

#define HS_CLOCK_IS_READY() (LL_RCC_HSI_IsReady() && LL_RCC_HSI48_IsReady())
#define LS_CLOCK_IS_READY() (LL_RCC_LSI_IsReady())

void furi_hal_clock_init_early(void) {
    LL_SetSystemCoreClock(CPU_CLOCK_HSI16_HZ);
    LL_Init1msTick(SystemCoreClock);
}

void furi_hal_clock_init(void) {
    /* HSI and HSI48 configuration and activation */
    LL_RCC_HSI_Enable();
    LL_RCC_HSI48_Enable();
    while(!HS_CLOCK_IS_READY())
        ;

    /* LSI configuration and activation */
    LL_RCC_LSI_Enable();
    while(!LS_CLOCK_IS_READY())
        ;

    /* Main PLL configuration and activation */
    LL_RCC_PLL_ConfigDomain_SYS(LL_RCC_PLLSOURCE_HSI, LL_RCC_PLLM_DIV_1, 8, LL_RCC_PLLR_DIV_2);
    LL_RCC_PLL_Enable();
    LL_RCC_PLL_EnableDomain_SYS();
    while(LL_RCC_PLL_IsReady() != 1)
        ;

    /* Sysclk activation on the main PLL */
    LL_RCC_SetAHBPrescaler(LL_RCC_SYSCLK_DIV_1);

    /* Prepare Flash memory for work on 64MHz system clock */
    LL_FLASH_SetLatency(LL_FLASH_LATENCY_2);
    while(LL_FLASH_GetLatency() != LL_FLASH_LATENCY_2)
        ;

    LL_RCC_SetSysClkSource(LL_RCC_SYS_CLKSOURCE_PLL);
    while(LL_RCC_GetSysClkSource() != LL_RCC_SYS_CLKSOURCE_STATUS_PLL)
        ;

    /* Set APB1 prescaler*/
    LL_RCC_SetAPB1Prescaler(LL_RCC_APB1_DIV_1);

    /* Update CMSIS variable (which can be updated also through SystemCoreClockUpdate function) */
    LL_SetSystemCoreClock(CPU_CLOCK_PLL_HZ);

    /* Update the time base */
    LL_Init1msTick(SystemCoreClock);
    LL_SYSTICK_EnableIT();
    NVIC_SetPriority(
        SysTick_IRQn, NVIC_EncodePriority(NVIC_GetPriorityGrouping(), TICK_INT_PRIORITY, 0));
    NVIC_EnableIRQ(SysTick_IRQn);

    LL_RCC_SetUSBClockSource(LL_RCC_USB_CLKSOURCE_HSI48);

    furi_hal_bus_enable(FuriHalBusCRS);
    LL_CRS_SetSyncSignalSource(LL_CRS_SYNC_SOURCE_USB);
    LL_CRS_EnableAutoTrimming();
    LL_CRS_EnableFreqErrorCounter();
}
