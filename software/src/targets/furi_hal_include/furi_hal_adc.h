#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    FuriHalAdcChannelPhoneCurrentSense,
    FuriHalAdcChannelPhoneVbusSense,
    FuriHalAdcChannelChargerCurrentSense,
    FuriHalAdcChannelChargerVbusSense,
    FuriHalAdcChannelHeadsetButtons,
    FuriHalAdcChannelMax,
} FuriHalAdcChannel;

void furi_hal_adc_init(void);

void furi_hal_adc_start(void);

void furi_hal_adc_stop(void);

uint32_t furi_hal_adc_read_mv(FuriHalAdcChannel channel);

uint32_t furi_hal_adc_read_vdda_mv(void);

int32_t furi_hal_adc_read_temperature(void);

#ifdef __cplusplus
}
#endif
