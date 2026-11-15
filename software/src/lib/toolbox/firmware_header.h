#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FIRMWARE_HEADER_MAGIC        0x46414844U
#define FIRMWARE_HEADER_VERSION_SIZE 48U

typedef struct {
    uint32_t magic;
    uint32_t target;
    uint32_t image_size;
    char version[FIRMWARE_HEADER_VERSION_SIZE];
} FirmwareHeader;

#ifdef __cplusplus
}
#endif
