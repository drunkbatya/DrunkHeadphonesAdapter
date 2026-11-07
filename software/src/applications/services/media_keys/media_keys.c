#include <applications/services/media_keys/media_keys.h>

#include <furi.h>

int32_t media_keys_srv(void* context) {
    UNUSED(context);

    FuriEventLoop* event_loop = furi_event_loop_alloc();
    furi_event_loop_run(event_loop);
    furi_event_loop_free(event_loop);

    return 0;
}
