#pragma once

#include <stdbool.h>
#include <stdint.h>

#include <furi_hal_i2c.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CS43131_INTERRUPT_HP_PLUG   (1 << 6)
#define CS43131_INTERRUPT_HP_UNPLUG (1 << 5)
#define CS43131_INTERRUPT_PDN_DONE  (1 << 0)

#define CS43131_VOLUME_ATTENUATION_MUTE 0xFF

typedef enum {
    Cs43131MclkSourceRco,
    Cs43131MclkSourceExternal24576,
    Cs43131MclkSourceExternal22579,
} Cs43131MclkSource;

typedef enum {
    Cs43131SampleRate32000,
    Cs43131SampleRate44100,
    Cs43131SampleRate48000,
    Cs43131SampleRate88200,
    Cs43131SampleRate96000,
    Cs43131SampleRate176400,
    Cs43131SampleRate192000,
} Cs43131SampleRate;

typedef enum {
    Cs43131BitDepth16,
    Cs43131BitDepth24,
    Cs43131BitDepth32,
} Cs43131BitDepth;

bool cs43131_init(const FuriHalI2cBusHandle* handle);

Cs43131MclkSource cs43131_get_mclk_source(Cs43131SampleRate sample_rate);

bool cs43131_set_mclk_source(const FuriHalI2cBusHandle* handle, Cs43131MclkSource source);

bool cs43131_set_clkout_enabled(const FuriHalI2cBusHandle* handle, bool enabled);

bool cs43131_configure_asp(
    const FuriHalI2cBusHandle* handle,
    Cs43131SampleRate sample_rate,
    Cs43131BitDepth bit_depth);

bool cs43131_power_up(const FuriHalI2cBusHandle* handle);

bool cs43131_power_down(const FuriHalI2cBusHandle* handle, uint8_t* interrupt_status);

bool cs43131_set_volume_attenuation(const FuriHalI2cBusHandle* handle, uint8_t half_db_steps);

bool cs43131_set_muted(const FuriHalI2cBusHandle* handle, bool muted);

bool cs43131_read_interrupt_status(const FuriHalI2cBusHandle* handle, uint8_t* interrupt_status);

bool cs43131_is_headphone_plugged(const FuriHalI2cBusHandle* handle, bool* plugged);

#ifdef __cplusplus
}
#endif
