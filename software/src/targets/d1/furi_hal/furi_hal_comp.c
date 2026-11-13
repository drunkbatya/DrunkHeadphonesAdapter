#include <furi_hal_comp.h>
#include <furi_hal_adc.h>
#include <furi_hal_bus.h>
#include <furi_hal_cortex.h>
#include <furi_hal_gpio.h>
#include <furi_hal_interrupt.h>
#include <furi_hal_resources.h>

#include <stm32g0xx_ll_comp.h>
#include <stm32g0xx_ll_dac.h>
#include <stm32g0xx_ll_exti.h>
#include <furi.h>

#define TAG "FuriHalComp"

#define FURI_HAL_COMP_DAC_INSTANCE  (DAC1)
#define FURI_HAL_COMP_DAC_SETTLE_US (LL_DAC_DELAY_STARTUP_VOLTAGE_SETTLING_US)
#define FURI_HAL_COMP_START_US      (LL_COMP_DELAY_VOLTAGE_SCALER_STAB_US)

typedef struct {
    COMP_TypeDef* comp;
    const GpioPin* gpio;
    uint32_t input_plus;
    uint32_t exti_line;
    uint32_t dac_channel;
    uint32_t dac_input_minus;
} FuriHalCompConfig;

typedef struct {
    FuriHalCompCallback callback;
    void* context;
    bool dac_threshold;
} FuriHalComp;

static const FuriHalCompConfig furi_hal_comp_config[FuriHalCompIdMax] = {
    [FuriHalCompIdPhoneVbus] =
        {
            .comp = COMP1,
            .gpio = &gpio_phone_vbus_sense,
            .input_plus = LL_COMP_INPUT_PLUS_IO3,
            .exti_line = LL_EXTI_LINE_17,
            .dac_channel = LL_DAC_CHANNEL_1,
            .dac_input_minus = LL_COMP_INPUT_MINUS_DAC1_CH1,
        },
    [FuriHalCompIdChargerVbus] =
        {
            .comp = COMP2,
            .gpio = &gpio_charger_vbus_sense,
            .input_plus = LL_COMP_INPUT_PLUS_IO3,
            .exti_line = LL_EXTI_LINE_18,
            .dac_channel = LL_DAC_CHANNEL_2,
            .dac_input_minus = LL_COMP_INPUT_MINUS_DAC1_CH2,
        },
};

static FuriHalComp furi_hal_comp[FuriHalCompIdMax] = {0};

static void furi_hal_comp_isr(void* context) {
    UNUSED(context);

    for(FuriHalCompId id = 0; id < FuriHalCompIdMax; id++) {
        const uint32_t exti_line = furi_hal_comp_config[id].exti_line;
        if(!LL_EXTI_IsActiveRisingFlag_0_31(exti_line) &&
           !LL_EXTI_IsActiveFallingFlag_0_31(exti_line)) {
            continue;
        }
        LL_EXTI_ClearRisingFlag_0_31(exti_line);
        LL_EXTI_ClearFallingFlag_0_31(exti_line);

        const FuriHalComp* comp = &furi_hal_comp[id];
        if(comp->callback) {
            comp->callback(furi_hal_comp_is_above(id), comp->context);
        }
    }
}

static bool furi_hal_comp_is_dac_used(void) {
    for(FuriHalCompId id = 0; id < FuriHalCompIdMax; id++) {
        if(furi_hal_comp[id].dac_threshold) {
            return true;
        }
    }
    return false;
}

static void furi_hal_comp_dac_start(FuriHalCompId id, uint32_t code) {
    const uint32_t channel = furi_hal_comp_config[id].dac_channel;

    if(!furi_hal_comp_is_dac_used()) {
        furi_hal_bus_enable(FuriHalBusDAC1);
    }

    LL_DAC_ConfigOutput(
        FURI_HAL_COMP_DAC_INSTANCE,
        channel,
        LL_DAC_OUTPUT_MODE_NORMAL,
        LL_DAC_OUTPUT_BUFFER_DISABLE,
        LL_DAC_OUTPUT_CONNECT_INTERNAL);
    LL_DAC_ConvertData12RightAligned(FURI_HAL_COMP_DAC_INSTANCE, channel, code);
    LL_DAC_Enable(FURI_HAL_COMP_DAC_INSTANCE, channel);

    furi_hal_cortex_delay_us(FURI_HAL_COMP_DAC_SETTLE_US);

    furi_hal_comp[id].dac_threshold = true;
}

static void furi_hal_comp_dac_stop(FuriHalCompId id) {
    LL_DAC_Disable(FURI_HAL_COMP_DAC_INSTANCE, furi_hal_comp_config[id].dac_channel);

    furi_hal_comp[id].dac_threshold = false;

    if(!furi_hal_comp_is_dac_used()) {
        furi_hal_bus_disable(FuriHalBusDAC1);
    }
}

void furi_hal_comp_init(void) {
    for(FuriHalCompId id = 0; id < FuriHalCompIdMax; id++) {
        const FuriHalCompConfig* config = &furi_hal_comp_config[id];

        furi_hal_gpio_init_simple(config->gpio, GpioModeAnalog);

        LL_COMP_Disable(config->comp);
        LL_COMP_SetPowerMode(config->comp, LL_COMP_POWERMODE_MEDIUMSPEED);
        LL_COMP_SetInputPlus(config->comp, config->input_plus);
        LL_COMP_SetInputMinus(config->comp, LL_COMP_INPUT_MINUS_1_4VREFINT);
        LL_COMP_SetInputHysteresis(config->comp, LL_COMP_HYSTERESIS_LOW);
        LL_COMP_SetOutputPolarity(config->comp, LL_COMP_OUTPUTPOL_NONINVERTED);
        LL_COMP_SetOutputBlankingSource(config->comp, LL_COMP_BLANKINGSRC_NONE);

        LL_EXTI_DisableIT_0_31(config->exti_line);
        LL_EXTI_DisableEvent_0_31(config->exti_line);
        LL_EXTI_EnableRisingTrig_0_31(config->exti_line);
        LL_EXTI_EnableFallingTrig_0_31(config->exti_line);
        LL_EXTI_ClearRisingFlag_0_31(config->exti_line);
        LL_EXTI_ClearFallingFlag_0_31(config->exti_line);
    }

    furi_hal_interrupt_set_isr_ex(
        FuriHalInterruptIdCOMP, FuriHalInterruptPriorityHighest, furi_hal_comp_isr, NULL);

    FURI_LOG_I(TAG, "Init OK");
}

void furi_hal_comp_start(FuriHalCompId id, FuriHalCompCallback callback, void* context) {
    furi_check(id < FuriHalCompIdMax);
    furi_check(callback);

    const FuriHalCompConfig* config = &furi_hal_comp_config[id];
    FuriHalComp* comp = &furi_hal_comp[id];
    furi_check(!comp->callback);

    LL_COMP_Enable(config->comp);
    furi_hal_cortex_delay_us(FURI_HAL_COMP_START_US);

    FURI_CRITICAL_ENTER();
    comp->callback = callback;
    comp->context = context;
    LL_EXTI_ClearRisingFlag_0_31(config->exti_line);
    LL_EXTI_ClearFallingFlag_0_31(config->exti_line);
    LL_EXTI_EnableIT_0_31(config->exti_line);
    FURI_CRITICAL_EXIT();
}

void furi_hal_comp_stop(FuriHalCompId id) {
    furi_check(id < FuriHalCompIdMax);

    const FuriHalCompConfig* config = &furi_hal_comp_config[id];
    FuriHalComp* comp = &furi_hal_comp[id];
    furi_check(comp->callback);

    FURI_CRITICAL_ENTER();
    LL_EXTI_DisableIT_0_31(config->exti_line);
    LL_EXTI_ClearRisingFlag_0_31(config->exti_line);
    LL_EXTI_ClearFallingFlag_0_31(config->exti_line);
    comp->callback = NULL;
    comp->context = NULL;
    FURI_CRITICAL_EXIT();

    LL_COMP_Disable(config->comp);
    LL_COMP_SetInputMinus(config->comp, LL_COMP_INPUT_MINUS_1_4VREFINT);
    if(comp->dac_threshold) {
        furi_hal_comp_dac_stop(id);
    }
}

void furi_hal_comp_set_threshold_quarter_vrefint(FuriHalCompId id) {
    furi_check(id < FuriHalCompIdMax);

    const FuriHalCompConfig* config = &furi_hal_comp_config[id];
    if(!furi_hal_comp[id].dac_threshold) {
        return;
    }

    LL_COMP_SetInputMinus(config->comp, LL_COMP_INPUT_MINUS_1_4VREFINT);
    furi_hal_comp_dac_stop(id);

    if(LL_COMP_IsEnabled(config->comp)) {
        furi_hal_cortex_delay_us(FURI_HAL_COMP_START_US);
    }
}

void furi_hal_comp_set_threshold_mv(FuriHalCompId id, uint32_t threshold_mv) {
    furi_check(id < FuriHalCompIdMax);

    furi_hal_adc_start();
    const uint32_t vdda_mv = furi_hal_adc_read_vdda_mv();
    furi_hal_adc_stop();
    furi_check(threshold_mv < vdda_mv);

    const FuriHalCompConfig* config = &furi_hal_comp_config[id];
    const uint32_t code =
        __LL_DAC_CALC_VOLTAGE_TO_DATA(vdda_mv, threshold_mv, LL_DAC_RESOLUTION_12B);

    if(furi_hal_comp[id].dac_threshold) {
        LL_DAC_ConvertData12RightAligned(FURI_HAL_COMP_DAC_INSTANCE, config->dac_channel, code);
        return;
    }

    furi_hal_comp_dac_start(id, code);
    LL_COMP_SetInputMinus(config->comp, config->dac_input_minus);
}

bool furi_hal_comp_is_above(FuriHalCompId id) {
    furi_check(id < FuriHalCompIdMax);

    return LL_COMP_ReadOutputLevel(furi_hal_comp_config[id].comp) == LL_COMP_OUTPUT_LEVEL_HIGH;
}
