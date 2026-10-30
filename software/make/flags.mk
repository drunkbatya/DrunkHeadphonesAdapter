MCU := -mcpu=cortex-m0plus -mthumb

C_DEFS := \
	-DUSE_FULL_LL_DRIVER \
	-DUSE_HAL_DRIVER \
	-DSTM32G0B1xx \
	-DUSBPD_PORT_COUNT=2 \
	-D_DRP \
	-DUSBPDCORE_LIB_PD3_FULL \
	-DTCPP0203_SUPPORT

C_DEFS += \
	'-DGIT_COMMIT="$(GIT_COMMIT)"' \
	'-DGIT_BRANCH="$(GIT_BRANCH)"' \
	'-DGIT_BRANCH_NUM="$(GIT_BRANCH_NUM)"' \
	'-DBUILD_DATE="$(BUILD_DATE)"' \
	'-DVERSION="$(VERSION)"' \
	'-DBUILD_DIRTY=$(GIT_DIRTY)' \
	'-DFIRMWARE_ORIGIN="DrunkHeadphonesAdapter"' \
	'-DGIT_ORIGIN="$(GIT_ORIGIN)"' \
	-DTARGET=$(HW_TARGET_ID)

ifeq ($(DEBUG),1)
C_DEFS += -DDEBUG
endif

C_INCLUDES := \
	-Isrc \
	-I$(STM_DIR)/Core/Inc \
	-I$(STM_DIR)/USBPD/App \
	-I$(STM_DIR)/USBPD/Target \
	-I$(STM_DIR)/USB_Device/App \
	-I$(STM_DIR)/USB_Device/Target \
	-I$(STM_DIR)/Drivers/STM32G0xx_HAL_Driver/Inc \
	-I$(STM_DIR)/Drivers/STM32G0xx_HAL_Driver/Inc/Legacy \
	-I$(STM_DIR)/Drivers/CMSIS/Device/ST/STM32G0xx/Include \
	-I$(STM_DIR)/Drivers/CMSIS/Include \
	-I$(STM_DIR)/Drivers/BSP/Components/tcpp0203 \
	-I$(STM_DIR)/Middlewares/ST/STM32_USBPD_Library/Core/inc \
	-I$(STM_DIR)/Middlewares/ST/STM32_USBPD_Library/Devices/STM32G0XX/inc \
	-I$(STM_DIR)/Middlewares/ST/STM32_USB_Device_Library/Core/Inc \
	-I$(STM_DIR)/Middlewares/ST/STM32_USB_Device_Library/Class/AUDIO/Inc

CFLAGS += $(MCU) $(C_DEFS) $(C_INCLUDES) $(OPT) -Wall -Werror -fdata-sections -ffunction-sections
ASFLAGS := $(MCU) $(OPT) -Wall -fdata-sections -ffunction-sections

ifeq ($(DEBUG),1)
CFLAGS += -g -gdwarf-2
ASFLAGS += -g -gdwarf-2
endif

CFLAGS += -MMD -MP -MF"$(@:%.o=%.d)"

LIBS := -l:$(USBPD_CORE_LIB) -lc -lm -lnosys
LIBDIR := -L$(STM_DIR)/Middlewares/ST/STM32_USBPD_Library/Core/lib
LDFLAGS := $(MCU) -specs=nano.specs -T$(LDSCRIPT) $(LIBDIR) $(LIBS) \
	-Wl,-Map=$(BUILD_DIR)/$(TARGET).map,--cref \
	-Wl,--gc-sections
