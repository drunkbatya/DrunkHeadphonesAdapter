/**
 * @file stm32g0b1_linker.h
 *
 * Linker defined symbols. Used in various part of firmware to understand
 * hardware boundaries.
 * 
 */
#pragma once

#ifdef __cplusplus
extern "C" {
typedef const char linker_symbol_t;
#else
typedef const void linker_symbol_t;
#endif

extern linker_symbol_t _estack; /**< end of stack */
extern linker_symbol_t _stack_size; /**< stack size */

extern linker_symbol_t _sidata; /**< data initial value start */
extern linker_symbol_t _sdata; /**< data start */
extern linker_symbol_t _edata; /**< data end */

extern linker_symbol_t _sbss; /**< bss start */
extern linker_symbol_t _ebss; /**< bss end */

extern linker_symbol_t __heap_start__; /**< RAM1 Heap start */
extern linker_symbol_t __heap_end__; /**< RAM1 Heap end */

#ifdef __cplusplus
}
#endif
