#include <furi_hal_cortex.h>
#include <furi_hal_bus.h>
#include <furi.h>

#include <stm32g0xx.h>
#include <stm32g0xx_ll_tim.h>

#define FURI_HAL_CORTEX_INSTRUCTIONS_PER_MICROSECOND (SystemCoreClock / 1000000)

void furi_hal_cortex_init_early(void) {
    furi_hal_bus_enable(FuriHalBusTIM2);
    LL_TIM_SetPrescaler(TIM2, 0);
    LL_TIM_SetAutoReload(TIM2, UINT32_MAX);
    LL_TIM_GenerateEvent_UPDATE(TIM2);
    LL_TIM_EnableCounter(TIM2);

    /* Enable instruction prefetch */
    SET_BIT(FLASH->ACR, FLASH_ACR_PRFTEN);
}

void furi_hal_cortex_delay_us(uint32_t microseconds) {
    furi_check(microseconds < (UINT32_MAX / FURI_HAL_CORTEX_INSTRUCTIONS_PER_MICROSECOND));

    uint32_t start = TIM2->CNT;
    uint32_t time_ticks = FURI_HAL_CORTEX_INSTRUCTIONS_PER_MICROSECOND * microseconds;

    while((TIM2->CNT - start) < time_ticks) {
    };
}

uint32_t furi_hal_cortex_instructions_per_microsecond(void) {
    return FURI_HAL_CORTEX_INSTRUCTIONS_PER_MICROSECOND;
}

FURI_WARN_UNUSED FuriHalCortexTimer furi_hal_cortex_timer_get(uint32_t timeout_us) {
    furi_check(timeout_us < (UINT32_MAX / FURI_HAL_CORTEX_INSTRUCTIONS_PER_MICROSECOND));

    FuriHalCortexTimer cortex_timer = {0};
    cortex_timer.start = TIM2->CNT;
    cortex_timer.value = FURI_HAL_CORTEX_INSTRUCTIONS_PER_MICROSECOND * timeout_us;
    return cortex_timer;
}

bool furi_hal_cortex_timer_is_expired(FuriHalCortexTimer cortex_timer) {
    return !((TIM2->CNT - cortex_timer.start) < cortex_timer.value);
}

void furi_hal_cortex_timer_wait(FuriHalCortexTimer cortex_timer) {
    while(!furi_hal_cortex_timer_is_expired(cortex_timer))
        ;
}
