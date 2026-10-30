TARGET ?= DrunkHeadphonesAdapter
HW_TARGET ?= d1
HW_TARGET_ID ?= $(subst d,,$(HW_TARGET))
DEBUG ?= 1
OPT ?= -Og
BUILD_DIR ?= build
GENERATED_DIR ?= generated
PATCHES_DIR ?= patches
JOBS ?= $(shell nproc 2>/dev/null || echo 1)

ifeq ($(filter -j% --jobs%,$(MAKEFLAGS)),)
MAKEFLAGS += -j$(JOBS)
endif

TARGET_DIR := src/targets/$(HW_TARGET)
TARGET_MK := $(TARGET_DIR)/target.mk

ifeq ($(wildcard $(TARGET_MK)),)
$(error Unknown HW_TARGET '$(HW_TARGET)': $(TARGET_MK) not found)
endif

GENERATED_SOURCES :=

MAKE_FILES := $(shell find make -type f -name '*.mk') $(TARGET_MK)
