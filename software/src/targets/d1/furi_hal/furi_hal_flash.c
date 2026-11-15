#include <furi_hal_flash.h>

#include <furi.h>

#include <stm32g0xx.h>

#include <string.h>

#define TAG "FuriHalFlash"

#define FURI_HAL_FLASH_KEY1             0x45670123U
#define FURI_HAL_FLASH_KEY2             0xCDEF89ABU
#define FURI_HAL_FLASH_OPTKEY1          0x08192A3BU
#define FURI_HAL_FLASH_OPTKEY2          0x4C5D6E7FU
#define FURI_HAL_FLASH_ERASE_TIMEOUT_MS 100U

#define FURI_HAL_FLASH_SR_ERRORS                                                               \
    (FLASH_SR_OPERR | FLASH_SR_PROGERR | FLASH_SR_WRPERR | FLASH_SR_PGAERR | FLASH_SR_SIZERR | \
     FLASH_SR_PGSERR | FLASH_SR_MISERR | FLASH_SR_FASTERR | FLASH_SR_RDERR | FLASH_SR_OPTVERR)

static void furi_hal_flash_unlock(void) {
    if(READ_BIT(FLASH->CR, FLASH_CR_LOCK)) {
        WRITE_REG(FLASH->KEYR, FURI_HAL_FLASH_KEY1);
        WRITE_REG(FLASH->KEYR, FURI_HAL_FLASH_KEY2);
    }
    furi_check(!READ_BIT(FLASH->CR, FLASH_CR_LOCK));
}

static void furi_hal_flash_lock(void) {
    SET_BIT(FLASH->CR, FLASH_CR_LOCK);
}

static bool furi_hal_flash_is_inactive_bank_physical_bank2(void) {
    return READ_BIT(FLASH->OPTR, FLASH_OPTR_nSWAP_BANK) != 0;
}

static bool furi_hal_flash_check_errors(const char* operation, size_t offset) {
    const uint32_t errors = READ_REG(FLASH->SR) & FURI_HAL_FLASH_SR_ERRORS;
    if(errors == 0) {
        return true;
    }
    WRITE_REG(FLASH->SR, errors);
    FURI_LOG_E(TAG, "%s at 0x%05X failed, SR 0x%08lX", operation, offset, errors);
    return false;
}

static bool furi_hal_flash_wait_erase_done(void) {
    for(uint32_t elapsed_ms = 0; elapsed_ms < FURI_HAL_FLASH_ERASE_TIMEOUT_MS; elapsed_ms++) {
        if(!READ_BIT(FLASH->SR, FLASH_SR_CFGBSY)) {
            return true;
        }
        furi_delay_ms(1);
    }
    return false;
}

bool furi_hal_flash_is_dual_bank(void) {
    return READ_BIT(FLASH->OPTR, FLASH_OPTR_DUAL_BANK) != 0;
}

size_t furi_hal_flash_get_bank_size(void) {
    return FLASH_SIZE / 2U;
}

const uint8_t* furi_hal_flash_get_inactive_bank(void) {
    return (const uint8_t*)(FLASH_BASE + furi_hal_flash_get_bank_size());
}

bool furi_hal_flash_erase_inactive_page(size_t page) {
    furi_check(page < furi_hal_flash_get_bank_size() / FURI_HAL_FLASH_PAGE_SIZE);

    furi_hal_flash_unlock();
    WRITE_REG(FLASH->SR, FURI_HAL_FLASH_SR_ERRORS);

    uint32_t cr = READ_REG(FLASH->CR) & ~(FLASH_CR_PNB | FLASH_CR_BKER);
    if(furi_hal_flash_is_inactive_bank_physical_bank2()) {
        cr |= FLASH_CR_BKER;
    }
    WRITE_REG(FLASH->CR, cr | (page << FLASH_CR_PNB_Pos) | FLASH_CR_PER);
    SET_BIT(FLASH->CR, FLASH_CR_STRT);

    const bool done = furi_hal_flash_wait_erase_done();
    CLEAR_BIT(FLASH->CR, FLASH_CR_PER | FLASH_CR_PNB | FLASH_CR_BKER);
    const size_t offset = page * FURI_HAL_FLASH_PAGE_SIZE;
    const bool success = done && furi_hal_flash_check_errors("Erase", offset);
    furi_hal_flash_lock();

    if(!done) {
        FURI_LOG_E(TAG, "Erase at 0x%05X timed out", offset);
    }
    return success;
}

bool furi_hal_flash_program_inactive(size_t offset, const uint8_t* data, size_t size) {
    furi_check(offset % FURI_HAL_FLASH_PROGRAM_UNIT == 0);
    furi_check(size % FURI_HAL_FLASH_PROGRAM_UNIT == 0);
    furi_check(offset + size <= furi_hal_flash_get_bank_size());

    furi_hal_flash_unlock();
    WRITE_REG(FLASH->SR, FURI_HAL_FLASH_SR_ERRORS);
    SET_BIT(FLASH->CR, FLASH_CR_PG);

    volatile uint32_t* destination =
        (volatile uint32_t*)(furi_hal_flash_get_inactive_bank() + offset);
    bool success = true;
    for(size_t i = 0; i < size; i += FURI_HAL_FLASH_PROGRAM_UNIT) {
        uint32_t words[2];
        memcpy(words, &data[i], sizeof(words));
        destination[0] = words[0];
        __ISB();
        destination[1] = words[1];
        destination += 2;
        while(READ_BIT(FLASH->SR, FLASH_SR_CFGBSY)) {
        }
        if(!furi_hal_flash_check_errors("Program", offset + i)) {
            success = false;
            break;
        }
    }

    CLEAR_BIT(FLASH->CR, FLASH_CR_PG);
    furi_hal_flash_lock();
    return success;
}

FURI_NORETURN void furi_hal_flash_swap_banks(void) {
    __disable_irq();

    furi_hal_flash_unlock();
    WRITE_REG(FLASH->OPTKEYR, FURI_HAL_FLASH_OPTKEY1);
    WRITE_REG(FLASH->OPTKEYR, FURI_HAL_FLASH_OPTKEY2);
    furi_check(!READ_BIT(FLASH->CR, FLASH_CR_OPTLOCK));

    while(READ_BIT(FLASH->SR, FLASH_SR_BSY1)) {
    }
    const uint32_t optr = READ_REG(FLASH->OPTR) ^ FLASH_OPTR_nSWAP_BANK;
    WRITE_REG(FLASH->OPTR, optr);
    SET_BIT(FLASH->CR, FLASH_CR_OPTSTRT);
    while(READ_BIT(FLASH->SR, FLASH_SR_BSY1)) {
    }

    SET_BIT(FLASH->CR, FLASH_CR_OBL_LAUNCH);
    for(;;) {
    }
}
