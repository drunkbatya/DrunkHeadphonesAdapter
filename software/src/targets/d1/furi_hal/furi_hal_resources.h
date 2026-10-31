#pragma once

#include <furi.h>

#ifdef __cplusplus
extern "C" {
#endif

extern const GpioPin gpio_swdio;
extern const GpioPin gpio_swclk;

extern const GpioPin gpio_lpuart_tx;
extern const GpioPin gpio_lpuart_rx;

extern const GpioPin gpio_usb_dm;
extern const GpioPin gpio_usb_dp;
extern const GpioPin gpio_usb_mux_select;
extern const GpioPin gpio_usb_bypass_switch;

extern const GpioPin gpio_phone_current_sense;
extern const GpioPin gpio_phone_vbus_sense;
extern const GpioPin gpio_charger_current_sense;
extern const GpioPin gpio_charger_vbus_sense;
extern const GpioPin gpio_headset_buttons;

extern const GpioPin gpio_i2c_scl;
extern const GpioPin gpio_i2c_sda;

extern const GpioPin gpio_i2s_headphones_ck;
extern const GpioPin gpio_i2s_headphones_ws;
extern const GpioPin gpio_i2s_headphones_sd;

extern const GpioPin gpio_i2s_mic_ck;
extern const GpioPin gpio_i2s_mic_ws;
extern const GpioPin gpio_i2s_mic_sd;

extern const GpioPin gpio_ucpd_phone_cc1;
extern const GpioPin gpio_ucpd_phone_cc2;
extern const GpioPin gpio_ucpd_phone_frstx;
extern const GpioPin gpio_ucpd_charger_cc1;
extern const GpioPin gpio_ucpd_charger_cc2;

extern const GpioPin gpio_oscillator_48k_en;
extern const GpioPin gpio_oscillator_44k1_en;

extern const GpioPin gpio_cs43131_reset_n;
extern const GpioPin gpio_cs43131_int;
extern const GpioPin gpio_es8327_int;

extern const GpioPin gpio_tcpp03_phone_en;
extern const GpioPin gpio_tcpp03_phone_flg_n;
extern const GpioPin gpio_tcpp03_charger_flg_n;

void furi_hal_resources_init_early(void);

void furi_hal_resources_init(void);

#ifdef __cplusplus
}
#endif
