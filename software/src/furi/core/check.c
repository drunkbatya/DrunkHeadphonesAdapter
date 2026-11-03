#include "check.h"
#include "common_defines.h"
#include "log.h"

#include <stm32g0xx.h>
#include <furi_hal_power.h>
#include <furi_hal_debug.h>
#include <furi_hal_interrupt.h>
#include <stdio.h>

#include <FreeRTOS.h>
#include <task.h>
#include <stdio.h>
#include <stdlib.h>

const char* __furi_check_message = NULL;
uint32_t __furi_check_registers[13] = {0};

/** Load r12 value to __furi_check_message and store registers to __furi_check_registers */
#define GET_MESSAGE_AND_STORE_REGISTERS()               \
    asm volatile("push {r1}                         \n" \
                 "ldr  r1, =__furi_check_registers  \n" \
                 "str  r0, [r1, #0]                 \n" \
                 "pop  {r0}                         \n" \
                 "str  r0, [r1, #4]                 \n" \
                 "str  r2, [r1, #8]                 \n" \
                 "str  r3, [r1, #12]                \n" \
                 "str  r4, [r1, #16]                \n" \
                 "str  r5, [r1, #20]                \n" \
                 "str  r6, [r1, #24]                \n" \
                 "str  r7, [r1, #28]                \n" \
                 "mov  r0, r8                       \n" \
                 "str  r0, [r1, #32]                \n" \
                 "mov  r0, r9                       \n" \
                 "str  r0, [r1, #36]                \n" \
                 "mov  r0, r10                      \n" \
                 "str  r0, [r1, #40]                \n" \
                 "mov  r0, r11                      \n" \
                 "str  r0, [r1, #44]                \n" \
                 "mov  r0, lr                       \n" \
                 "str  r0, [r1, #48]                \n" \
                 "mov  r0, r12                      \n" \
                 "ldr  r1, =__furi_check_message    \n" \
                 "str  r0, [r1]                     \n" \
                 :                                      \
                 :                                      \
                 : "r0", "r1", "memory");

/** Restore registers and halt MCU
 *
 * - Always use it with GET_MESSAGE_AND_STORE_REGISTERS
 * - If debugger is(was) connected this routine will raise bkpt
 * - If debugger is not connected then endless loop
 *
 */
#define RESTORE_REGISTERS_AND_HALT_MCU(debug)           \
    register bool r0 asm("r0") = debug;                 \
    asm volatile("ldr   r1, =__furi_check_registers \n" \
                 "ldr   r2, [r1, #32]               \n" \
                 "mov   r8, r2                      \n" \
                 "ldr   r2, [r1, #36]               \n" \
                 "mov   r9, r2                      \n" \
                 "ldr   r2, [r1, #40]               \n" \
                 "mov   r10, r2                     \n" \
                 "ldr   r2, [r1, #44]               \n" \
                 "mov   r11, r2                     \n" \
                 "cmp   r0, #0                      \n" \
                 "ldm   r1, {r0-r7}                 \n" \
                 "bne   with_debugger%=             \n" \
                 "loop%=:                           \n" \
                 "wfi                               \n" \
                 "b     loop%=                      \n" \
                 "with_debugger%=:                  \n" \
                 "debug_loop%=:                     \n" \
                 "bkpt  0x00                        \n" \
                 "wfi                               \n" \
                 "b     debug_loop%=                \n" \
                 :                                      \
                 : "r"(r0)                              \
                 : "memory");

static void __furi_put_uint32_as_text(uint32_t data) {
    char tmp_str[] = "-2147483648";
    itoa(data, tmp_str, 10);
    furi_log_puts(tmp_str);
}

static void __furi_put_uint32_as_hex(uint32_t data) {
    char tmp_str[] = "0xFFFFFFFF";
    itoa(data, tmp_str, 16);
    furi_log_puts(tmp_str);
}

static void __furi_print_register_info(void) {
    // Print registers
    for(uint8_t i = 0; i < 12; i++) {
        furi_log_puts("\r\n\tr");
        __furi_put_uint32_as_text(i);
        furi_log_puts(" : ");
        __furi_put_uint32_as_hex(__furi_check_registers[i]);
    }

    furi_log_puts("\r\n\tlr : ");
    __furi_put_uint32_as_hex(__furi_check_registers[12]);
}

static void __furi_print_stack_info(void) {
    furi_log_puts("\r\n\tstack watermark: ");
    __furi_put_uint32_as_text(uxTaskGetStackHighWaterMark(NULL) * 4);
}

static void __furi_print_heap_info(void) {
    furi_log_puts("\r\n\t     heap total: ");
    __furi_put_uint32_as_text(configTOTAL_HEAP_SIZE);
    furi_log_puts("\r\n\t      heap free: ");
    __furi_put_uint32_as_text(xPortGetFreeHeapSize());
    HeapStats_t heap_stats;
    vPortGetHeapStats(&heap_stats);
    furi_log_puts("\r\n\t heap watermark: ");
    __furi_put_uint32_as_text(heap_stats.xMinimumEverFreeBytesRemaining);
}

static void __furi_print_name(bool isr) {
    if(isr) {
        uint8_t exception_number = __get_IPSR();
        const char* name = furi_hal_interrupt_get_name(exception_number);
        furi_log_puts("[ISR ");
        if(name) {
            furi_log_puts(name);
        } else {
            __furi_put_uint32_as_text(__get_IPSR());
        }
        furi_log_puts("] ");
    } else {
        const char* name = pcTaskGetName(NULL);
        if(name == NULL) {
            furi_log_puts("[main] ");
        } else {
            furi_log_puts("[");
            furi_log_puts(name);
            furi_log_puts("] ");
        }
    }
}

FURI_NORETURN void __furi_crash_implementation(void) {
    __disable_irq();
    GET_MESSAGE_AND_STORE_REGISTERS();

    bool isr = FURI_IS_IRQ_MODE();

    if(__furi_check_message == NULL) {
        __furi_check_message = "Fatal Error";
    } else if(__furi_check_message == (void*)__FURI_ASSERT_MESSAGE_FLAG) {
        __furi_check_message = "furi_assert failed";
    } else if(__furi_check_message == (void*)__FURI_CHECK_MESSAGE_FLAG) {
        __furi_check_message = "furi_check failed";
    }

    furi_log_puts("\r\n\033[0;31m[CRASH]");
    __furi_print_name(isr);
    furi_log_puts(__furi_check_message);

    __furi_print_register_info();
    if(!isr) {
        __furi_print_stack_info();
    }
    __furi_print_heap_info();

    bool debug = furi_hal_debug_is_gdb_session_active();
#ifdef FURI_NDEBUG
    if(debug) {
#endif
        furi_log_puts("\r\nSystem halted. Connect debugger for more info\r\n");
        furi_log_puts("\033[0m\r\n");
        furi_hal_debug_enable();

        RESTORE_REGISTERS_AND_HALT_MCU(debug);
#ifdef FURI_NDEBUG
    } else {
        furi_log_puts("\r\nRebooting system.\r\n");
        furi_log_puts("\033[0m\r\n");
        furi_hal_power_reset();
    }
#endif
    __builtin_unreachable();
}

FURI_NORETURN void __furi_halt_implementation(void) {
    __disable_irq();
    GET_MESSAGE_AND_STORE_REGISTERS();

    bool isr = FURI_IS_IRQ_MODE();

    if(__furi_check_message == NULL) {
        __furi_check_message = "System halt requested.";
    }

    furi_log_puts("\r\n\033[0;31m[HALT]");
    __furi_print_name(isr);
    furi_log_puts(__furi_check_message);
    furi_log_puts("\r\nSystem halted. Bye-bye!\r\n");
    furi_log_puts("\033[0m\r\n");

    bool debug = furi_hal_debug_is_gdb_session_active();
    RESTORE_REGISTERS_AND_HALT_MCU(debug);

    __builtin_unreachable();
}
