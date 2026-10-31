#include <furi_hal_os.h>
#include <furi.h>

#include <FreeRTOS.h>
#include <task.h>

extern void xPortSysTickHandler(void);

void furi_hal_os_tick(void) {
    if(xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED) {
        xPortSysTickHandler();
    }
}

void vApplicationStackOverflowHook(TaskHandle_t xTask, char* pcTaskName) {
    UNUSED(xTask);
    UNUSED(pcTaskName);
    furi_crash("StackOverflow");
}
