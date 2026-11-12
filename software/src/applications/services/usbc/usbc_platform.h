#pragma once

#include <applications/services/usbc/usbc_vbus.h>

#ifdef __cplusplus
extern "C" {
#endif

void usbc_platform_init(UsbcVbus* vbus);

void usbc_platform_deinit(void);

#ifdef __cplusplus
}
#endif
