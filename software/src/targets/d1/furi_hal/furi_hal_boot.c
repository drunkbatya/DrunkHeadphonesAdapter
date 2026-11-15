#include <furi_hal_boot.h>

#include <stm32g0xx.h>

void furi_hal_boot_set_mode(FuriHalBootMode mode) {
    if(mode == FuriHalBootModeDfu) {
        SET_BIT(FLASH->ACR, FLASH_ACR_PROGEMPTY);
    } else {
        CLEAR_BIT(FLASH->ACR, FLASH_ACR_PROGEMPTY);
    }
}

FuriHalBootMode furi_hal_boot_get_mode(void) {
    return READ_BIT(FLASH->ACR, FLASH_ACR_PROGEMPTY) ? FuriHalBootModeDfu : FuriHalBootModeNormal;
}
