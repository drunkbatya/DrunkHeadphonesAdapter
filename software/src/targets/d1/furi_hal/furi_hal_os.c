#include <furi.h>

#include <FreeRTOS.h>
#include <task.h>

void vApplicationStackOverflowHook(TaskHandle_t xTask, char* pcTaskName) {
    UNUSED(xTask);
    UNUSED(pcTaskName);
    furi_crash("StackOverflow");
}
