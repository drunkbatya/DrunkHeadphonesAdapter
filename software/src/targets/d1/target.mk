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
	lib/FreeRTOS-Kernel/portable/GCC/ARM_CM0/port.c \
	lib/FreeRTOS-Kernel/portable/GCC/ARM_CM0/portasm.c \
	lib/tinyusb/src/portable/st/stm32_fsdev/dcd_stm32_fsdev.c \
	lib/tinyusb/src/portable/st/stm32_fsdev/fsdev_common.c

TARGET_ASM_SOURCES := \
	lib/cmsis-device-g0/Source/Templates/gcc/startup_stm32g0b1xx.s

TARGET_LDSCRIPT := $(TARGET_DIR)/stm32g0b1xx_flash.ld

TARGET_OPENOCD_CFG := -f interface/stlink.cfg -f target/stm32g0x.cfg
