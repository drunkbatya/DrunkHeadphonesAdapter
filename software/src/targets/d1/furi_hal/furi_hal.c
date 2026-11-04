#include <furi_hal.h>

void furi_hal_init_early(void) {
    furi_hal_clock_init_early();
    furi_hal_bus_init_early();
    furi_hal_dma_init_early();
    furi_hal_cortex_init_early();
    furi_hal_resources_init_early();
#ifdef FURI_DEBUG
    furi_hal_debug_enable();
#else
    if(furi_hal_debug_is_gdb_session_active()) {
        furi_hal_debug_enable();
    } else {
        furi_hal_debug_disable();
    }
#endif
    furi_hal_os_init();
    furi_hal_i2c_init_early();
}

void furi_hal_init(void) {
    furi_hal_clock_init();
#ifdef FURI_DEBUG
    furi_hal_serial_control_init();
    furi_hal_serial_control_set_logging_config(FuriHalSerialIdLpuart, 230400);
#endif
    furi_hal_interrupt_init();
    furi_hal_resources_init();
    furi_hal_power_init();
    furi_hal_i2c_init();
    furi_hal_i2s_init();
    furi_hal_adc_init();
    furi_hal_usb_init();
    furi_hal_usbpd_init();
}
