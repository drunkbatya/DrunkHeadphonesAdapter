#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    FuriHalCompIdPhoneVbus,
    FuriHalCompIdChargerVbus,

    FuriHalCompIdMax,
} FuriHalCompId;

typedef void (*FuriHalCompCallback)(bool above, void* context);

void furi_hal_comp_init(void);

void furi_hal_comp_start(FuriHalCompId id, FuriHalCompCallback callback, void* context);

void furi_hal_comp_stop(FuriHalCompId id);

void furi_hal_comp_set_threshold_quarter_vrefint(FuriHalCompId id);

void furi_hal_comp_set_threshold_mv(FuriHalCompId id, uint32_t threshold_mv);

bool furi_hal_comp_is_above(FuriHalCompId id);

#ifdef __cplusplus
}
#endif
