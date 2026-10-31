MCU := $(TARGET_MCU)

C_DEFS := $(TARGET_DEFS)

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
C_DEFS += -DFURI_DEBUG
else
C_DEFS += -DFURI_NDEBUG
endif

C_INCLUDES := \
	-Isrc \
	-Isrc/furi \
	-Isrc/lib \
	-Isrc/targets/furi_hal_include \
	$(TARGET_INCLUDES) \
	$(LIB_INCLUDES)

CFLAGS += $(MCU) $(C_DEFS) $(C_INCLUDES) $(OPT) -Wall -Werror -fdata-sections -ffunction-sections
ASFLAGS := $(MCU) $(OPT) -Wall -fdata-sections -ffunction-sections

ifeq ($(DEBUG),1)
CFLAGS += -g -gdwarf-2
ASFLAGS += -g -gdwarf-2
endif

CFLAGS += -MMD -MP -MF"$(@:%.o=%.d)"

LIBS := -lc -lm -lnosys
LDFLAGS := $(MCU) -specs=nano.specs -T$(TARGET_LDSCRIPT) $(LIBS) \
	-Wl,-Map=$(BUILD_DIR)/$(TARGET).map,--cref \
	-Wl,--gc-sections
