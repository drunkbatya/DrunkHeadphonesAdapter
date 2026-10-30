C_SOURCES := \
	$(STM_DIR)/Core/Src/main.c \
	$(STM_DIR)/Core/Src/stm32g0xx_it.c \
	$(STM_DIR)/Core/Src/stm32g0xx_hal_msp.c \
	$(STM_DIR)/Core/Src/system_stm32g0xx.c \
	$(STM_DIR)/Core/Src/sysmem.c \
	$(STM_DIR)/Core/Src/syscalls.c \
	$(STM_DIR)/USBPD/Target/usbpd_dpm_user.c \
	$(STM_DIR)/USBPD/Target/usbpd_pwr_user.c \
	$(STM_DIR)/USBPD/Target/usbpd_vdm_user.c \
	$(STM_DIR)/USBPD/App/usbpd.c \
	$(STM_DIR)/USBPD/App/usbpd_pwr_if.c \
	$(STM_DIR)/USBPD/App/usbpd_dpm_core.c \
	$(STM_DIR)/USB_Device/App/usb_device.c \
	$(STM_DIR)/USB_Device/App/usbd_desc.c \
	$(STM_DIR)/USB_Device/App/usbd_audio_if.c \
	$(STM_DIR)/USB_Device/Target/usbd_conf.c \
	$(STM_DIR)/Drivers/STM32G0xx_HAL_Driver/Src/stm32g0xx_hal_pcd.c \
	$(STM_DIR)/Drivers/STM32G0xx_HAL_Driver/Src/stm32g0xx_hal_pcd_ex.c \
	$(STM_DIR)/Drivers/STM32G0xx_HAL_Driver/Src/stm32g0xx_ll_usb.c \
	$(STM_DIR)/Drivers/STM32G0xx_HAL_Driver/Src/stm32g0xx_hal_rcc.c \
	$(STM_DIR)/Drivers/STM32G0xx_HAL_Driver/Src/stm32g0xx_hal_rcc_ex.c \
	$(STM_DIR)/Drivers/STM32G0xx_HAL_Driver/Src/stm32g0xx_ll_rcc.c \
	$(STM_DIR)/Drivers/STM32G0xx_HAL_Driver/Src/stm32g0xx_hal_flash.c \
	$(STM_DIR)/Drivers/STM32G0xx_HAL_Driver/Src/stm32g0xx_hal_flash_ex.c \
	$(STM_DIR)/Drivers/STM32G0xx_HAL_Driver/Src/stm32g0xx_hal_gpio.c \
	$(STM_DIR)/Drivers/STM32G0xx_HAL_Driver/Src/stm32g0xx_hal_dma.c \
	$(STM_DIR)/Drivers/STM32G0xx_HAL_Driver/Src/stm32g0xx_hal_dma_ex.c \
	$(STM_DIR)/Drivers/STM32G0xx_HAL_Driver/Src/stm32g0xx_hal_pwr.c \
	$(STM_DIR)/Drivers/STM32G0xx_HAL_Driver/Src/stm32g0xx_hal_pwr_ex.c \
	$(STM_DIR)/Drivers/STM32G0xx_HAL_Driver/Src/stm32g0xx_hal_cortex.c \
	$(STM_DIR)/Drivers/STM32G0xx_HAL_Driver/Src/stm32g0xx_hal.c \
	$(STM_DIR)/Drivers/STM32G0xx_HAL_Driver/Src/stm32g0xx_hal_exti.c \
	$(STM_DIR)/Drivers/STM32G0xx_HAL_Driver/Src/stm32g0xx_ll_utils.c \
	$(STM_DIR)/Drivers/STM32G0xx_HAL_Driver/Src/stm32g0xx_ll_exti.c \
	$(STM_DIR)/Drivers/STM32G0xx_HAL_Driver/Src/stm32g0xx_ll_crs.c \
	$(STM_DIR)/Drivers/STM32G0xx_HAL_Driver/Src/stm32g0xx_ll_ucpd.c \
	$(STM_DIR)/Drivers/STM32G0xx_HAL_Driver/Src/stm32g0xx_ll_gpio.c \
	$(STM_DIR)/Drivers/STM32G0xx_HAL_Driver/Src/stm32g0xx_ll_dma.c \
	$(STM_DIR)/Drivers/STM32G0xx_HAL_Driver/Src/stm32g0xx_ll_tim.c \
	$(STM_DIR)/Drivers/STM32G0xx_HAL_Driver/Src/stm32g0xx_ll_spi.c \
	$(STM_DIR)/Drivers/STM32G0xx_HAL_Driver/Src/stm32g0xx_ll_i2c.c \
	$(STM_DIR)/Drivers/STM32G0xx_HAL_Driver/Src/stm32g0xx_ll_adc.c \
	$(STM_DIR)/Drivers/STM32G0xx_HAL_Driver/Src/stm32g0xx_ll_lpuart.c \
	$(STM_DIR)/Drivers/BSP/Components/tcpp0203/tcpp0203.c \
	$(STM_DIR)/Drivers/BSP/Components/tcpp0203/tcpp0203_reg.c \
	$(STM_DIR)/Middlewares/ST/STM32_USBPD_Library/Core/src/usbpd_trace.c \
	$(STM_DIR)/Middlewares/ST/STM32_USBPD_Library/Devices/STM32G0XX/src/usbpd_cad_hw_if.c \
	$(STM_DIR)/Middlewares/ST/STM32_USBPD_Library/Devices/STM32G0XX/src/usbpd_hw.c \
	$(STM_DIR)/Middlewares/ST/STM32_USBPD_Library/Devices/STM32G0XX/src/usbpd_hw_if_it.c \
	$(STM_DIR)/Middlewares/ST/STM32_USBPD_Library/Devices/STM32G0XX/src/usbpd_phy.c \
	$(STM_DIR)/Middlewares/ST/STM32_USBPD_Library/Devices/STM32G0XX/src/usbpd_phy_hw_if.c \
	$(STM_DIR)/Middlewares/ST/STM32_USBPD_Library/Devices/STM32G0XX/src/usbpd_pwr_hw_if.c \
	$(STM_DIR)/Middlewares/ST/STM32_USBPD_Library/Devices/STM32G0XX/src/usbpd_timersserver.c \
	$(STM_DIR)/Middlewares/ST/STM32_USB_Device_Library/Core/Src/usbd_core.c \
	$(STM_DIR)/Middlewares/ST/STM32_USB_Device_Library/Core/Src/usbd_ctlreq.c \
	$(STM_DIR)/Middlewares/ST/STM32_USB_Device_Library/Core/Src/usbd_ioreq.c \
	$(STM_DIR)/Middlewares/ST/STM32_USB_Device_Library/Class/AUDIO/Src/usbd_audio.c

C_SOURCES += $(shell find src -type d -name '.*' -prune -o -type f -name '*.c' -print)

ASM_SOURCES := $(STM_DIR)/startup_stm32g0b1xx.s

C_OBJECTS := $(addprefix $(BUILD_DIR)/,$(C_SOURCES:.c=.o))
ASM_OBJECTS := $(addprefix $(BUILD_DIR)/,$(ASM_SOURCES:.s=.o))
OBJECTS := $(C_OBJECTS) $(ASM_OBJECTS)

LDSCRIPT := $(STM_DIR)/STM32G0B1xx_FLASH.ld
