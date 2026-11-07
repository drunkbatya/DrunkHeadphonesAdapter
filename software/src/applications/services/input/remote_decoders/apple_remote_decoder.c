#include <applications/services/input/remote_decoders/apple_remote_decoder.h>

#include <furi.h>

#include <stddef.h>

static void* apple_remote_decoder_alloc(RemoteDecoderCallback callback, void* context) {
    UNUSED(callback);
    UNUSED(context);
    return NULL;
}

static void apple_remote_decoder_free(void* decoder) {
    UNUSED(decoder);
}

const RemoteDecoder apple_remote_decoder = {
    .name = "apple",
    .alloc = apple_remote_decoder_alloc,
    .free = apple_remote_decoder_free,
};
