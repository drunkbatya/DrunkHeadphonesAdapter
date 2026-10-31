#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32g0xx.h"
#include "stdbool.h"

typedef enum {
    FuriHalBusIOP_GRP1,
    FuriHalBusGPIOA,
    FuriHalBusGPIOB,
    FuriHalBusGPIOC,
    FuriHalBusGPIOD,
    FuriHalBusGPIOE,
    FuriHalBusGPIOF,

    FuriHalBusAHB1_GRP1,
    FuriHalBusDMA1,
    FuriHalBusDMA2,
    FuriHalBusFLASH,
    FuriHalBusCRC,

    FuriHalBusAPB1_GRP1,
    FuriHalBusTIM2,
    FuriHalBusTIM3,
    FuriHalBusTIM4,
    FuriHalBusTIM6,
    FuriHalBusTIM7,
    FuriHalBusLPUART2,
    FuriHalBusUSART5,
    FuriHalBusUSART6,
    FuriHalBusFDCAN,
    FuriHalBusUSB,
    FuriHalBusSPI2,
    FuriHalBusSPI3,
    FuriHalBusCRS,
    FuriHalBusUSART2,
    FuriHalBusUSART3,
    FuriHalBusUSART4,
    FuriHalBusLPUART1,
    FuriHalBusI2C1,
    FuriHalBusI2C2,
    FuriHalBusI2C3,
    FuriHalBusCEC,
    FuriHalBusUCPD1,
    FuriHalBusUCPD2,
    FuriHalBusDBGMCU,
    FuriHalBusPWR,
    FuriHalBusDAC1,
    FuriHalBusLPTIM2,
    FuriHalBusLPTIM1,

    FuriHalBusAPB2_GRP1,
    FuriHalBusSYSCFG,
    FuriHalBusTIM1,
    FuriHalBusSPI1,
    FuriHalBusUSART1,
    FuriHalBusTIM14,
    FuriHalBusTIM15,
    FuriHalBusTIM16,
    FuriHalBusTIM17,
    FuriHalBusADC,

    FuriHalBusMAX,
} FuriHalBus;

/** Early initialization */
void furi_hal_bus_init_early(void);

/**
 * Enable a peripheral by turning the clocking on and deasserting the reset.
 * @param [in] bus Peripheral to be enabled.
 * @warning Peripheral must be in disabled state in order to be enabled.
 */
void furi_hal_bus_enable(FuriHalBus bus);

/**
 * Reset a peripheral by sequentially asserting and deasserting the reset.
 * @param [in] bus Peripheral to be reset.
 * @warning Peripheral must be in enabled state in order to be reset.
 */
void furi_hal_bus_reset(FuriHalBus bus);

/**
 * Disable a peripheral by turning the clocking off and asserting the reset.
 * @param [in] bus Peripheral to be disabled.
 * @warning Peripheral must be in enabled state in order to be disabled.
 */
void furi_hal_bus_disable(FuriHalBus bus);

/** Check if peripheral is enabled
 *
 * @param[in]  bus   The peripheral to check
 *
 * @return     true if enabled or always enabled, false otherwise
 */
bool furi_hal_bus_is_enabled(FuriHalBus bus);

#ifdef __cplusplus
}
#endif
