#pragma once

#include <applications/services/rpc/rpc.h>

#include <furi.h>

#include <adapter.pb.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct Rpc Rpc;

typedef void (*RpcRequestHandler)(const adapter_RpcRequest* request, void* context);

void rpc_add_handler(Rpc* rpc, pb_size_t request_tag, RpcRequestHandler handler, void* context);

FuriEventLoop* rpc_get_event_loop(Rpc* rpc);

adapter_RpcResponse* rpc_begin_response(Rpc* rpc, uint32_t request_id, pb_size_t response_tag);

void rpc_send_response(Rpc* rpc);

void rpc_send_status(Rpc* rpc, uint32_t request_id, adapter_ErrType err_type);

typedef struct RpcSystem RpcSystem;

RpcSystem* rpc_system_alloc(Rpc* rpc);

void rpc_system_free(RpcSystem* rpc_system);

typedef struct RpcAudio RpcAudio;

RpcAudio* rpc_audio_alloc(Rpc* rpc);

void rpc_audio_free(RpcAudio* rpc_audio);

typedef struct RpcUsbc RpcUsbc;

RpcUsbc* rpc_usbc_alloc(Rpc* rpc);

void rpc_usbc_free(RpcUsbc* rpc_usbc);

#ifdef __cplusplus
}
#endif
