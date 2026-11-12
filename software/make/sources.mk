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
	lib/tinyusb/src/class/hid/hid_device.c \
	lib/tinyusb/src/class/cdc/cdc_device.c \
	lib/nanopb/pb_common.c \
	lib/nanopb/pb_decode.c \
	lib/nanopb/pb_encode.c \
	$(call patched,lib/tinyusb/src/class/audio/audio_device.c) \
	$(shell find lib/usbpd-core-g0 -type f -name '*.c')

LIB_INCLUDES := \
	-Ilib/FreeRTOS-Kernel/include \
	-Ilib/mlib \
	-Ilib/nanopb \
	-I$(PROTO_OUT_DIR) \
	-Ilib/tinyusb/src \
	-Ilib/usbpd-core-g0/include \
	-Ilib/usbpd-core-g0

GENERATED_SOURCES += $(NANOPB_GENERATED_C) $(NANOPB_GENERATED_H)

C_SOURCES := $(SRC_SOURCES) $(LIB_SOURCES) $(NANOPB_GENERATED_C)
ASM_SOURCES := $(TARGET_ASM_SOURCES)

C_OBJECTS := $(addprefix $(BUILD_DIR)/,$(C_SOURCES:.c=.o))
ASM_OBJECTS := $(addprefix $(BUILD_DIR)/,$(ASM_SOURCES:.s=.o))
OBJECTS := $(C_OBJECTS) $(ASM_OBJECTS)
