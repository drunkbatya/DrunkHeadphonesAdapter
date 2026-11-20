#include <furi_hal_flash.h>
#include <furi_hal_cortex.h>

#include <furi.h>

#include <stm32g0xx.h>
#include <stm32g0b1_linker.h>

#include <string.h>

#define TAG "FuriHalFlash"

#define FURI_HAL_FLASH_PAGE_SIZE           2048U
#define FURI_HAL_FLASH_WRITE_BLOCK         8U
#define FURI_HAL_FLASH_ERASED_DWORD        UINT64_MAX
#define FURI_HAL_FLASH_ERASE_TIMEOUT_US    100000U
#define FURI_HAL_FLASH_PROGRAM_TIMEOUT_US  1000U
#define FURI_HAL_FLASH_KEY1                0x45670123U
#define FURI_HAL_FLASH_KEY2                0xCDEF89ABU
#define FURI_HAL_FLASH_ECC_FLAGS_KEEP_MASK FLASH_ECCR_ECCCIE

#define FURI_HAL_FLASH_SR_ERRORS                                                               \
    (FLASH_SR_OPERR | FLASH_SR_PROGERR | FLASH_SR_WRPERR | FLASH_SR_PGAERR | FLASH_SR_SIZERR | \
     FLASH_SR_PGSERR | FLASH_SR_MISERR | FLASH_SR_FASTERR | FLASH_SR_RDERR | FLASH_SR_OPTVERR)

typedef struct {
    volatile bool ecc_tolerant;
    volatile bool ecc_error;
} FuriHalFlash;

static FuriHalFlash furi_hal_flash = {0};

static size_t furi_hal_flash_get_total_size(void) {
    return FLASH_SIZE;
}

static bool furi_hal_flash_is_program_address(size_t address, size_t size) {
    const size_t base = furi_hal_flash_get_base();
    return (address >= base) && (size <= furi_hal_flash_get_total_size()) &&
           (address - base <= furi_hal_flash_get_total_size() - size);
}

void furi_hal_flash_init(void) {
    const uint32_t errors = READ_REG(FLASH->SR) & FURI_HAL_FLASH_SR_ERRORS;
    if(errors != 0) {
        FURI_LOG_W(TAG, "SR 0x%08lX at init", errors);
        WRITE_REG(FLASH->SR, errors);
    }
    FURI_LOG_I(TAG, "Init OK");
}

size_t furi_hal_flash_get_base(void) {
    return FLASH_BASE;
}

size_t furi_hal_flash_get_page_size(void) {
    return FURI_HAL_FLASH_PAGE_SIZE;
}

size_t furi_hal_flash_get_write_block_size(void) {
    return FURI_HAL_FLASH_WRITE_BLOCK;
}

size_t furi_hal_flash_get_storage_start_address(void) {
    return (size_t)&_storage_start;
}

size_t furi_hal_flash_get_storage_page_count(void) {
    return ((size_t)&_storage_end - (size_t)&_storage_start) / FURI_HAL_FLASH_PAGE_SIZE;
}

int16_t furi_hal_flash_get_page_number(size_t address) {
    if(!furi_hal_flash_is_program_address(address, 1)) {
        return -1;
    }
    return (int16_t)((address - furi_hal_flash_get_base()) / FURI_HAL_FLASH_PAGE_SIZE);
}

bool furi_hal_flash_read(size_t address, void* data, size_t size) {
    furi_check(data);
    furi_check(furi_hal_flash_is_program_address(address, size));

    furi_hal_flash.ecc_error = false;
    furi_hal_flash.ecc_tolerant = true;
    __DSB();
    memcpy(data, (const void*)address, size);
    __DSB();
    furi_hal_flash.ecc_tolerant = false;

    if(furi_hal_flash.ecc_error) {
        FURI_LOG_W(TAG, "ECC error in 0x%08X, %u bytes", address, size);
        return false;
    }
    return true;
}

bool furi_hal_flash_is_erased(size_t address, size_t size) {
    furi_check(address % FURI_HAL_FLASH_WRITE_BLOCK == 0);
    furi_check(size % FURI_HAL_FLASH_WRITE_BLOCK == 0);

    for(size_t offset = 0; offset < size; offset += FURI_HAL_FLASH_WRITE_BLOCK) {
        uint64_t dword;
        if(!furi_hal_flash_read(address + offset, &dword, sizeof(dword))) {
            return false;
        }
        if(dword != FURI_HAL_FLASH_ERASED_DWORD) {
            return false;
        }
    }
    return true;
}

static void furi_hal_flash_unlock(void) {
    if(READ_BIT(FLASH->CR, FLASH_CR_LOCK)) {
        WRITE_REG(FLASH->KEYR, FURI_HAL_FLASH_KEY1);
        __ISB();
        WRITE_REG(FLASH->KEYR, FURI_HAL_FLASH_KEY2);
    }
    furi_check(!READ_BIT(FLASH->CR, FLASH_CR_LOCK));
}

static void furi_hal_flash_lock(void) {
    SET_BIT(FLASH->CR, FLASH_CR_LOCK);
}

static bool furi_hal_flash_wait_flag_cleared(uint32_t flag, uint32_t timeout_us) {
    const FuriHalCortexTimer timer = furi_hal_cortex_timer_get(timeout_us);
    while(READ_BIT(FLASH->SR, flag)) {
        if(furi_hal_cortex_timer_is_expired(timer)) {
            return false;
        }
    }
    return true;
}

static bool
    furi_hal_flash_wait_last_operation(const char* operation, size_t address, uint32_t timeout_us) {
    if(!furi_hal_flash_wait_flag_cleared(FLASH_SR_BSY1, timeout_us)) {
        FURI_LOG_E(TAG, "%s at 0x%08X timed out, SR 0x%08lX", operation, address, FLASH->SR);
        return false;
    }

    const uint32_t errors = READ_REG(FLASH->SR) & FURI_HAL_FLASH_SR_ERRORS;
    WRITE_REG(FLASH->SR, errors | FLASH_SR_EOP);
    if(errors != 0) {
        FURI_LOG_E(TAG, "%s at 0x%08X failed, SR 0x%08lX", operation, address, errors);
        return false;
    }

    if(!furi_hal_flash_wait_flag_cleared(FLASH_SR_CFGBSY, timeout_us)) {
        FURI_LOG_E(TAG, "%s at 0x%08X CFGBSY timed out", operation, address);
        return false;
    }
    return true;
}

static bool furi_hal_flash_begin(const char* operation, size_t address) {
    furi_hal_flash_unlock();

    if(!furi_hal_flash_wait_flag_cleared(FLASH_SR_BSY1, FURI_HAL_FLASH_ERASE_TIMEOUT_US)) {
        FURI_LOG_E(TAG, "%s at 0x%08X: flash busy, SR 0x%08lX", operation, address, FLASH->SR);
        furi_hal_flash_lock();
        return false;
    }
    WRITE_REG(FLASH->SR, FURI_HAL_FLASH_SR_ERRORS | FLASH_SR_EOP);
    if(!furi_hal_flash_wait_flag_cleared(FLASH_SR_CFGBSY, FURI_HAL_FLASH_ERASE_TIMEOUT_US)) {
        FURI_LOG_E(TAG, "%s at 0x%08X: CFGBSY set", operation, address);
        furi_hal_flash_lock();
        return false;
    }
    return true;
}

static void furi_hal_flash_flush_cache(void) {
    if(READ_BIT(FLASH->ACR, FLASH_ACR_ICEN)) {
        CLEAR_BIT(FLASH->ACR, FLASH_ACR_ICEN);
        SET_BIT(FLASH->ACR, FLASH_ACR_ICRST);
        CLEAR_BIT(FLASH->ACR, FLASH_ACR_ICRST);
        SET_BIT(FLASH->ACR, FLASH_ACR_ICEN);
    }
}

bool furi_hal_flash_erase(uint8_t page) {
    furi_check(page < furi_hal_flash_get_total_size() / FURI_HAL_FLASH_PAGE_SIZE);

    const size_t address = furi_hal_flash_get_base() + page * FURI_HAL_FLASH_PAGE_SIZE;
    if(!furi_hal_flash_begin("Erase", address)) {
        return false;
    }

    MODIFY_REG(FLASH->CR, FLASH_CR_PNB | FLASH_CR_BKER, (page << FLASH_CR_PNB_Pos) | FLASH_CR_PER);
    SET_BIT(FLASH->CR, FLASH_CR_STRT);
    const bool success =
        furi_hal_flash_wait_last_operation("Erase", address, FURI_HAL_FLASH_ERASE_TIMEOUT_US);
    CLEAR_BIT(FLASH->CR, FLASH_CR_PER | FLASH_CR_PNB);

    furi_hal_flash_flush_cache();
    furi_hal_flash_lock();
    return success;
}

bool furi_hal_flash_write_dword(size_t address, uint64_t data) {
    furi_check(address % FURI_HAL_FLASH_WRITE_BLOCK == 0);
    furi_check(furi_hal_flash_is_program_address(address, FURI_HAL_FLASH_WRITE_BLOCK));

    if(!furi_hal_flash_is_erased(address, FURI_HAL_FLASH_WRITE_BLOCK)) {
        FURI_LOG_E(TAG, "Program at 0x%08X: not erased", address);
        return false;
    }

    if(!furi_hal_flash_begin("Program", address)) {
        return false;
    }

    SET_BIT(FLASH->CR, FLASH_CR_PG);
    *(volatile uint32_t*)address = (uint32_t)data;
    __ISB();
    *(volatile uint32_t*)(address + sizeof(uint32_t)) = (uint32_t)(data >> 32U);
    const bool success =
        furi_hal_flash_wait_last_operation("Program", address, FURI_HAL_FLASH_PROGRAM_TIMEOUT_US);
    CLEAR_BIT(FLASH->CR, FLASH_CR_PG);

    furi_hal_flash_lock();
    return success;
}

bool furi_hal_flash_handle_ecc_error(void) {
    const uint32_t eccr = READ_REG(FLASH->ECCR);
    if(!(eccr & FLASH_ECCR_ECCD) || !furi_hal_flash.ecc_tolerant) {
        return false;
    }
    WRITE_REG(FLASH->ECCR, (eccr & FURI_HAL_FLASH_ECC_FLAGS_KEEP_MASK) | FLASH_ECCR_ECCD);
    furi_hal_flash.ecc_error = true;
    return true;
}
