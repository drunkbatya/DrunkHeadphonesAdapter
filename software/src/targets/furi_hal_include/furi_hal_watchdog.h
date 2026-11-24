#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#define FURI_HAL_WATCHDOG_TIMEOUT_MIN_MS 3800

void furi_hal_watchdog_start(void);

void furi_hal_watchdog_feed(void);

#ifdef __cplusplus
}
#endif
