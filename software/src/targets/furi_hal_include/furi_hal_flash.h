#pragma once

#include <core/common_defines.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FURI_HAL_FLASH_PAGE_SIZE    2048U
#define FURI_HAL_FLASH_PROGRAM_UNIT 8U

bool furi_hal_flash_is_dual_bank(void);

size_t furi_hal_flash_get_bank_size(void);

const uint8_t* furi_hal_flash_get_inactive_bank(void);

bool furi_hal_flash_erase_inactive_page(size_t page);

bool furi_hal_flash_program_inactive(size_t offset, const uint8_t* data, size_t size);

FURI_NORETURN void furi_hal_flash_swap_banks(void);

#ifdef __cplusplus
}
#endif
