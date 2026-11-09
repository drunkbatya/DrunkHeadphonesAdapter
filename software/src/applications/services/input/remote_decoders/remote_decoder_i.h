#pragma once

#include <applications/services/input/input.h>

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*RemoteDecoderCallback)(InputKey key, bool pressed, void* context);

typedef struct {
    const char* name;
    void* (*alloc)(RemoteDecoderCallback callback, void* context);
    void (*free)(void* decoder);
} RemoteDecoder;

extern const RemoteDecoder* const REMOTE_DECODERS[];
extern const size_t REMOTE_DECODERS_COUNT;

#ifdef __cplusplus
}
#endif
