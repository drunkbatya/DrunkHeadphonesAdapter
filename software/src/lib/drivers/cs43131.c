#include <drivers/cs43131.h>
#include <drivers/cs43131_reg.h>

#include <furi.h>

#define CS43131_MCLK_SWITCH_DELAY_US 200

typedef struct {
    uint8_t sample_rate_code;
    uint8_t asp_denominator;
    Cs43131MclkSource mclk_source;
} Cs43131SampleRateConfig;

typedef struct {
    uint8_t sp_bitsize;
    uint8_t channel_resolution;
} Cs43131BitDepthConfig;

static const Cs43131SampleRateConfig cs43131_sample_rate_config[] = {
    [Cs43131SampleRate32000] = {0x00, 12, Cs43131MclkSourceExternal24576},
    [Cs43131SampleRate44100] = {0x01, 8, Cs43131MclkSourceExternal22579},
    [Cs43131SampleRate48000] = {0x02, 8, Cs43131MclkSourceExternal24576},
    [Cs43131SampleRate88200] = {0x03, 4, Cs43131MclkSourceExternal22579},
    [Cs43131SampleRate96000] = {0x04, 4, Cs43131MclkSourceExternal24576},
    [Cs43131SampleRate176400] = {0x05, 2, Cs43131MclkSourceExternal22579},
    [Cs43131SampleRate192000] = {0x06, 2, Cs43131MclkSourceExternal24576},
};

static const Cs43131BitDepthConfig cs43131_bit_depth_config[] = {
    [Cs43131BitDepth16] = {CS43131_SP_BITSIZE_ASP_16, CS43131_ASP_CH_SZ_EN_RES_16},
    [Cs43131BitDepth24] = {CS43131_SP_BITSIZE_ASP_24, CS43131_ASP_CH_SZ_EN_RES_24},
    [Cs43131BitDepth32] = {CS43131_SP_BITSIZE_ASP_32, CS43131_ASP_CH_SZ_EN_RES_32},
};

static bool cs43131_write(const FuriHalI2cBusHandle* handle, uint32_t reg, uint8_t value) {
    const uint8_t tx_data[] = {
        (reg >> 16) & 0xFF,
        (reg >> 8) & 0xFF,
        reg & 0xFF,
        CS43131_CONTROL_BYTE_SINGLE,
        value,
    };
    return furi_hal_i2c_tx(handle, CS43131_ADDRESS, tx_data, sizeof(tx_data), CS43131_I2C_TIMEOUT);
}

static bool cs43131_read(const FuriHalI2cBusHandle* handle, uint32_t reg, uint8_t* value) {
    const uint8_t tx_data[] = {
        (reg >> 16) & 0xFF,
        (reg >> 8) & 0xFF,
        reg & 0xFF,
        CS43131_CONTROL_BYTE_SINGLE,
    };
    return furi_hal_i2c_trx(
        handle, CS43131_ADDRESS, tx_data, sizeof(tx_data), value, 1, CS43131_I2C_TIMEOUT);
}

static bool cs43131_update_bits(
    const FuriHalI2cBusHandle* handle,
    uint32_t reg,
    uint8_t mask,
    uint8_t value) {
    uint8_t current;
    if(!cs43131_read(handle, reg, &current)) {
        return false;
    }
    return cs43131_write(handle, reg, (current & ~mask) | (value & mask));
}

bool cs43131_init(const FuriHalI2cBusHandle* handle) {
    furi_check(handle);

    bool success = false;
    do {
        uint8_t devid_ab;
        uint8_t devid_cd;
        uint8_t devid_e;
        if(!cs43131_read(handle, CS43131_REG_DEVID_AB, &devid_ab)) break;
        if(!cs43131_read(handle, CS43131_REG_DEVID_CD, &devid_cd)) break;
        if(!cs43131_read(handle, CS43131_REG_DEVID_E, &devid_e)) break;
        if(devid_ab != CS43131_DEVID_AB) break;
        if(devid_cd != CS43131_DEVID_CD) break;
        if((devid_e & CS43131_DEVID_E_MASK) != CS43131_DEVID_E) break;

        uint8_t status;
        if(!cs43131_write(handle, CS43131_REG_CLASS_H_CTL, CS43131_CLASS_H_CTL_ADAPT_LOW_VOLTAGE))
            break;
        if(!cs43131_write(handle, CS43131_REG_HP_OUT_CTL_1, CS43131_HP_OUT_CTL_1_OUT_FS_1P41V))
            break;
        if(!cs43131_write(handle, CS43131_REG_PCM_PATH_CTL_1, CS43131_PCM_PATH_CTL_1_DEFAULT))
            break;
        if(!cs43131_write(handle, CS43131_REG_CLKOUT_CTL, CS43131_CLKOUT_CTL_DIV_2_MCLK_PATH))
            break;
        if(!cs43131_write(handle, CS43131_REG_PAD_INT_CFG, CS43131_PAD_INT_CFG_ASP_ACTIVE_XSP_3ST))
            break;
        if(!cs43131_read(handle, CS43131_REG_INT_STATUS_1, &status)) break;
        if(!cs43131_read(handle, CS43131_REG_HP_STATUS, &status)) break;
        if(!cs43131_write(
               handle,
               CS43131_REG_INT_MASK_1,
               (uint8_t)~(CS43131_INTERRUPT_HP_PLUG | CS43131_INTERRUPT_HP_UNPLUG)))
            break;
        if(!cs43131_write(
               handle, CS43131_REG_HP_DETECT, CS43131_HP_DETECT_ENABLED_UNPLUG_0MS_PLUG_500MS))
            break;
        success = true;
    } while(0);
    return success;
}

Cs43131MclkSource cs43131_get_mclk_source(Cs43131SampleRate sample_rate) {
    furi_check(sample_rate < COUNT_OF(cs43131_sample_rate_config));
    return cs43131_sample_rate_config[sample_rate].mclk_source;
}

bool cs43131_set_mclk_source(const FuriHalI2cBusHandle* handle, Cs43131MclkSource source) {
    furi_check(handle);

    const uint8_t mclk_int = (source == Cs43131MclkSourceExternal24576) ?
                                 CS43131_SYS_CLK_CTL_MCLK_INT_24P576 :
                                 CS43131_SYS_CLK_CTL_MCLK_INT_22P5792;

    if(!cs43131_write(handle, CS43131_REG_SYS_CLK_CTL, mclk_int | CS43131_SYS_CLK_CTL_SRC_RCO)) {
        return false;
    }
    furi_delay_us(CS43131_MCLK_SWITCH_DELAY_US);

    if(source == Cs43131MclkSourceRco) {
        return true;
    }

    if(!cs43131_write(handle, CS43131_REG_SYS_CLK_CTL, mclk_int | CS43131_SYS_CLK_CTL_SRC_DIRECT)) {
        return false;
    }
    furi_delay_us(CS43131_MCLK_SWITCH_DELAY_US);

    return true;
}

bool cs43131_set_clkout_enabled(const FuriHalI2cBusHandle* handle, bool enabled) {
    furi_check(handle);

    return cs43131_update_bits(
        handle,
        CS43131_REG_PWDN_CTL,
        CS43131_PWDN_CTL_PDN_CLKOUT | CS43131_PWDN_CTL_RESERVED,
        enabled ? 0 : CS43131_PWDN_CTL_PDN_CLKOUT);
}

bool cs43131_configure_asp(
    const FuriHalI2cBusHandle* handle,
    Cs43131SampleRate sample_rate,
    Cs43131BitDepth bit_depth) {
    furi_check(handle);
    furi_check(sample_rate < COUNT_OF(cs43131_sample_rate_config));
    furi_check(bit_depth < COUNT_OF(cs43131_bit_depth_config));

    const Cs43131SampleRateConfig* rate = &cs43131_sample_rate_config[sample_rate];
    const Cs43131BitDepthConfig* depth = &cs43131_bit_depth_config[bit_depth];

    bool success = false;
    do {
        if(!cs43131_write(handle, CS43131_REG_SP_SRATE, rate->sample_rate_code)) break;
        if(!cs43131_write(
               handle, CS43131_REG_SP_BITSIZE, CS43131_SP_BITSIZE_XSP_DEFAULT | depth->sp_bitsize))
            break;
        if(!cs43131_write(handle, CS43131_REG_ASP_NUM_1, 1)) break;
        if(!cs43131_write(handle, CS43131_REG_ASP_NUM_2, 0)) break;
        if(!cs43131_write(handle, CS43131_REG_ASP_DEN_1, rate->asp_denominator)) break;
        if(!cs43131_write(handle, CS43131_REG_ASP_DEN_2, 0)) break;
        if(!cs43131_write(
               handle, CS43131_REG_ASP_LRCK_HI_1, CS43131_ASP_FRAME_SCLK_PERIODS / 2 - 1))
            break;
        if(!cs43131_write(handle, CS43131_REG_ASP_LRCK_HI_2, 0)) break;
        if(!cs43131_write(
               handle, CS43131_REG_ASP_LRCK_PERIOD_1, CS43131_ASP_FRAME_SCLK_PERIODS - 1))
            break;
        if(!cs43131_write(handle, CS43131_REG_ASP_LRCK_PERIOD_2, 0)) break;
        if(!cs43131_write(handle, CS43131_REG_ASP_CLOCK_CONF, CS43131_ASP_CLOCK_CONF_MASTER_I2S))
            break;
        if(!cs43131_write(handle, CS43131_REG_ASP_FRAME_CONF, CS43131_ASP_FRAME_CONF_I2S)) break;
        if(!cs43131_write(handle, CS43131_REG_ASP_CH_1_LOC, 0)) break;
        if(!cs43131_write(handle, CS43131_REG_ASP_CH_2_LOC, 0)) break;
        if(!cs43131_write(
               handle,
               CS43131_REG_ASP_CH_1_SZ_EN,
               CS43131_ASP_CH_SZ_EN_ENABLE | depth->channel_resolution))
            break;
        if(!cs43131_write(
               handle,
               CS43131_REG_ASP_CH_2_SZ_EN,
               CS43131_ASP_CH_SZ_EN_ACTIVE_HIGH | CS43131_ASP_CH_SZ_EN_ENABLE |
                   depth->channel_resolution))
            break;
        success = true;
    } while(0);
    return success;
}

bool cs43131_power_up_begin(const FuriHalI2cBusHandle* handle) {
    furi_check(handle);

    bool success = false;
    do {
        if(!cs43131_write(handle, CS43131_REG_DXD1, CS43131_DXD1_UNLOCK)) break;
        if(!cs43131_write(handle, CS43131_REG_DXD13, CS43131_DXD13_POP_FREE)) break;
        if(!cs43131_update_bits(
               handle,
               CS43131_REG_PWDN_CTL,
               CS43131_PWDN_CTL_PDN_ASP | CS43131_PWDN_CTL_RESERVED,
               0))
            break;
        if(!cs43131_update_bits(
               handle,
               CS43131_REG_PWDN_CTL,
               CS43131_PWDN_CTL_PDN_HP | CS43131_PWDN_CTL_RESERVED,
               0))
            break;
        success = true;
    } while(0);
    return success;
}

bool cs43131_power_up_end(const FuriHalI2cBusHandle* handle) {
    furi_check(handle);

    if(!cs43131_write(handle, CS43131_REG_DXD13, CS43131_DXD13_DEFAULT)) {
        return false;
    }
    return cs43131_write(handle, CS43131_REG_DXD1, CS43131_DXD1_LOCK);
}

bool cs43131_power_down_begin(const FuriHalI2cBusHandle* handle) {
    furi_check(handle);

    if(!cs43131_update_bits(handle, CS43131_REG_INT_MASK_1, CS43131_INTERRUPT_PDN_DONE, 0)) {
        return false;
    }
    return cs43131_update_bits(
        handle,
        CS43131_REG_PWDN_CTL,
        CS43131_PWDN_CTL_PDN_HP | CS43131_PWDN_CTL_RESERVED,
        CS43131_PWDN_CTL_PDN_HP);
}

bool cs43131_power_down_end(const FuriHalI2cBusHandle* handle) {
    furi_check(handle);

    if(!cs43131_update_bits(
           handle,
           CS43131_REG_PWDN_CTL,
           CS43131_PWDN_CTL_PDN_ASP | CS43131_PWDN_CTL_RESERVED,
           CS43131_PWDN_CTL_PDN_ASP)) {
        return false;
    }
    return cs43131_update_bits(
        handle, CS43131_REG_INT_MASK_1, CS43131_INTERRUPT_PDN_DONE, CS43131_INTERRUPT_PDN_DONE);
}

bool cs43131_set_volume_attenuation(const FuriHalI2cBusHandle* handle, uint8_t half_db_steps) {
    furi_check(handle);

    return cs43131_write(handle, CS43131_REG_PCM_VOL_A, half_db_steps);
}

bool cs43131_set_muted(const FuriHalI2cBusHandle* handle, bool muted) {
    furi_check(handle);

    return cs43131_update_bits(
        handle,
        CS43131_REG_PCM_PATH_CTL_1,
        CS43131_PCM_PATH_CTL_1_MUTE_AB,
        muted ? CS43131_PCM_PATH_CTL_1_MUTE_AB : 0);
}

bool cs43131_read_interrupt_status(const FuriHalI2cBusHandle* handle, uint8_t* interrupt_status) {
    furi_check(handle);
    furi_check(interrupt_status);

    return cs43131_read(handle, CS43131_REG_INT_STATUS_1, interrupt_status);
}

bool cs43131_is_headphone_plugged(const FuriHalI2cBusHandle* handle, bool* plugged) {
    furi_check(handle);
    furi_check(plugged);

    uint8_t status;
    if(!cs43131_read(handle, CS43131_REG_HP_STATUS, &status)) {
        return false;
    }
    *plugged = status & CS43131_HP_STATUS_PLUG_DBC;
    return true;
}
