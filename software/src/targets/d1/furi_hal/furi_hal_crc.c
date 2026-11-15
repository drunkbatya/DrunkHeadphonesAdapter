#include <furi_hal_crc.h>
#include <furi_hal_bus.h>

#include <stm32g0xx.h>

#define FURI_HAL_CRC32_FINAL_XOR 0xFFFFFFFFU

uint32_t furi_hal_crc32(const uint8_t* data, size_t size) {
    furi_hal_bus_enable(FuriHalBusCRC);

    WRITE_REG(CRC->CR, CRC_CR_REV_IN_0 | CRC_CR_REV_OUT | CRC_CR_RESET);
    for(size_t i = 0; i < size; i++) {
        *(volatile uint8_t*)&CRC->DR = data[i];
    }
    const uint32_t crc = READ_REG(CRC->DR) ^ FURI_HAL_CRC32_FINAL_XOR;

    furi_hal_bus_disable(FuriHalBusCRC);
    return crc;
}
