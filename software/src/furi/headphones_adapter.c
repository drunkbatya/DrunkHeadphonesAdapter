#include <headphones_adapter.h>

#include <applications/services/applications.h>

void headphones_adapter_init(void) {
    for(size_t i = 0; i < SERVICE_APPLICATIONS_COUNT; i++) {
        UNUSED(SERVICE_APPLICATIONS[i]);
    }
}
