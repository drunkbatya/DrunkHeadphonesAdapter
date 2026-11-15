#pragma once

#include <adapter.pb.h>
#include <furi.h>

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    RpcUpdateStateIdle,
    RpcUpdateStateWriting,
    RpcUpdateStateVerified,
} RpcUpdateState;

typedef struct {
    RpcUpdateState state;
    uint32_t image_size;
    uint32_t image_crc32;
    uint32_t written;
} RpcUpdate;

adapter_ErrType rpc_update_begin(RpcUpdate* update, const adapter_UpdateBeginRequest* request);

adapter_ErrType rpc_update_write(RpcUpdate* update, adapter_UpdateWriteRequest* request);

adapter_ErrType rpc_update_finish(RpcUpdate* update);

void rpc_update_abort(RpcUpdate* update);

FURI_NORETURN void rpc_update_apply(const RpcUpdate* update);

#ifdef __cplusplus
}
#endif
