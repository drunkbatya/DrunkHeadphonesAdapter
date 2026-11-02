#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Initialize Power HAL
 */
void furi_hal_power_init(void);

/** Get current insomnia level
 *
 * @return     insomnia level: 0 - no insomnia, >0 - insomnia, bearer count.
 */
uint16_t furi_hal_power_insomnia_level(void);

/** Enter insomnia mode Prevents device from going to sleep
 * @warning    Internally increases insomnia level Must be paired with
 *             furi_hal_power_insomnia_exit
 */
void furi_hal_power_insomnia_enter(void);

/** Exit insomnia mode Allow device to go to sleep
 * @warning    Internally decreases insomnia level. Must be paired with
 *             furi_hal_power_insomnia_enter
 */
void furi_hal_power_insomnia_exit(void);

/** Check if sleep available
 *
 * @return     true if available
 */
bool furi_hal_power_sleep_available(void);

/** Go to sleep
 */
void furi_hal_power_sleep(void);

#ifdef __cplusplus
}
#endif
