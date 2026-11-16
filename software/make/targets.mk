all: $(BUILD_DIR)/$(FIRMWARE_NAME).elf $(BUILD_DIR)/$(FIRMWARE_NAME).hex $(BUILD_DIR)/$(FIRMWARE_NAME).bin

.PHONY: release
release: all

.PHONY: generated
generated: $(GENERATED_SOURCES)

.PHONY: lint
lint:
	find src -type d -name '.*' -prune -o -type f -name '*.[ch]' -print \
		| xargs clang-format --Werror --style=file -i --dry-run

.PHONY: format
format:
	find src -type d -name '.*' -prune -o -type f -name '*.[ch]' -print \
		| xargs clang-format --Werror --style=file -i

.PHONY: flash
flash: $(BUILD_DIR)/$(FIRMWARE_NAME).bin
	@echo "\tFLASH\t" $(BUILD_DIR)/$(FIRMWARE_NAME).bin
	@openocd $(TARGET_OPENOCD_CFG) \
		-c init -c "reset halt" \
		-c "flash write_image erase $(BUILD_DIR)/$(FIRMWARE_NAME).bin 0x08000000" \
		-c "reset" -c shutdown

.PHONY: flash_option_bytes
flash_option_bytes:
	@echo "\tFLASH\t option bytes"
	@openocd $(TARGET_OPENOCD_CFG) \
		-c init -c "reset halt" \
		$(TARGET_OPENOCD_OPTION_BYTES_WRITE) \
		-c "reset" -c shutdown

.PHONY: debug
debug: flash
	@openocd $(TARGET_OPENOCD_CFG) \
		-c "init" \
		-c "reset halt" & \
	OPENOCD_PID=$$!; \
	trap 'kill $$OPENOCD_PID 2>/dev/null; wait $$OPENOCD_PID 2>/dev/null' EXIT INT TERM; \
	sleep 1; \
	arm-none-eabi-gdb $(BUILD_DIR)/$(FIRMWARE_NAME).elf \
		-ex "target extended-remote localhost:3333" \
		-ex "monitor reset halt" \
		-ex "tbreak main" \
		-ex "continue" \
		-ex "set variable furi_hal_debug_gdb_session_active = 1" \
		-ex "continue"

.PHONY: clean
clean:
	-rm -fR $(BUILD_ROOT)
	-rm -fR $(GENERATED_DIR)
