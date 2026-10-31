#include "check.h"
#include "common_defines.h"

#include <stm32g0xx.h>

const char* __furi_check_message = NULL;

#define GET_MESSAGE()                        \
    do {                                     \
        register const char* r12 asm("r12"); \
        asm volatile("" : "=r"(r12));        \
        __furi_check_message = r12;          \
    } while(0)

FURI_NORETURN void __furi_crash_implementation(void) {
    __disable_irq();
    GET_MESSAGE();

    if(__furi_check_message == NULL) {
        __furi_check_message = "Fatal Error";
    } else if(__furi_check_message == (void*)__FURI_ASSERT_MESSAGE_FLAG) {
        __furi_check_message = "furi_assert failed";
    } else if(__furi_check_message == (void*)__FURI_CHECK_MESSAGE_FLAG) {
        __furi_check_message = "furi_check failed";
    }

#ifdef FURI_DEBUG
    __BKPT(0);
#endif
    NVIC_SystemReset();
}

FURI_NORETURN void __furi_halt_implementation(void) {
    __disable_irq();
    GET_MESSAGE();

    if(__furi_check_message == NULL) {
        __furi_check_message = "System halt requested.";
    }

#ifdef FURI_DEBUG
    __BKPT(0);
#endif
    while(true) {
        __WFI();
    }
}
