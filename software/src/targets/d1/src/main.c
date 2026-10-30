#include <furi.h>
#include <furi_hal.h>
#include <headphones_adapter.h>

int main(void) {
    furi_init();
    furi_hal_init_early();
    furi_hal_init();
    headphones_adapter_init();
    furi_run();

    while(1) {
    }
}
