#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/** Early initialization */
void furi_hal_clock_init_early(void);

/** Initialize clocks */
void furi_hal_clock_init(void);

/** Switch clock from HSI to PLL */
void furi_hal_clock_switch_hsi2pll(void);

/** Stop SysTick counter without resetting */
void furi_hal_clock_suspend_tick(void);

/** Continue SysTick counter operation */
void furi_hal_clock_resume_tick(void);

#ifdef __cplusplus
}
#endif
