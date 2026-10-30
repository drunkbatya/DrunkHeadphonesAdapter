patched = $(eval $(call patched_source_rules,$(1)))$(GENERATED_DIR)/$(1)

define patched_source_rules
GENERATED_SOURCES += $(GENERATED_DIR)/$(1)
$(BUILD_DIR)/$(GENERATED_DIR)/$(basename $(1)).o: CFLAGS += -iquote $(dir $(1))
endef
