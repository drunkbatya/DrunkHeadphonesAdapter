#include <applications/services/usbc/usbc.h>

#include <furi.h>

int32_t usbc_srv(void* context) {
    UNUSED(context);

    FuriEventLoop* event_loop = furi_event_loop_alloc();
    furi_event_loop_run(event_loop);
    furi_event_loop_free(event_loop);

    return 0;
}
