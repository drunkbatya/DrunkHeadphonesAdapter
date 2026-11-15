#include <toolbox/firmware_header.h>

#include <furi.h>
#include <stm32g0b1_linker.h>
#include <stm32g0xx.h>

PLACE_IN_SECTION(".firmware_header")
const FirmwareHeader firmware_header = {
    .magic = FIRMWARE_HEADER_MAGIC,
    .target = TARGET,
    .image_size = (uint32_t)&_firmware_size,
    .version = VERSION,
};

size_t firmware_header_get_offset(void) {
    return (uintptr_t)&firmware_header - FLASH_BASE;
}
