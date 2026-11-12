#pragma once

#include <stdint.h>

#include <applications/services/usbc/usbc_power.h>
#include <applications/services/usbc/usbc_vbus.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct UsbcPolicy UsbcPolicy;

typedef void (*UsbcPolicyWakeupCallback)(void* context);

UsbcPolicy* usbc_policy_alloc(
    UsbcPower* power,
    UsbcVbus* vbus,
    UsbcPolicyWakeupCallback wakeup_callback,
    void* context);

void usbc_policy_free(UsbcPolicy* policy);

uint32_t usbc_policy_process(UsbcPolicy* policy);

#ifdef __cplusplus
}
#endif
