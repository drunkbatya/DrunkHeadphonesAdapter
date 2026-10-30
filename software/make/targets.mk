all: $(BUILD_DIR)/$(TARGET).elf $(BUILD_DIR)/$(TARGET).hex $(BUILD_DIR)/$(TARGET).bin

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
flash: $(BUILD_DIR)/$(TARGET).bin
	@echo "\tFLASH\t" $(BUILD_DIR)/$(TARGET).bin
	@openocd $(TARGET_OPENOCD_CFG) \
		-c init -c "reset halt" \
		-c "flash write_image erase $(BUILD_DIR)/$(TARGET).bin 0x08000000" \
		-c "reset" -c shutdown

.PHONY: debug
debug: flash
	@openocd $(TARGET_OPENOCD_CFG) \
		-c "init" \
		-c "reset halt" & \
	OPENOCD_PID=$$!; \
	trap 'kill $$OPENOCD_PID 2>/dev/null; wait $$OPENOCD_PID 2>/dev/null' EXIT INT TERM; \
	sleep 1; \
	arm-none-eabi-gdb $(BUILD_DIR)/$(TARGET).elf \
		-ex "target extended-remote localhost:3333" \
		-ex "monitor reset halt" \
		-ex "continue"

.PHONY: clean
clean:
	-rm -fR $(BUILD_DIR)
	-rm -fR $(GENERATED_DIR)
