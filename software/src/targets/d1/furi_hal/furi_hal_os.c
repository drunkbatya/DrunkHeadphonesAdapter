#include <furi_hal_os.h>
#include <furi_hal_clock.h>
#include <furi_hal_power.h>
#include <furi_hal_idle_timer.h>

#include <stm32g0xx_ll_cortex.h>
#include <stm32g0xx_ll_exti.h>

#include <furi.h>

#include <FreeRTOS.h>
#include <task.h>

#define TAG "FuriHalOs"

#define FURI_HAL_IDLE_TIMER_CLK_HZ 32000
#define FURI_HAL_OS_TICK_HZ        configTICK_RATE_HZ

#define FURI_HAL_OS_IDLE_CNT_TO_TICKS(x) (((x) * FURI_HAL_OS_TICK_HZ) / FURI_HAL_IDLE_TIMER_CLK_HZ)
#define FURI_HAL_OS_TICKS_TO_IDLE_CNT(x) (((x) * FURI_HAL_IDLE_TIMER_CLK_HZ) / FURI_HAL_OS_TICK_HZ)

#define FURI_HAL_IDLE_TIMER_TICK_PER_EPOCH (FURI_HAL_OS_IDLE_CNT_TO_TICKS(FURI_HAL_IDLE_TIMER_MAX))
#define FURI_HAL_OS_MAX_SLEEP              (FURI_HAL_IDLE_TIMER_TICK_PER_EPOCH - 1)

#define FURI_HAL_OS_NVIC_IS_PENDING() (NVIC->ISPR[0])

// Arbitrary (but small) number for better tick consistency
#define FURI_HAL_OS_EXTRA_CNT 3

extern void xPortSysTickHandler(void);

static volatile uint32_t furi_hal_os_skew;

void furi_hal_os_init(void) {
    furi_hal_idle_timer_init();

    FURI_LOG_I(TAG, "Init OK");
}

void furi_hal_os_tick(void) {
    if(xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED) {
        xPortSysTickHandler();
    }
}

static inline bool furi_hal_os_is_pending_irq(void) {
    if(FURI_HAL_OS_NVIC_IS_PENDING()) {
        return true;
    }

    if(LL_EXTI_ReadRisingFlag_0_31(LL_EXTI_LINE_ALL_0_31) ||
       LL_EXTI_ReadFallingFlag_0_31(LL_EXTI_LINE_ALL_0_31)) {
        return true;
    } else if(
        LL_EXTI_ReadRisingFlag_32_63(LL_EXTI_LINE_ALL_32_63) ||
        LL_EXTI_ReadFallingFlag_32_63(LL_EXTI_LINE_ALL_32_63)) {
        return true;
    }

    return false;
}

static inline uint32_t furi_hal_os_sleep(TickType_t expected_idle_ticks) {
    // Stop ticks
    furi_hal_clock_suspend_tick();

    // Start wakeup timer
    furi_hal_idle_timer_start(FURI_HAL_OS_TICKS_TO_IDLE_CNT(expected_idle_ticks));

    // Go to sleep mode
    furi_hal_power_sleep();

    // Calculate how much time we spent in the sleep
    uint32_t after_cnt = furi_hal_idle_timer_get_cnt() + furi_hal_os_skew + FURI_HAL_OS_EXTRA_CNT;
    uint32_t after_tick = FURI_HAL_OS_IDLE_CNT_TO_TICKS(after_cnt);
    furi_hal_os_skew = after_cnt - FURI_HAL_OS_TICKS_TO_IDLE_CNT(after_tick);

    bool cmpm = LL_LPTIM_IsActiveFlag_CMPM(FURI_HAL_IDLE_TIMER);
    bool arrm = LL_LPTIM_IsActiveFlag_ARRM(FURI_HAL_IDLE_TIMER);
    if(cmpm && arrm) after_tick += expected_idle_ticks;

    // Prepare tick timer for new round
    furi_hal_idle_timer_reset();

    // Resume ticks
    furi_hal_clock_resume_tick();
    return after_tick;
}

void vPortSuppressTicksAndSleep(TickType_t expected_idle_ticks) {
    if(!furi_hal_power_sleep_available()) {
        __WFI();
        return;
    }

    TickType_t sleep_ticks = expected_idle_ticks;

    // Limit amount of ticks to maximum that timer can count
    if(sleep_ticks > FURI_HAL_OS_MAX_SLEEP) {
        sleep_ticks = FURI_HAL_OS_MAX_SLEEP;
    }

    // Stop IRQ handling, no one should disturb us till we finish
    __disable_irq();
    do {
        // Confirm OS that sleep is still possible
        if(eTaskConfirmSleepModeStatus() == eAbortSleep || furi_hal_os_is_pending_irq()) {
            break;
        }

        // Sleep and track how much ticks we spent sleeping
        uint32_t completed_ticks = furi_hal_os_sleep(sleep_ticks);
        // Notify system about time spent in sleep
        if(completed_ticks > 0) {
            if(completed_ticks > expected_idle_ticks) {
                completed_ticks = expected_idle_ticks;
            }
            vTaskStepTick(completed_ticks);
        }
    } while(0);
    // Reenable IRQ
    __enable_irq();
}

void vApplicationStackOverflowHook(TaskHandle_t xTask, char* pcTaskName) {
    UNUSED(xTask);
    furi_log_puts("\r\n\r\n stack overflow in ");
    furi_log_puts(pcTaskName);
    furi_log_puts("\r\n\r\n");
    furi_crash("StackOverflow");
}
