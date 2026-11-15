TARGET_MCU := -mcpu=cortex-m0plus -mthumb

TARGET_DEFS := \
	-DSTM32G0B1xx \
	-DUSE_FULL_LL_DRIVER

TARGET_INCLUDES := \
	-I$(TARGET_DIR)/inc \
	-I$(TARGET_DIR)/furi_hal \
	-Ilib/cmsis-core/CMSIS/Core/Include \
	-Ilib/cmsis-device-g0/Include \
	-Ilib/stm32g0xx-hal-driver/Inc \
	-Ilib/FreeRTOS-Kernel/portable/GCC/ARM_CM0

TARGET_LIB_SOURCES := \
	lib/cmsis-device-g0/Source/Templates/system_stm32g0xx.c \
	lib/stm32g0xx-hal-driver/Src/stm32g0xx_ll_utils.c \
	lib/stm32g0xx-hal-driver/Src/stm32g0xx_ll_rcc.c \
	lib/stm32g0xx-hal-driver/Src/stm32g0xx_ll_adc.c \
	lib/stm32g0xx-hal-driver/Src/stm32g0xx_ll_dma.c \
	lib/stm32g0xx-hal-driver/Src/stm32g0xx_ll_i2c.c \
	lib/stm32g0xx-hal-driver/Src/stm32g0xx_ll_lpuart.c \
	lib/stm32g0xx-hal-driver/Src/stm32g0xx_ll_ucpd.c \
	$(call patched,lib/FreeRTOS-Kernel/portable/GCC/ARM_CM0/port.c) \
	lib/FreeRTOS-Kernel/portable/GCC/ARM_CM0/portasm.c \
	$(call patched,lib/tinyusb/src/portable/st/stm32_fsdev/dcd_stm32_fsdev.c) \
	lib/tinyusb/src/portable/st/stm32_fsdev/fsdev_common.c

TARGET_ASM_SOURCES := \
	lib/cmsis-device-g0/Source/Templates/gcc/startup_stm32g0b1xx.s

TARGET_LDSCRIPT := $(TARGET_DIR)/stm32g0b1xx_flash.ld

TARGET_OPENOCD_CFG := -f interface/stlink.cfg -f target/stm32g0x.cfg \
	-c "reset_config srst_only srst_nogate connect_assert_srst"

TARGET_FLASH_OPTR_OFFSET := 0x20
TARGET_FLASH_OPTR_ADDRESS := 0x40022020
TARGET_FLASH_OPTR_BANK_BITS := 0x00300000

TARGET_OPENOCD_OPTION_BYTES_WRITE := \
	-c "set optr [read_memory $(TARGET_FLASH_OPTR_ADDRESS) 32 1]; \
		if {($$optr & $(TARGET_FLASH_OPTR_BANK_BITS)) == $(TARGET_FLASH_OPTR_BANK_BITS)} { \
		echo [format \"OPTR 0x%08X unchanged\" $$optr] } else { \
		stm32l4x option_write 0 $(TARGET_FLASH_OPTR_OFFSET) $(TARGET_FLASH_OPTR_BANK_BITS) $(TARGET_FLASH_OPTR_BANK_BITS); \
		stm32l4x option_load 0 }"
