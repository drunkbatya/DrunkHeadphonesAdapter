#include <furi_hal_debug.h>

#include <stm32g0xx_ll_system.h>

#include <furi_hal_bus.h>
#include <furi_hal_gpio.h>
#include <furi_hal_resources.h>

volatile bool furi_hal_debug_gdb_session_active = false;

void furi_hal_debug_enable(void) {
    if(!furi_hal_bus_is_enabled(FuriHalBusDBGMCU)) {
        furi_hal_bus_enable(FuriHalBusDBGMCU);
    }
    // Low power mode debug
    LL_DBGMCU_EnableDBGStopMode();
    LL_DBGMCU_EnableDBGStandbyMode();
    // SWD GPIO
    furi_hal_gpio_init_ex(
        &gpio_swdio, GpioModeAltFunctionPushPull, GpioPullUp, GpioSpeedVeryHigh, GpioAltFn0SWD);
    furi_hal_gpio_init_ex(
        &gpio_swclk, GpioModeAltFunctionPushPull, GpioPullDown, GpioSpeedLow, GpioAltFn0SWD);
}

void furi_hal_debug_disable(void) {
    // Low power mode debug
    if(!furi_hal_bus_is_enabled(FuriHalBusDBGMCU)) {
        furi_hal_bus_enable(FuriHalBusDBGMCU);
    }
    LL_DBGMCU_DisableDBGStopMode();
    LL_DBGMCU_DisableDBGStandbyMode();
    furi_hal_bus_disable(FuriHalBusDBGMCU);
    // SWD GPIO
    furi_hal_gpio_init_simple(&gpio_swdio, GpioModeAnalog);
    furi_hal_gpio_init_simple(&gpio_swclk, GpioModeAnalog);
}

bool furi_hal_debug_is_gdb_session_active(void) {
    return furi_hal_debug_gdb_session_active;
}
