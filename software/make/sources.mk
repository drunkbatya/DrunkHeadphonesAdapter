SRC_SOURCES := \
	$(shell find src -type d -name '.*' -prune -o -path src/targets -prune -o -type f -name '*.c' -print) \
	$(shell find $(TARGET_DIR) -type f -name '*.c')

LIB_SOURCES := \
	$(TARGET_LIB_SOURCES) \
	lib/FreeRTOS-Kernel/event_groups.c \
	lib/FreeRTOS-Kernel/list.c \
	lib/FreeRTOS-Kernel/queue.c \
	lib/FreeRTOS-Kernel/stream_buffer.c \
	lib/FreeRTOS-Kernel/tasks.c \
	lib/FreeRTOS-Kernel/timers.c \
	lib/tinyusb/src/tusb.c \
	lib/tinyusb/src/common/tusb_fifo.c \
	lib/tinyusb/src/device/usbd.c \
	$(call patched,lib/tinyusb/src/class/audio/audio_device.c)

LIB_INCLUDES := \
	-Ilib/FreeRTOS-Kernel/include \
	-Ilib/mlib \
	-Ilib/tinyusb/src

C_SOURCES := $(SRC_SOURCES) $(LIB_SOURCES)
ASM_SOURCES := $(TARGET_ASM_SOURCES)

C_OBJECTS := $(addprefix $(BUILD_DIR)/,$(C_SOURCES:.c=.o))
ASM_OBJECTS := $(addprefix $(BUILD_DIR)/,$(ASM_SOURCES:.s=.o))
OBJECTS := $(C_OBJECTS) $(ASM_OBJECTS)
