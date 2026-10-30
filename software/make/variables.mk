TARGET ?= DrunkHeadphonesAdapter
HW_TARGET ?= d1
HW_TARGET_ID ?= $(subst d,,$(HW_TARGET))
DEBUG ?= 1
OPT ?= -Og
BUILD_DIR ?= build
GENERATED_DIR ?= generated
JOBS ?= $(shell nproc 2>/dev/null || echo 1)

ifeq ($(filter -j% --jobs%,$(MAKEFLAGS)),)
MAKEFLAGS += -j$(JOBS)
endif

OPENOCD_CFG ?= -f interface/stlink.cfg -f target/stm32g0x.cfg

STM_DIR := targets/$(HW_TARGET)/stm

ifeq ($(wildcard $(STM_DIR)),)
$(error Unknown HW_TARGET '$(HW_TARGET)': $(STM_DIR) not found)
endif

TINYUSB_DIR := lib/tinyusb/src
TINYUSB_GLUE_DIR := lib/tinyusb-glue
TINYUSB_GENERATED_DIR := $(GENERATED_DIR)/tinyusb
TINYUSB_AUDIO_DEVICE_SOURCE := $(TINYUSB_DIR)/class/audio/audio_device.c
TINYUSB_AUDIO_DEVICE_PATCH := $(TINYUSB_GLUE_DIR)/audio_device_full_speed_feedback.patch
TINYUSB_AUDIO_DEVICE_PATCHED := $(TINYUSB_GENERATED_DIR)/audio_device.c

USBPD_CORE_LIB := USBPDCORE_PD3_FULL_CM0PLUS_wc32.a

MAKE_FILES := $(shell find make -type f -name '*.mk')
