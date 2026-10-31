# Patches

`lib/tinyusb/src/class/audio/audio_device.c.patch` - because tinyusb forces format 4-byte 16.16 at any spead for UAC2, but USB specufication needs 3-byte 10.14, see chapter `5.12.4.2 Feedback` in USB2.0 spec, tinyusb PR is declined as not planned - [#4264](https://github.com/hathach/tinyusb/issues/4264)

`lib/FreeRTOS-Kernel/portable/GCC/ARM_CM0/port.c.patch` - ARM_CM0 port defines `SysTick_Handler` itself, renamed to `xPortSysTickHandler` as was in a previous freertos vectors to make `furi_hal_interrupt` owner of the vector
