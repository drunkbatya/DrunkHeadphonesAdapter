#pragma once

#include <stdbool.h>
#include <stdint.h>

#include <usb_pd.h>
#include <usb_pd_platform.h>

#include <applications/services/usbc/usbc_port.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RECORD_USBC "usbc"

typedef struct {
    UsbPdPortMode mode;
    bool attached;
    bool partner_not_pd;
    UsbPdPowerRole power_role;
    UsbPdDataRole data_role;
    UsbPdCcPin cc_pin;
    UsbPdRpCurrent partner_rp_current;
    UsbPdSpecRevision spec_revision;
    UsbPdVbusLevel vbus_level;
    UsbPdContract contract;
    UsbPdCapabilities partner_source_capabilities;
    UsbPdCapabilities partner_sink_capabilities;
    UsbPdCapabilities offer;
    bool tcpp03_faulted;
} UsbcPortStatus;

typedef struct {
    UsbcPortStatus ports[UsbcPortCount];
} UsbcStatus;

typedef struct Usbc Usbc;

void usbc_get_status(Usbc* usbc, UsbcStatus* status);

int32_t usbc_srv(void* context);

#ifdef __cplusplus
}
#endif
