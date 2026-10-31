#pragma once

#if defined(__ICCARM__) || defined(__CC_ARM) || defined(__GNUC__)
#include <stdint.h>
#include <errno.h>
#pragma GCC diagnostic ignored "-Wredundant-decls"
#endif

#ifndef CMSIS_device_header
#define CMSIS_device_header "stm32g0xx.h"
#endif /* CMSIS_device_header */

#include CMSIS_device_header
#include <stm32g0b1_linker.h>

#define configENABLE_MPU 0

#define configUSE_PREEMPTION             1
#define configSUPPORT_STATIC_ALLOCATION  1
#define configSUPPORT_DYNAMIC_ALLOCATION 1
#define configENABLE_HEAP_PROTECTOR      1
#define configHEAP_CLEAR_MEMORY_ON_FREE  1
#define configUSE_MALLOC_FAILED_HOOK     0
#define configUSE_IDLE_HOOK              0
#define configUSE_TICK_HOOK              0
#define configCPU_CLOCK_HZ               (SystemCoreClock)
#define configTICK_RATE_HZ_RAW           1000
#define configTICK_RATE_HZ               ((TickType_t)configTICK_RATE_HZ_RAW)
#define configUSE_16_BIT_TICKS           0
#define configMAX_PRIORITIES             (32)
#define configMINIMAL_STACK_SIZE         ((uint16_t)128)

/* Heap size determined automatically by linker */
#define configTOTAL_HEAP_SIZE   ((uint32_t)&__heap_end__ - (uint32_t)&__heap_start__)
#define configMAX_TASK_NAME_LEN (32)

#define configUSE_TRACE_FACILITY                1
#define configUSE_MUTEXES                       1
#define configQUEUE_REGISTRY_SIZE               0
#define configCHECK_FOR_STACK_OVERFLOW          2
#define configUSE_RECURSIVE_MUTEXES             1
#define configUSE_COUNTING_SEMAPHORES           1
#define configENABLE_BACKWARD_COMPATIBILITY     0
#define configUSE_PORT_OPTIMISED_TASK_SELECTION 0
#define configUSE_TICKLESS_IDLE                 0
#define configRECORD_STACK_HIGH_ADDRESS         1
#define configUSE_NEWLIB_REENTRANT              0

/* Defaults to size_t for backward compatibility, but can be changed
   if lengths will always be less than the number of bytes in a size_t. */
#define configMESSAGE_BUFFER_LENGTH_TYPE        size_t
#define configNUM_THREAD_LOCAL_STORAGE_POINTERS 1
#define configEXPECTED_IDLE_TIME_BEFORE_SLEEP   4

/* Co-routine definitions. */
#define configUSE_CO_ROUTINES 0

/* Software timer definitions. */
#define configUSE_TIMERS              1
#define configTIMER_TASK_PRIORITY     (2)
#define configTIMER_QUEUE_LENGTH      32
#define configTIMER_TASK_STACK_DEPTH  256
#define configTIMER_SERVICE_TASK_NAME "TimersSrv"

#define configIDLE_TASK_NAME        "(-_-)"
#define configIDLE_TASK_STACK_DEPTH 128

/* Set the following definitions to 1 to include the API function, or zero
to exclude the API function. */
#define INCLUDE_xTaskGetHandle              1
#define INCLUDE_eTaskGetState               1
#define INCLUDE_uxTaskGetStackHighWaterMark 1
#define INCLUDE_uxTaskPriorityGet           1
#define INCLUDE_vTaskCleanUpResources       0
#define INCLUDE_vTaskDelay                  1
#define INCLUDE_vTaskDelayUntil             1
#define INCLUDE_vTaskDelete                 1
#define INCLUDE_vTaskPrioritySet            1
#define INCLUDE_vTaskSuspend                1
#define INCLUDE_xQueueGetMutexHolder        1
#define INCLUDE_xTaskGetCurrentTaskHandle   1
#define INCLUDE_xTaskGetSchedulerState      1
#define INCLUDE_xTimerPendFunctionCall      1
#define INCLUDE_xTaskGetIdleTaskHandle      1

/* Workaround for various notification issues:
 * - First one used by system primitives
 * - Second one by thread event notification
 * - Third one by FuriEventLoop
 */
#define configTASK_NOTIFICATION_ARRAY_ENTRIES 3

/* Normal assert() semantics without relying on the provision of an assert.h
header file. */
#ifdef FURI_DEBUG
#define configASSERT(x)                \
    if((x) == 0) {                     \
        furi_crash("FreeRTOS Assert"); \
    }
#endif

// Must be last line of config because of recursion
#include <core/check.h>
