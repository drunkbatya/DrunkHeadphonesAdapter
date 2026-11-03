#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Timer ISR */
typedef void (*FuriHalInterruptISR)(void* context);

typedef enum {
    // USB, UCPD1, UCPD2
    FuriHalInterruptIdUsb,
    FuriHalInterruptIdUcpd1,
    FuriHalInterruptIdUcpd2,

    // DMA1 Ch1
    FuriHalInterruptIdDma1Ch1,

    // DMA1 Ch2, Ch3
    FuriHalInterruptIdDma1Ch2,
    FuriHalInterruptIdDma1Ch3,

    // DMA1 Ch4-Ch7, DMA2 Ch1-Ch5, DMAMUX1
    FuriHalInterruptIdDma1Ch4,
    FuriHalInterruptIdDma1Ch5,
    FuriHalInterruptIdDma1Ch6,
    FuriHalInterruptIdDma1Ch7,
    FuriHalInterruptIdDma2Ch1,
    FuriHalInterruptIdDma2Ch2,
    FuriHalInterruptIdDma2Ch3,
    FuriHalInterruptIdDma2Ch4,
    FuriHalInterruptIdDma2Ch5,
    FuriHalInterruptIdDmamux1Ovr,

    // ADC1, COMP
    FuriHalInterruptIdAdc1,
    FuriHalInterruptIdCOMP,

    // I2C1
    FuriHalInterruptIdI2c1,

    // SPI1
    FuriHalInterruptIdSpi1,

    // SPI2
    FuriHalInterruptIdSpi2,

    // LPUART2
    FuriHalInterruptIdLpUart2,

    // Service value
    FuriHalInterruptIdMax,
} FuriHalInterruptId;

typedef enum {
    FuriHalInterruptPriorityLowest =
        -1, /**< Lowest priority level, you can use ISR-safe OS primitives */
    FuriHalInterruptPriorityNormal =
        0, /**< Normal(default) priority level, you can use ISR-safe OS primitives */
    FuriHalInterruptPriorityHigh =
        1, /**< High priority level, you can use ISR-safe OS primitives */
    FuriHalInterruptPriorityHighest =
        2, /**< Highest priority level, you can use ISR-safe OS primitives */
} FuriHalInterruptPriority;

/** Initialize interrupt subsystem */
void furi_hal_interrupt_init(void);

/** Set ISR and enable interrupt with default priority
 *
 * @warning    Interrupt flags are not cleared automatically. You may want to
 *             ensure that your peripheral status flags are cleared.
 *
 * @param      index    - interrupt ID
 * @param      isr      - your interrupt service routine or use NULL to clear
 * @param      context  - isr context
 */
void furi_hal_interrupt_set_isr(FuriHalInterruptId index, FuriHalInterruptISR isr, void* context);

/** Set ISR and enable interrupt with custom priority
 *
 * @warning    Interrupt flags are not cleared automatically. You may want to
 *             ensure that your peripheral status flags are cleared.
 *
 * @warning    Interrupts sharing one NVIC vector must use the same priority.
 *
 * @param      index     - interrupt ID
 * @param      priority  - One of FuriHalInterruptPriority
 * @param      isr       - your interrupt service routine or use NULL to clear
 * @param      context   - isr context
 */
void furi_hal_interrupt_set_isr_ex(
    FuriHalInterruptId index,
    FuriHalInterruptPriority priority,
    FuriHalInterruptISR isr,
    void* context);

/** Get interrupt name by exception number.
 * Exception number can be obtained from IPSR register.
 *
 * @param exception_number
 * @return const char* or NULL if interrupt name is not found
 */
const char* furi_hal_interrupt_get_name(uint8_t exception_number);

/** Get total time(in CPU clocks) spent in ISR
 *
 * @return     total time in CPU clocks
 */
uint32_t furi_hal_interrupt_get_time_in_isr_total(void);

#ifdef __cplusplus
}
#endif
