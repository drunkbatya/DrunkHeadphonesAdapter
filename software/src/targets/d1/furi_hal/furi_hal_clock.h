#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/** Early initialization */
void furi_hal_clock_init_early(void);

/** Initialize clocks */
void furi_hal_clock_init(void);

#ifdef __cplusplus
}
#endif
