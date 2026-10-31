#pragma once

#include <stdlib.h>

#include "core/common_defines.h"
#include "core/check.h"

#include <furi_hal_gpio.h>

#ifdef __cplusplus
extern "C" {
#endif

void furi_init(void);

void furi_run(void);

#ifdef __cplusplus
}
#endif
