#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CRC32_INITIAL 0U

uint32_t crc32_update(uint32_t crc, const void* data, size_t size);

#ifdef __cplusplus
}
#endif
