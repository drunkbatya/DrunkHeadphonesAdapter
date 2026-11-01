#include <headphones_adapter.h>

#include <applications/services/applications.h>

#include <FreeRTOS.h>
#include <task.h>

#define TAG "HeadphonesAdapter"

#define HEAP_CANARY_VALUE 0x8BADF00D

void headphones_adapter_init(void) {
    FURI_LOG_I(TAG, "Starting services");

    for(size_t i = 0; i < SERVICE_APPLICATIONS_COUNT; i++) {
        FURI_LOG_D(TAG, "Starting service %s", SERVICE_APPLICATIONS[i].name);

        FuriThread* thread = furi_thread_alloc_service(
            SERVICE_APPLICATIONS[i].name,
            SERVICE_APPLICATIONS[i].stack_size,
            SERVICE_APPLICATIONS[i].app,
            NULL);

        furi_thread_start(thread);
    }

    FURI_LOG_I(TAG, "Startup complete");
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
