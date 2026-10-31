#include <furi_hal_interrupt.h>
#include <furi_hal_os.h>

void furi_hal_interrupt_init(void) {
}

void SysTick_Handler(void) {
    furi_hal_os_tick();
}
