#include <furi_hal_watchdog.h>

#include <stm32g0xx_ll_iwdg.h>

#define FURI_HAL_WATCHDOG_PRESCALER LL_IWDG_PRESCALER_32
#define FURI_HAL_WATCHDOG_RELOAD    0xFFF

void furi_hal_watchdog_start(void) {
    LL_IWDG_Enable(IWDG);
    LL_IWDG_EnableWriteAccess(IWDG);
    LL_IWDG_SetPrescaler(IWDG, FURI_HAL_WATCHDOG_PRESCALER);
    LL_IWDG_SetReloadCounter(IWDG, FURI_HAL_WATCHDOG_RELOAD);
    while(!LL_IWDG_IsReady(IWDG)) {
    }
    LL_IWDG_ReloadCounter(IWDG);
}

void furi_hal_watchdog_feed(void) {
    LL_IWDG_ReloadCounter(IWDG);
}
