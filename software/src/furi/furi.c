#include "furi.h"

#include <FreeRTOS.h>
#include <task.h>

void furi_init(void) {
    furi_check(!FURI_IS_ISR());
    furi_check(xTaskGetSchedulerState() == taskSCHEDULER_NOT_STARTED);
}

void furi_run(void) {
    furi_check(!FURI_IS_ISR());
    furi_check(xTaskGetSchedulerState() == taskSCHEDULER_NOT_STARTED);

    /* Start the kernel scheduler */
    vTaskStartScheduler();
}
