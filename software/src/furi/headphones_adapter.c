#include <headphones_adapter.h>

#include <applications/services/applications.h>

#include <FreeRTOS.h>
#include <task.h>

#define HEAP_CANARY_VALUE 0x8BADF00D

void headphones_adapter_init(void) {
    for(size_t i = 0; i < SERVICE_APPLICATIONS_COUNT; i++) {
        UNUSED(SERVICE_APPLICATIONS[i]);
    }
}

void vApplicationGetIdleTaskMemory(
    StaticTask_t** tcb_ptr,
    StackType_t** stack_ptr,
    uint32_t* stack_size) {
    *tcb_ptr = malloc(sizeof(StaticTask_t));
    *stack_ptr = malloc(sizeof(StackType_t) * configIDLE_TASK_STACK_DEPTH);
    *stack_size = configIDLE_TASK_STACK_DEPTH;
}

void vApplicationGetTimerTaskMemory(
    StaticTask_t** tcb_ptr,
    StackType_t** stack_ptr,
    uint32_t* stack_size) {
    *tcb_ptr = malloc(sizeof(StaticTask_t));
    *stack_ptr = malloc(sizeof(StackType_t) * configTIMER_TASK_STACK_DEPTH);
    *stack_size = configTIMER_TASK_STACK_DEPTH;
}

void vApplicationGetRandomHeapCanary(portPOINTER_SIZE_TYPE* pxHeapCanary) {
    *pxHeapCanary = HEAP_CANARY_VALUE;
}
