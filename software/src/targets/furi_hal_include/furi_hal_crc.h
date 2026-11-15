#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

uint32_t furi_hal_crc32(const uint8_t* data, size_t size);

#ifdef __cplusplus
}
#endif
