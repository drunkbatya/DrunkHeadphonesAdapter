#include <furi_hal_resources.h>
#include <furi.h>

#include <stm32g0xx_ll_bus.h>

const GpioPin gpio_swdio = {.port = GPIOA, .pin = LL_GPIO_PIN_13};
const GpioPin gpio_swclk = {.port = GPIOA, .pin = LL_GPIO_PIN_14};

const GpioPin gpio_lpuart_tx = {.port = GPIOC, .pin = LL_GPIO_PIN_6};
const GpioPin gpio_lpuart_rx = {.port = GPIOC, .pin = LL_GPIO_PIN_7};

const GpioPin gpio_usb_dm = {.port = GPIOA, .pin = LL_GPIO_PIN_11};
const GpioPin gpio_usb_dp = {.port = GPIOA, .pin = LL_GPIO_PIN_12};
const GpioPin gpio_usb_mux_select = {.port = GPIOD, .pin = LL_GPIO_PIN_8};
const GpioPin gpio_usb_bypass_switch = {.port = GPIOD, .pin = LL_GPIO_PIN_9};

const GpioPin gpio_phone_current_sense = {.port = GPIOA, .pin = LL_GPIO_PIN_0};
const GpioPin gpio_phone_vbus_sense = {.port = GPIOA, .pin = LL_GPIO_PIN_1};
const GpioPin gpio_charger_current_sense = {.port = GPIOA, .pin = LL_GPIO_PIN_2};
const GpioPin gpio_charger_vbus_sense = {.port = GPIOA, .pin = LL_GPIO_PIN_3};
const GpioPin gpio_headset_buttons = {.port = GPIOA, .pin = LL_GPIO_PIN_4};

const GpioPin gpio_i2c_scl = {.port = GPIOB, .pin = LL_GPIO_PIN_6};
const GpioPin gpio_i2c_sda = {.port = GPIOB, .pin = LL_GPIO_PIN_7};

const GpioPin gpio_i2s_headphones_ck = {.port = GPIOA, .pin = LL_GPIO_PIN_5};
const GpioPin gpio_i2s_headphones_ws = {.port = GPIOB, .pin = LL_GPIO_PIN_0};
const GpioPin gpio_i2s_headphones_sd = {.port = GPIOA, .pin = LL_GPIO_PIN_7};

const GpioPin gpio_i2s_mic_ck = {.port = GPIOB, .pin = LL_GPIO_PIN_10};
const GpioPin gpio_i2s_mic_ws = {.port = GPIOB, .pin = LL_GPIO_PIN_9};
const GpioPin gpio_i2s_mic_sd = {.port = GPIOC, .pin = LL_GPIO_PIN_3};

const GpioPin gpio_ucpd_phone_cc1 = {.port = GPIOA, .pin = LL_GPIO_PIN_8};
const GpioPin gpio_ucpd_phone_cc2 = {.port = GPIOB, .pin = LL_GPIO_PIN_15};
const GpioPin gpio_ucpd_phone_frstx = {.port = GPIOC, .pin = LL_GPIO_PIN_12};
const GpioPin gpio_ucpd_charger_cc1 = {.port = GPIOD, .pin = LL_GPIO_PIN_0};
const GpioPin gpio_ucpd_charger_cc2 = {.port = GPIOD, .pin = LL_GPIO_PIN_2};

const GpioPin gpio_oscillator_48k_en = {.port = GPIOD, .pin = LL_GPIO_PIN_5};
const GpioPin gpio_oscillator_44k1_en = {.port = GPIOD, .pin = LL_GPIO_PIN_6};

const GpioPin gpio_cs43131_reset_n = {.port = GPIOD, .pin = LL_GPIO_PIN_7};
const GpioPin gpio_cs43131_int = {.port = GPIOE, .pin = LL_GPIO_PIN_0};
const GpioPin gpio_es8327_int = {.port = GPIOE, .pin = LL_GPIO_PIN_1};

const GpioPin gpio_tcpp03_phone_en = {.port = GPIOF, .pin = LL_GPIO_PIN_9};
const GpioPin gpio_tcpp03_phone_flg_n = {.port = GPIOE, .pin = LL_GPIO_PIN_2};
const GpioPin gpio_tcpp03_charger_flg_n = {.port = GPIOE, .pin = LL_GPIO_PIN_3};

static void furi_hal_resources_init_output_low(const GpioPin* gpio) {
    furi_hal_gpio_write(gpio, false);
    furi_hal_gpio_init_simple(gpio, GpioModeOutputPushPull);
}

void furi_hal_resources_init_early(void) {
    // TODO: furi_hal_bus_enable when furi_hal_bus is ported
    LL_IOP_GRP1_EnableClock(
        LL_IOP_GRP1_PERIPH_GPIOA | LL_IOP_GRP1_PERIPH_GPIOB | LL_IOP_GRP1_PERIPH_GPIOC |
        LL_IOP_GRP1_PERIPH_GPIOD | LL_IOP_GRP1_PERIPH_GPIOE | LL_IOP_GRP1_PERIPH_GPIOF);

    furi_hal_resources_init_output_low(&gpio_tcpp03_phone_en);
    furi_hal_resources_init_output_low(&gpio_usb_mux_select);
    furi_hal_resources_init_output_low(&gpio_cs43131_reset_n);
    furi_hal_resources_init_output_low(&gpio_oscillator_48k_en);
    furi_hal_resources_init_output_low(&gpio_oscillator_44k1_en);
}

void furi_hal_resources_init(void) {
    furi_hal_gpio_init(
        &gpio_usb_bypass_switch, GpioModeInterruptRiseFall, GpioPullNo, GpioSpeedLow);

    furi_hal_gpio_init(&gpio_cs43131_int, GpioModeInterruptFall, GpioPullNo, GpioSpeedLow);
    furi_hal_gpio_init(&gpio_es8327_int, GpioModeInterruptFall, GpioPullNo, GpioSpeedLow);
    furi_hal_gpio_init(&gpio_tcpp03_phone_flg_n, GpioModeInterruptFall, GpioPullNo, GpioSpeedLow);
    furi_hal_gpio_init(
        &gpio_tcpp03_charger_flg_n, GpioModeInterruptFall, GpioPullNo, GpioSpeedLow);

    NVIC_SetPriority(EXTI0_1_IRQn, 2);
    NVIC_EnableIRQ(EXTI0_1_IRQn);

    NVIC_SetPriority(EXTI2_3_IRQn, 2);
    NVIC_EnableIRQ(EXTI2_3_IRQn);

    NVIC_SetPriority(EXTI4_15_IRQn, 2);
    NVIC_EnableIRQ(EXTI4_15_IRQn);
}
