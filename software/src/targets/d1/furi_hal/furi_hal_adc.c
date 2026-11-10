#include <furi_hal_adc.h>
#include <furi_hal_bus.h>
#include <furi_hal_cortex.h>
#include <furi_hal_gpio.h>
#include <furi_hal_power.h>
#include <furi_hal_resources.h>

#include <stm32g0xx_ll_adc.h>
#include <stm32g0xx_ll_dma.h>
#include <stm32g0xx_ll_rcc.h>
#include <furi.h>

#define TAG "FuriHalAdc"

#define FURI_HAL_ADC_INSTANCE          (ADC1)
#define FURI_HAL_ADC_COMMON_INSTANCE   (ADC1_COMMON)
#define FURI_HAL_ADC_DMA_INSTANCE      (DMA2)
#define FURI_HAL_ADC_DMA_CHANNEL       (LL_DMA_CHANNEL_1)
#define FURI_HAL_ADC_CLOCK_PRESCALER   (LL_ADC_CLOCK_ASYNC_DIV16)
#define FURI_HAL_ADC_CLOCK_DIVIDER     16
#define FURI_HAL_ADC_CLOCK_HZ          (HSI_VALUE / FURI_HAL_ADC_CLOCK_DIVIDER)
#define FURI_HAL_ADC_SAMPLING_TIME     (LL_ADC_SAMPLINGTIME_39CYCLES_5)
#define FURI_HAL_ADC_CALIBRATION_COUNT 8
#define FURI_HAL_ADC_STATE_TIMEOUT_US  1000
#define FURI_HAL_ADC_US_PER_SECOND     1000000
#define FURI_HAL_ADC_CYCLES_TO_US(cycles) \
    (((cycles) * FURI_HAL_ADC_US_PER_SECOND + FURI_HAL_ADC_CLOCK_HZ - 1) / FURI_HAL_ADC_CLOCK_HZ)

#define FURI_HAL_ADC_RANK_TEMPERATURE (FuriHalAdcChannelMax)
#define FURI_HAL_ADC_RANK_VREFINT     (FuriHalAdcChannelMax + 1)
#define FURI_HAL_ADC_RANK_COUNT       (FuriHalAdcChannelMax + 2)

typedef struct {
    uint32_t channel;
    const GpioPin* gpio;
} FuriHalAdcRank;

typedef struct {
    FuriMutex* mutex;
    uint32_t user_count;
} FuriHalAdc;

static const FuriHalAdcRank furi_hal_adc_ranks[FURI_HAL_ADC_RANK_COUNT] = {
    [FuriHalAdcChannelPhoneCurrentSense] =
        {
            .channel = LL_ADC_CHANNEL_0,
            .gpio = &gpio_phone_current_sense,
        },
    [FuriHalAdcChannelPhoneVbusSense] =
        {
            .channel = LL_ADC_CHANNEL_1,
            .gpio = &gpio_phone_vbus_sense,
        },
    [FuriHalAdcChannelChargerCurrentSense] =
        {
            .channel = LL_ADC_CHANNEL_2,
            .gpio = &gpio_charger_current_sense,
        },
    [FuriHalAdcChannelChargerVbusSense] =
        {
            .channel = LL_ADC_CHANNEL_3,
            .gpio = &gpio_charger_vbus_sense,
        },
    [FuriHalAdcChannelHeadsetButtons] =
        {
            .channel = LL_ADC_CHANNEL_4,
            .gpio = &gpio_headset_buttons,
        },
    [FURI_HAL_ADC_RANK_TEMPERATURE] =
        {
            .channel = LL_ADC_CHANNEL_TEMPSENSOR,
            .gpio = NULL,
        },
    [FURI_HAL_ADC_RANK_VREFINT] =
        {
            .channel = LL_ADC_CHANNEL_VREFINT,
            .gpio = NULL,
        },
};

static FuriHalAdc furi_hal_adc = {0};

static volatile uint16_t furi_hal_adc_samples[FURI_HAL_ADC_RANK_COUNT];

static bool
    furi_hal_adc_wait_flag(uint32_t (*read_flag)(const ADC_TypeDef* adc), uint32_t expected) {
    FuriHalCortexTimer timer = furi_hal_cortex_timer_get(FURI_HAL_ADC_STATE_TIMEOUT_US);
    while(!furi_hal_cortex_timer_is_expired(timer)) {
        if(read_flag(FURI_HAL_ADC_INSTANCE) == expected) {
            return true;
        }
    }
    return read_flag(FURI_HAL_ADC_INSTANCE) == expected;
}

static bool furi_hal_adc_wait_first_scan(void) {
    FuriHalCortexTimer timer = furi_hal_cortex_timer_get(FURI_HAL_ADC_STATE_TIMEOUT_US);
    while(!furi_hal_cortex_timer_is_expired(timer)) {
        if(LL_DMA_IsActiveFlag_TC1(FURI_HAL_ADC_DMA_INSTANCE)) {
            return true;
        }
    }
    return LL_DMA_IsActiveFlag_TC1(FURI_HAL_ADC_DMA_INSTANCE);
}

static uint32_t furi_hal_adc_calibrate(void) {
    uint32_t calibration_factor_sum = 0;

    for(size_t i = 0; i < FURI_HAL_ADC_CALIBRATION_COUNT; i++) {
        LL_ADC_StartCalibration(FURI_HAL_ADC_INSTANCE);
        furi_check(
            furi_hal_adc_wait_flag(LL_ADC_IsCalibrationOnGoing, 0), "ADC calibration timeout");
        calibration_factor_sum += LL_ADC_GetCalibrationFactor(FURI_HAL_ADC_INSTANCE) + 1;
    }

    uint32_t calibration_factor = (calibration_factor_sum + FURI_HAL_ADC_CALIBRATION_COUNT / 2) /
                                  FURI_HAL_ADC_CALIBRATION_COUNT;
    if(calibration_factor > ADC_CALFACT_CALFACT) {
        calibration_factor = ADC_CALFACT_CALFACT;
    }

    return calibration_factor;
}

static void furi_hal_adc_start_scan(void) {
    furi_hal_power_insomnia_enter();

    LL_RCC_SetADCClockSource(LL_RCC_ADC_CLKSOURCE_HSI);
    furi_hal_bus_enable(FuriHalBusADC);

    LL_ADC_CommonInitTypeDef common_init = {
        .CommonClock = FURI_HAL_ADC_CLOCK_PRESCALER,
    };
    furi_check(LL_ADC_CommonInit(FURI_HAL_ADC_COMMON_INSTANCE, &common_init) == SUCCESS);
    LL_ADC_SetCommonPathInternalCh(
        FURI_HAL_ADC_COMMON_INSTANCE,
        LL_ADC_PATH_INTERNAL_VREFINT | LL_ADC_PATH_INTERNAL_TEMPSENSOR);

    LL_ADC_InitTypeDef init = {
        .Clock = LL_ADC_CLOCK_ASYNC,
        .Resolution = LL_ADC_RESOLUTION_12B,
        .DataAlignment = LL_ADC_DATA_ALIGN_RIGHT,
        .LowPowerMode = LL_ADC_LP_MODE_NONE,
    };
    furi_check(LL_ADC_Init(FURI_HAL_ADC_INSTANCE, &init) == SUCCESS);

    LL_ADC_REG_InitTypeDef reg_init = {
        .TriggerSource = LL_ADC_REG_TRIG_SOFTWARE,
        .SequencerDiscont = LL_ADC_REG_SEQ_DISCONT_DISABLE,
        .ContinuousMode = LL_ADC_REG_CONV_CONTINUOUS,
        .DMATransfer = LL_ADC_REG_DMA_TRANSFER_NONE,
        .Overrun = LL_ADC_REG_OVR_DATA_OVERWRITTEN,
    };
    furi_check(LL_ADC_REG_Init(FURI_HAL_ADC_INSTANCE, &reg_init) == SUCCESS);
    LL_ADC_SetSamplingTimeCommonChannels(
        FURI_HAL_ADC_INSTANCE, LL_ADC_SAMPLINGTIME_COMMON_1, FURI_HAL_ADC_SAMPLING_TIME);

    uint32_t channels = 0;
    for(size_t i = 0; i < FURI_HAL_ADC_RANK_COUNT; i++) {
        channels |= furi_hal_adc_ranks[i].channel;
    }
    LL_ADC_REG_SetSequencerChannels(FURI_HAL_ADC_INSTANCE, channels);
    furi_check(
        furi_hal_adc_wait_flag(LL_ADC_IsActiveFlag_CCRDY, 1), "ADC channel configuration timeout");
    LL_ADC_ClearFlag_CCRDY(FURI_HAL_ADC_INSTANCE);

    LL_ADC_EnableInternalRegulator(FURI_HAL_ADC_INSTANCE);
    furi_hal_cortex_delay_us(LL_ADC_DELAY_INTERNAL_REGUL_STAB_US);

    const uint32_t calibration_factor = furi_hal_adc_calibrate();

    LL_ADC_REG_SetDMATransfer(FURI_HAL_ADC_INSTANCE, LL_ADC_REG_DMA_TRANSFER_UNLIMITED);

    furi_hal_cortex_delay_us(FURI_HAL_ADC_CYCLES_TO_US(LL_ADC_DELAY_CALIB_ENABLE_ADC_CYCLES));
    LL_ADC_Enable(FURI_HAL_ADC_INSTANCE);
    furi_check(furi_hal_adc_wait_flag(LL_ADC_IsActiveFlag_ADRDY, 1), "ADC enable timeout");
    LL_ADC_SetCalibrationFactor(FURI_HAL_ADC_INSTANCE, calibration_factor);

    LL_DMA_ConfigTransfer(
        FURI_HAL_ADC_DMA_INSTANCE,
        FURI_HAL_ADC_DMA_CHANNEL,
        LL_DMA_DIRECTION_PERIPH_TO_MEMORY | LL_DMA_MODE_CIRCULAR | LL_DMA_PERIPH_NOINCREMENT |
            LL_DMA_MEMORY_INCREMENT | LL_DMA_PDATAALIGN_HALFWORD | LL_DMA_MDATAALIGN_HALFWORD |
            LL_DMA_PRIORITY_LOW);
    LL_DMA_SetPeriphAddress(
        FURI_HAL_ADC_DMA_INSTANCE,
        FURI_HAL_ADC_DMA_CHANNEL,
        LL_ADC_DMA_GetRegAddr(FURI_HAL_ADC_INSTANCE, LL_ADC_DMA_REG_REGULAR_DATA));
    LL_DMA_SetMemoryAddress(
        FURI_HAL_ADC_DMA_INSTANCE, FURI_HAL_ADC_DMA_CHANNEL, (uint32_t)furi_hal_adc_samples);
    LL_DMA_SetDataLength(
        FURI_HAL_ADC_DMA_INSTANCE, FURI_HAL_ADC_DMA_CHANNEL, FURI_HAL_ADC_RANK_COUNT);
    LL_DMA_SetPeriphRequest(
        FURI_HAL_ADC_DMA_INSTANCE, FURI_HAL_ADC_DMA_CHANNEL, LL_DMAMUX_REQ_ADC1);
    LL_DMA_ClearFlag_GI1(FURI_HAL_ADC_DMA_INSTANCE);
    LL_DMA_EnableChannel(FURI_HAL_ADC_DMA_INSTANCE, FURI_HAL_ADC_DMA_CHANNEL);

    furi_hal_cortex_delay_us(LL_ADC_DELAY_TEMPSENSOR_STAB_US);

    LL_ADC_REG_StartConversion(FURI_HAL_ADC_INSTANCE);
    furi_check(furi_hal_adc_wait_first_scan(), "ADC first scan timeout");
}

static void furi_hal_adc_stop_scan(void) {
    LL_ADC_REG_StopConversion(FURI_HAL_ADC_INSTANCE);
    furi_check(furi_hal_adc_wait_flag(LL_ADC_REG_IsStopConversionOngoing, 0), "ADC stop timeout");
    LL_ADC_Disable(FURI_HAL_ADC_INSTANCE);
    furi_check(furi_hal_adc_wait_flag(LL_ADC_IsEnabled, 0), "ADC disable timeout");

    LL_DMA_DisableChannel(FURI_HAL_ADC_DMA_INSTANCE, FURI_HAL_ADC_DMA_CHANNEL);
    LL_DMA_ClearFlag_GI1(FURI_HAL_ADC_DMA_INSTANCE);

    furi_hal_bus_disable(FuriHalBusADC);

    furi_hal_power_insomnia_exit();
}

static uint16_t furi_hal_adc_get_sample(size_t rank) {
    furi_check(furi_hal_adc.user_count > 0);
    furi_check(!LL_ADC_IsActiveFlag_OVR(FURI_HAL_ADC_INSTANCE), "ADC overrun");

    return furi_hal_adc_samples[rank];
}

static uint32_t furi_hal_adc_get_vdda_mv(void) {
    const uint16_t vrefint = furi_hal_adc_get_sample(FURI_HAL_ADC_RANK_VREFINT);
    furi_check(vrefint > 0);

    return __LL_ADC_CALC_VREFANALOG_VOLTAGE(vrefint, LL_ADC_RESOLUTION_12B);
}

void furi_hal_adc_init(void) {
    furi_hal_adc.mutex = furi_mutex_alloc(FuriMutexTypeNormal);

    for(FuriHalAdcChannel channel = 0; channel < FuriHalAdcChannelMax; channel++) {
        furi_hal_gpio_init_simple(furi_hal_adc_ranks[channel].gpio, GpioModeAnalog);
    }

    FURI_LOG_I(TAG, "Init OK");
}

void furi_hal_adc_start(void) {
    furi_check(furi_mutex_acquire(furi_hal_adc.mutex, FuriWaitForever) == FuriStatusOk);

    if(furi_hal_adc.user_count == 0) {
        furi_hal_adc_start_scan();
    }
    furi_hal_adc.user_count++;

    furi_check(furi_mutex_release(furi_hal_adc.mutex) == FuriStatusOk);
}

void furi_hal_adc_stop(void) {
    furi_check(furi_mutex_acquire(furi_hal_adc.mutex, FuriWaitForever) == FuriStatusOk);

    furi_check(furi_hal_adc.user_count > 0);
    furi_hal_adc.user_count--;
    if(furi_hal_adc.user_count == 0) {
        furi_hal_adc_stop_scan();
    }

    furi_check(furi_mutex_release(furi_hal_adc.mutex) == FuriStatusOk);
}

uint32_t furi_hal_adc_read_mv(FuriHalAdcChannel channel) {
    furi_check(channel < FuriHalAdcChannelMax);

    const uint32_t vdda_mv = furi_hal_adc_get_vdda_mv();
    const uint16_t sample = furi_hal_adc_get_sample(channel);

    return __LL_ADC_CALC_DATA_TO_VOLTAGE(vdda_mv, sample, LL_ADC_RESOLUTION_12B);
}

int32_t furi_hal_adc_read_temperature(void) {
    const uint32_t vdda_mv = furi_hal_adc_get_vdda_mv();
    const uint16_t sample = furi_hal_adc_get_sample(FURI_HAL_ADC_RANK_TEMPERATURE);

    return __LL_ADC_CALC_TEMPERATURE(vdda_mv, sample, LL_ADC_RESOLUTION_12B);
}
