#pragma once

#include <stdbool.h>
#include <stdint.h>

#include <applications/services/usbc/usbc.h>
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

void usbc_policy_start(UsbcPolicy* policy);

uint32_t usbc_policy_process(UsbcPolicy* policy);

void usbc_policy_set_usb_bypass(UsbcPolicy* policy, bool usb_bypass);

bool usbc_policy_take_status_change(UsbcPolicy* policy);

void usbc_policy_fill_status(UsbcPolicy* policy, UsbcStatus* status);

#ifdef __cplusplus
}
#endif
