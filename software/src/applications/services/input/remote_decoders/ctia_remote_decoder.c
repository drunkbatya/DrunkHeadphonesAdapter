#include <applications/services/input/remote_decoders/ctia_remote_decoder.h>

#include <furi.h>

#include <stddef.h>

static void* ctia_remote_decoder_alloc(RemoteDecoderCallback callback, void* context) {
    UNUSED(callback);
    UNUSED(context);
    return NULL;
}

static void ctia_remote_decoder_free(void* decoder) {
    UNUSED(decoder);
}

const RemoteDecoder ctia_remote_decoder = {
    .name = "ctia",
    .alloc = ctia_remote_decoder_alloc,
    .free = ctia_remote_decoder_free,
};
