#pragma once

#include <stdbool.h>
#include <stdint.h>

#include <furi_hal_i2c.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ES8327_ADC_VOLUME_MIN          0x00
#define ES8327_ADC_VOLUME_0DB          0xBF
#define ES8327_ADC_VOLUME_MAX          0xFF
#define ES8327_ADC_VOLUME_STEPS_PER_DB 2

typedef enum {
    Es8327SampleRate44100,
    Es8327SampleRate48000,
} Es8327SampleRate;

typedef enum {
    Es8327WordLength16,
    Es8327WordLength24,
    Es8327WordLength32,
} Es8327WordLength;

bool es8327_init(const FuriHalI2cBusHandle* handle);

bool es8327_configure(
    const FuriHalI2cBusHandle* handle,
    Es8327SampleRate sample_rate,
    Es8327WordLength word_length);

bool es8327_power_up(const FuriHalI2cBusHandle* handle);

bool es8327_power_down(const FuriHalI2cBusHandle* handle);

bool es8327_set_mic_bias_enabled(const FuriHalI2cBusHandle* handle, bool enabled);

bool es8327_set_mic_gain(const FuriHalI2cBusHandle* handle, uint8_t gain_3db_steps);

bool es8327_set_adc_volume(const FuriHalI2cBusHandle* handle, uint8_t volume);

bool es8327_set_adc_muted(const FuriHalI2cBusHandle* handle, bool muted);

#ifdef __cplusplus
}
#endif
