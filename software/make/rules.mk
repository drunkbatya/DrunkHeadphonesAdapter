$(BUILD_DIR):
	@mkdir -p $@

$(GENERATED_DIR)/%: % $(PATCHES_DIR)/%.patch $(MAKE_FILES)
	@echo "\tPATCH\t" $<
	@mkdir -p $(@D)
	@patch --silent --forward --no-backup-if-mismatch --reject-file=- --output=$@ $< $(PATCHES_DIR)/$*.patch

.SECONDARY: $(GENERATED_SOURCES)

$(BUILD_DIR)/%.o: %.c $(MAKE_FILES) | $(BUILD_DIR)
	@echo "\tCC\t" $<
	@mkdir -p $(@D)
	@$(CC) -c $(CFLAGS) $< -o $@

$(BUILD_DIR)/%.o: %.s $(MAKE_FILES) | $(BUILD_DIR)
	@echo "\tASM\t" $<
	@mkdir -p $(@D)
	@$(AS) -c $(ASFLAGS) $< -o $@

$(BUILD_DIR)/$(TARGET).elf: $(OBJECTS) $(TARGET_LDSCRIPT) $(MAKE_FILES) | $(BUILD_DIR)
	@echo "\tLD\t" $@
	@$(CC) $(OBJECTS) $(LDFLAGS) -o $@
	@$(SZ) $@

$(BUILD_DIR)/%.hex: $(BUILD_DIR)/%.elf | $(BUILD_DIR)
	@echo "\tHEX\t" $@
	@$(HEX) $< $@

$(BUILD_DIR)/%.bin: $(BUILD_DIR)/%.elf | $(BUILD_DIR)
	@echo "\tBIN\t" $@
	@$(BIN) $< $@

-include $(OBJECTS:.o=.d)
