#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void furi_hal_flash_init(void);

size_t furi_hal_flash_get_base(void);

size_t furi_hal_flash_get_page_size(void);

size_t furi_hal_flash_get_write_block_size(void);

size_t furi_hal_flash_get_storage_start_address(void);

size_t furi_hal_flash_get_storage_page_count(void);

int16_t furi_hal_flash_get_page_number(size_t address);

bool furi_hal_flash_read(size_t address, void* data, size_t size);

bool furi_hal_flash_is_erased(size_t address, size_t size);

bool furi_hal_flash_erase(uint8_t page);

bool furi_hal_flash_write_dword(size_t address, uint64_t data);

bool furi_hal_flash_handle_ecc_error(void);

#ifdef __cplusplus
}
#endif
