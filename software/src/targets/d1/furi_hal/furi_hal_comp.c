#include <furi_hal_comp.h>

#include <furi.h>

void furi_hal_comp_init(void) {
}

void furi_hal_comp_start(FuriHalCompId id, FuriHalCompCallback callback, void* context) {
    UNUSED(id);
    UNUSED(callback);
    UNUSED(context);
}

void furi_hal_comp_stop(FuriHalCompId id) {
    UNUSED(id);
}

void furi_hal_comp_set_threshold_quarter_vrefint(FuriHalCompId id) {
    UNUSED(id);
}

void furi_hal_comp_set_threshold_mv(FuriHalCompId id, uint32_t threshold_mv) {
    UNUSED(id);
    UNUSED(threshold_mv);
}

bool furi_hal_comp_is_above(FuriHalCompId id) {
    UNUSED(id);
    return false;
}
