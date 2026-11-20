#include <toolbox/crc32.h>

#define CRC32_POLYNOMIAL 0xEDB88320U
#define CRC32_BITS       8

uint32_t crc32_update(uint32_t crc, const void* data, size_t size) {
    const uint8_t* bytes = data;

    crc = ~crc;
    for(size_t i = 0; i < size; i++) {
        crc ^= bytes[i];
        for(uint8_t bit = 0; bit < CRC32_BITS; bit++) {
            crc = (crc >> 1) ^ (CRC32_POLYNOMIAL & (0U - (crc & 1U)));
        }
    }
    return ~crc;
}
