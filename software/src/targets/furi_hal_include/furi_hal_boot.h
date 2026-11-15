#pragma once

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    FuriHalBootModeNormal,
    FuriHalBootModeDfu,
} FuriHalBootMode;

void furi_hal_boot_set_mode(FuriHalBootMode mode);

FuriHalBootMode furi_hal_boot_get_mode(void);

#ifdef __cplusplus
}
#endif
