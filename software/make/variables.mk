TARGET ?= DrunkHeadphonesAdapter
HW_TARGET ?= d1
HW_TARGET_ID ?= $(subst d,,$(HW_TARGET))
DEBUG ?= 1
OPT ?= -Og
BUILD_DIR ?= build
GENERATED_DIR ?= generated
PATCHES_DIR ?= patches
PROTO_DIR ?= ../protobuf
PROTO_FILE ?= adapter.proto
PROTO_GENERATOR ?= lib/nanopb/generator/nanopb_generator.py
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

PROTO_BASENAME := $(basename $(PROTO_FILE))
PROTO_OUT_DIR := $(GENERATED_DIR)/protobuf
NANOPB_GENERATED_C := $(PROTO_OUT_DIR)/$(PROTO_BASENAME).pb.c
NANOPB_GENERATED_H := $(PROTO_OUT_DIR)/$(PROTO_BASENAME).pb.h

MAKE_FILES := $(shell find make -type f -name '*.mk') $(TARGET_MK)
