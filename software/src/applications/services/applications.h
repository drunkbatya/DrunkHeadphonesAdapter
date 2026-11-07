#pragma once

#include <furi.h>

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int32_t (*ServiceApplicationCallback)(void* context);

typedef struct {
    ServiceApplicationCallback app;
    const char* name;
    size_t stack_size;
} ServiceApplication;

extern const ServiceApplication SERVICE_APPLICATIONS[];
extern const size_t SERVICE_APPLICATIONS_COUNT;

#ifdef __cplusplus
}
#endif
