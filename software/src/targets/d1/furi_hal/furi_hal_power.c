#include <furi_hal_power.h>
#include <furi_hal_clock.h>
#include <furi_hal_bus.h>
#include <furi_hal_debug.h>

#include <stm32g0xx_ll_rcc.h>
#include <stm32g0xx_ll_pwr.h>
#include <stm32g0xx_ll_cortex.h>

#include <furi.h>

#define TAG "FuriHalPower"

#ifndef FURI_HAL_POWER_STOP_MODE
#define FURI_HAL_POWER_STOP_MODE (LL_PWR_MODE_STOP1)
#endif

typedef struct {
    volatile uint8_t insomnia;
} FuriHalPower;

static volatile FuriHalPower furi_hal_power = {
    .insomnia = 0,
};

void furi_hal_power_init(void) {
    furi_hal_bus_enable(FuriHalBusPWR);

    LL_PWR_SetRegulVoltageScaling(LL_PWR_REGU_VOLTAGE_SCALE1);

    LL_PWR_SetPowerMode(FURI_HAL_POWER_STOP_MODE);

    FURI_LOG_I(TAG, "Init OK");
}

uint16_t furi_hal_power_insomnia_level(void) {
    return furi_hal_power.insomnia;
}

void furi_hal_power_insomnia_enter(void) {
    FURI_CRITICAL_ENTER();
    furi_check(furi_hal_power.insomnia < UINT8_MAX);
    furi_hal_power.insomnia++;
    FURI_CRITICAL_EXIT();
}

void furi_hal_power_insomnia_exit(void) {
    FURI_CRITICAL_ENTER();
    furi_check(furi_hal_power.insomnia > 0);
    furi_hal_power.insomnia--;
    FURI_CRITICAL_EXIT();
}

bool furi_hal_power_sleep_available(void) {
    return furi_hal_power.insomnia == 0;
}

static inline bool furi_hal_power_deep_sleep_available(void) {
    return !furi_hal_debug_is_gdb_session_active();
}

static inline void furi_hal_power_light_sleep(void) {
    __WFI();
}

static inline void furi_hal_power_deep_sleep(void) {
    // Prepare deep sleep
    LL_LPM_EnableDeepSleep();

    __WFI();

    LL_LPM_EnableSleep();

    if(LL_RCC_GetSysClkSource() == LL_RCC_SYS_CLKSOURCE_STATUS_HSI) {
        furi_hal_clock_switch_hsi2pll();
    } else {
        furi_check(LL_RCC_GetSysClkSource() == LL_RCC_SYS_CLKSOURCE_STATUS_PLL);
    }
}

void furi_hal_power_sleep(void) {
    if(furi_hal_power_deep_sleep_available()) {
        furi_hal_power_deep_sleep();
    } else {
        furi_hal_power_light_sleep();
    }
}
