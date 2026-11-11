#include <drivers/es8327.h>
#include <drivers/es8327_reg.h>

#include <furi.h>

#define ES8327_RESET_DELAY_MS 5

#define ES8327_CLOCK_REG_COUNT 8

typedef struct {
    uint8_t clock_regs[ES8327_CLOCK_REG_COUNT];
} Es8327ClockConfig;

// TODO: values for MCLK = 256 Fs from the Everest ES8326 driver (Linux v6.1 coeff_div), datasheet does not describe clk2..clk10
static const Es8327ClockConfig es8327_clock_config[] = {
    [Es8327SampleRate44100] = {{0x00, 0x00, 0x10, 0x2D, 0x0A, 0x0A, 0x1F, 0x1F}},
    [Es8327SampleRate48000] = {{0x00, 0x00, 0x30, 0x2D, 0x0A, 0x0A, 0x1F, 0x1F}},
};

static const uint8_t es8327_word_length[] = {
    [Es8327WordLength16] = ES8327_SDP_FMT_WL_16,
    [Es8327WordLength24] = ES8327_SDP_FMT_WL_24,
    [Es8327WordLength32] = ES8327_SDP_FMT_WL_32,
};

static bool es8327_write(const FuriHalI2cBusHandle* handle, uint8_t reg, uint8_t value) {
    return furi_hal_i2c_write_reg_8(handle, ES8327_ADDRESS, reg, value, ES8327_I2C_TIMEOUT);
}

static bool es8327_read(const FuriHalI2cBusHandle* handle, uint8_t reg, uint8_t* value) {
    return furi_hal_i2c_read_reg_8(handle, ES8327_ADDRESS, reg, value, ES8327_I2C_TIMEOUT);
}

static bool es8327_update_bits(
    const FuriHalI2cBusHandle* handle,
    uint8_t reg,
    uint8_t mask,
    uint8_t value) {
    uint8_t current;
    if(!es8327_read(handle, reg, &current)) {
        return false;
    }
    return es8327_write(handle, reg, (current & ~mask) | (value & mask));
}

bool es8327_init(const FuriHalI2cBusHandle* handle) {
    furi_check(handle);

    bool success = false;
    do {
        uint8_t chip_id1;
        uint8_t chip_id2;
        if(!es8327_read(handle, ES8327_REG_CHIP_ID1, &chip_id1)) break;
        if(!es8327_read(handle, ES8327_REG_CHIP_ID2, &chip_id2)) break;
        if(chip_id1 != ES8327_CHIP_ID1) break;
        if(chip_id2 != ES8327_CHIP_ID2) break;

        if(!es8327_write(handle, ES8327_REG_RESET, ES8327_RESET_ALL)) break;
        furi_delay_ms(ES8327_RESET_DELAY_MS);

        if(!es8327_write(handle, ES8327_REG_TEST_MODE, ES8327_TEST_MODE_ADC34_OFF_NO_PULLUP_EDGE))
            break;
        if(!es8327_write(handle, ES8327_REG_CLK_INV, ES8327_CLK_INV_MCLK_PAD)) break;
        if(!es8327_write(handle, ES8327_REG_CLK_RESAMPLE, ES8327_CLK_RESAMPLE_INTCLK_OSC_AUTO))
            break;
        if(!es8327_write(handle, ES8327_REG_CLK_DIV_CPC, ES8327_CLK_DIV_CPC_DEFAULT)) break;
        if(!es8327_write(handle, ES8327_REG_CLK_VMIDS1, ES8327_CLK_VMIDS1_VALUE)) break;
        if(!es8327_write(handle, ES8327_REG_CLK_VMIDS2, ES8327_CLK_VMIDS2_VALUE)) break;
        if(!es8327_write(handle, ES8327_REG_CLK_CAL_TIME, ES8327_CLK_CAL_TIME_VALUE)) break;
        if(!es8327_write(handle, ES8327_REG_ANA_LP, ES8327_ANA_LP_VALUE)) break;
        if(!es8327_write(handle, ES8327_REG_SYS_BIAS, ES8327_SYS_BIAS_VALUE)) break;
        if(!es8327_write(
               handle,
               ES8327_REG_ANA_MICBIAS,
               ES8327_ANA_MICBIAS_RES_1K | ES8327_ANA_MICBIAS_FORCE_ON |
                   ES8327_ANA_MICBIAS_HPSCALE_1000))
            break;
        if(!es8327_write(handle, ES8327_REG_PGAGAIN, ES8327_PGAGAIN_MIC1_SEL)) break;
        if(!es8327_write(handle, ES8327_REG_HPJACK_TIMER, ES8327_HPJACK_TIMER_VALUE)) break;
        if(!es8327_write(
               handle,
               ES8327_REG_HPDET_TYPE,
               ES8327_HPDET_TYPE_KEEP_CSM | ES8327_HPDET_TYPE_INSERT_PIN9 |
                   ES8327_HPDET_TYPE_BUTTON_LOW | ES8327_HPDET_TYPE_CTIA))
            break;
        if(!es8327_write(handle, ES8327_REG_INTOUT_IO, ES8327_INTOUT_IO_DEFAULT)) break;
        if(!es8327_write(handle, ES8327_REG_SDINOUT1_IO, ES8327_SDINOUT1_IO_LOW_MCLK_INPUT)) break;
        if(!es8327_write(handle, ES8327_REG_INT_SOURCE, ES8327_INT_SOURCE_BUTTON)) break;
        if(!es8327_power_down(handle)) break;
        success = true;
    } while(0);
    return success;
}

bool es8327_configure(
    const FuriHalI2cBusHandle* handle,
    Es8327SampleRate sample_rate,
    Es8327WordLength word_length) {
    furi_check(handle);
    furi_check(sample_rate < COUNT_OF(es8327_clock_config));
    furi_check(word_length < COUNT_OF(es8327_word_length));

    const Es8327ClockConfig* clock_config = &es8327_clock_config[sample_rate];
    for(uint8_t i = 0; i < ES8327_CLOCK_REG_COUNT; i++) {
        if(!es8327_write(handle, ES8327_REG_CLK_DIV1 + i, clock_config->clock_regs[i])) {
            return false;
        }
    }

    return es8327_write(
        handle, ES8327_REG_SDP_FMT, ES8327_SDP_FMT_I2S | es8327_word_length[word_length]);
}

bool es8327_power_up(const FuriHalI2cBusHandle* handle) {
    furi_check(handle);

    bool success = false;
    do {
        if(!es8327_write(handle, ES8327_REG_CLK_CTL, ES8327_CLK_CTL_ALL_EXCEPT_CPCLK)) break;
        if(!es8327_write(handle, ES8327_REG_RESET, ES8327_RESET_MASTER_MODE | ES8327_RESET_SEQ_DIS))
            break;
        if(!es8327_write(handle, ES8327_REG_VMIDSEL, ES8327_VMIDSEL_NORMAL)) break;
        if(!es8327_write(handle, ES8327_REG_ANA_PDN, ES8327_ANA_PDN_ADC_ON)) break;
        if(!es8327_write(handle, ES8327_REG_PGA_PDN, ES8327_PGA_PDN_ON)) break;
        if(!es8327_write(handle, ES8327_REG_SDP_ADC, ES8327_SDP_ADC_2CH_UNMUTED)) break;
        if(!es8327_write(handle, ES8327_REG_RESET, ES8327_RESET_CSM_ON | ES8327_RESET_MASTER_MODE))
            break;
        success = true;
    } while(0);
    return success;
}

bool es8327_power_down(const FuriHalI2cBusHandle* handle) {
    furi_check(handle);

    bool success = false;
    do {
        if(!es8327_write(handle, ES8327_REG_SDP_ADC, ES8327_SDP_ADC_TDM_MUTED)) break;
        if(!es8327_write(handle, ES8327_REG_RESET, ES8327_RESET_MASTER_MODE)) break;
        if(!es8327_write(handle, ES8327_REG_PGA_PDN, ES8327_PGA_PDN_OFF)) break;
        if(!es8327_write(handle, ES8327_REG_ANA_PDN, ES8327_ANA_PDN_OFF)) break;
        if(!es8327_write(handle, ES8327_REG_VMIDSEL, ES8327_VMIDSEL_OFF)) break;
        if(!es8327_write(
               handle,
               ES8327_REG_RESET,
               ES8327_RESET_MASTER_MODE | ES8327_RESET_SEQ_DIS | ES8327_RESET_BLOCKS))
            break;
        if(!es8327_write(handle, ES8327_REG_CLK_CTL, ES8327_CLK_CTL_OFF)) break;
        success = true;
    } while(0);
    return success;
}

bool es8327_set_mic_bias_enabled(const FuriHalI2cBusHandle* handle, bool enabled) {
    furi_check(handle);

    return es8327_update_bits(
        handle,
        ES8327_REG_ANA_MICBIAS,
        ES8327_ANA_MICBIAS_FORCE_ON,
        enabled ? ES8327_ANA_MICBIAS_FORCE_ON : ES8327_ANA_MICBIAS_FORCE_OFF);
}

bool es8327_set_mic_gain(const FuriHalI2cBusHandle* handle, uint8_t gain_3db_steps) {
    furi_check(handle);
    furi_check(gain_3db_steps <= ES8327_PGAGAIN_GAIN_MAX);

    return es8327_update_bits(
        handle, ES8327_REG_PGAGAIN, ES8327_PGAGAIN_GAIN_MASK, gain_3db_steps);
}

bool es8327_set_adc_volume(const FuriHalI2cBusHandle* handle, uint8_t volume) {
    furi_check(handle);

    return es8327_write(handle, ES8327_REG_ADC_VOLUME1, volume);
}

bool es8327_set_adc_muted(const FuriHalI2cBusHandle* handle, bool muted) {
    furi_check(handle);

    return es8327_write(
        handle, ES8327_REG_SDP_ADC, muted ? ES8327_SDP_ADC_2CH_MUTED : ES8327_SDP_ADC_2CH_UNMUTED);
}
