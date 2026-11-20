#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct UsbcBypass UsbcBypass;

typedef void (*UsbcBypassWakeupCallback)(void* context);

UsbcBypass* usbc_bypass_alloc(UsbcBypassWakeupCallback wakeup_callback, void* context);

void usbc_bypass_free(UsbcBypass* bypass);

uint32_t usbc_bypass_process(UsbcBypass* bypass);

bool usbc_bypass_is_enabled(const UsbcBypass* bypass);

#ifdef __cplusplus
}
#endif
