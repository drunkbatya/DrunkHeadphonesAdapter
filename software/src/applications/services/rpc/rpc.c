#include <applications/services/rpc/rpc.h>

#include <furi.h>
#include <furi_hal_usb_cdc.h>
#include <toolbox/version.h>

#include <adapter.pb.h>
#include <pb_decode.h>
#include <pb_encode.h>

#include <string.h>

#define TAG "RpcSrv"

#define RPC_FRAME_SIZE_MAX       256
#define RPC_RX_BUFFER_SIZE       (RPC_FRAME_SIZE_MAX + CDC_DATA_SZ)
#define RPC_TX_STREAM_SIZE       (RPC_FRAME_SIZE_MAX * 2)
#define RPC_TX_STREAM_TRIGGER    1
#define RPC_FRAME_TIMEOUT_MS     500
#define RPC_VARINT32_SIZE_MAX    5
#define RPC_REQUEST_ID_UNKNOWN   0
#define RPC_INTERNAL_EVENT_Q_LEN 8

typedef enum {
    RpcInternalEventConnected,
    RpcInternalEventDisconnected,
    RpcInternalEventTxDone,
    RpcInternalEventRx,
} RpcInternalEvent;

typedef struct {
    FuriEventLoop* event_loop;
    FuriMessageQueue* internal_evt_queue;
    FuriEventLoopTimer* frame_timer;
    FuriStreamBuffer* tx_stream;
    bool is_connected;
    volatile bool is_currently_transmitting;
    uint8_t rx_frame[RPC_RX_BUFFER_SIZE];
    size_t rx_frame_size;
    uint8_t tx_frame[RPC_FRAME_SIZE_MAX];
    adapter_RpcRequest request;
    adapter_RpcResponse response;
} Rpc;

static void rpc_frame_timer_callback(void* context);

static Rpc* rpc_alloc(void) {
    Rpc* rpc = malloc(sizeof(Rpc));

    rpc->event_loop = furi_event_loop_alloc();
    rpc->internal_evt_queue =
        furi_message_queue_alloc(RPC_INTERNAL_EVENT_Q_LEN, sizeof(RpcInternalEvent));
    rpc->frame_timer = furi_event_loop_timer_alloc(
        rpc->event_loop, rpc_frame_timer_callback, FuriEventLoopTimerTypeOnce, rpc);
    rpc->tx_stream = furi_stream_buffer_alloc(RPC_TX_STREAM_SIZE, RPC_TX_STREAM_TRIGGER);
    rpc->is_connected = false;
    rpc->is_currently_transmitting = false;
    rpc->rx_frame_size = 0;

    return rpc;
}

static void rpc_free(Rpc* rpc) {
    furi_stream_buffer_free(rpc->tx_stream);
    furi_event_loop_timer_free(rpc->frame_timer);
    furi_message_queue_free(rpc->internal_evt_queue);
    furi_event_loop_free(rpc->event_loop);

    free(rpc);
}

static void rpc_drop_rx_frame(Rpc* rpc) {
    rpc->rx_frame_size = 0;
    furi_event_loop_timer_stop(rpc->frame_timer);
}

static void rpc_frame_timer_callback(void* context) {
    Rpc* rpc = context;

    FURI_LOG_E(TAG, "Frame timeout, %u bytes dropped", rpc->rx_frame_size);
    rpc->rx_frame_size = 0;
}

static void rpc_maybe_send_data(Rpc* rpc) {
    if(rpc->is_currently_transmitting) return;
    if(!rpc->is_connected) return;

    uint8_t buf[CDC_DATA_SZ];
    size_t length = furi_stream_buffer_receive(rpc->tx_stream, buf, sizeof(buf), 0);
    if(length > 0) {
        rpc->is_currently_transmitting = true;
        furi_hal_cdc_send(buf, length);
    }
}

static void rpc_send_response(Rpc* rpc) {
    pb_ostream_t stream = pb_ostream_from_buffer(rpc->tx_frame, sizeof(rpc->tx_frame));
    if(!pb_encode_ex(&stream, adapter_RpcResponse_fields, &rpc->response, PB_ENCODE_DELIMITED)) {
        FURI_LOG_E(TAG, "Response encode failed: %s", PB_GET_ERROR(&stream));
        return;
    }

    const size_t space = furi_stream_buffer_spaces_available(rpc->tx_stream);
    if(space < stream.bytes_written) {
        FURI_LOG_E(
            TAG, "Response of %u bytes dropped, %u bytes free", stream.bytes_written, space);
        return;
    }

    furi_check(
        furi_stream_buffer_send(rpc->tx_stream, rpc->tx_frame, stream.bytes_written, 0) ==
        stream.bytes_written);
    rpc_maybe_send_data(rpc);
}

static void rpc_send_error(Rpc* rpc, uint32_t request_id, adapter_ErrType err_type) {
    rpc->response = (adapter_RpcResponse)adapter_RpcResponse_init_zero;
    rpc->response.request_id = request_id;
    rpc->response.which_payload = adapter_RpcResponse_generic_tag;
    rpc->response.payload.generic.err_type = err_type;
    rpc_send_response(rpc);
}

static void rpc_handle_version(Rpc* rpc) {
    adapter_VersionResponse* version = &rpc->response.payload.version;

    rpc->response.which_payload = adapter_RpcResponse_version_tag;
    strlcpy(version->version, version_get_version(NULL), sizeof(version->version));
    strlcpy(version->git_commit, version_get_githash(NULL), sizeof(version->git_commit));
    strlcpy(version->git_branch, version_get_gitbranch(NULL), sizeof(version->git_branch));
    strlcpy(version->build_date, version_get_builddate(NULL), sizeof(version->build_date));
    version->dirty = version_get_dirty_flag(NULL);
    version->target = version_get_target(NULL);
}

static void rpc_handle_request(Rpc* rpc) {
    switch(rpc->request.which_payload) {
    case adapter_RpcRequest_version_tag:
        rpc->response = (adapter_RpcResponse)adapter_RpcResponse_init_zero;
        rpc->response.request_id = rpc->request.request_id;
        rpc_handle_version(rpc);
        rpc_send_response(rpc);
        break;
    default:
        rpc_send_error(rpc, rpc->request.request_id, adapter_ErrType_ERROR_TYPE_BAD_REQUEST);
        break;
    }
}

static bool rpc_get_rx_frame_size(const Rpc* rpc, size_t* frame_size) {
    pb_istream_t stream = pb_istream_from_buffer(rpc->rx_frame, rpc->rx_frame_size);
    uint32_t message_size;
    if(!pb_decode_varint32(&stream, &message_size)) {
        return false;
    }
    *frame_size = rpc->rx_frame_size - stream.bytes_left + message_size;
    return true;
}

static bool rpc_process_rx_frame(Rpc* rpc) {
    size_t frame_size;
    if(!rpc_get_rx_frame_size(rpc, &frame_size)) {
        if(rpc->rx_frame_size >= RPC_VARINT32_SIZE_MAX) {
            FURI_LOG_E(TAG, "Frame length prefix invalid");
            rpc_drop_rx_frame(rpc);
            rpc_send_error(rpc, RPC_REQUEST_ID_UNKNOWN, adapter_ErrType_ERROR_TYPE_BAD_REQUEST);
        }
        return false;
    }

    if(frame_size > RPC_FRAME_SIZE_MAX) {
        FURI_LOG_E(TAG, "Frame size %u exceeds %u", frame_size, RPC_FRAME_SIZE_MAX);
        rpc_drop_rx_frame(rpc);
        rpc_send_error(rpc, RPC_REQUEST_ID_UNKNOWN, adapter_ErrType_ERROR_TYPE_BAD_REQUEST);
        return false;
    }

    if(rpc->rx_frame_size < frame_size) {
        return false;
    }

    rpc->request = (adapter_RpcRequest)adapter_RpcRequest_init_zero;
    pb_istream_t stream = pb_istream_from_buffer(rpc->rx_frame, frame_size);
    if(pb_decode_ex(&stream, adapter_RpcRequest_fields, &rpc->request, PB_DECODE_DELIMITED)) {
        rpc_handle_request(rpc);
    } else {
        FURI_LOG_E(TAG, "Request decode failed: %s", PB_GET_ERROR(&stream));
        rpc_send_error(rpc, RPC_REQUEST_ID_UNKNOWN, adapter_ErrType_ERROR_TYPE_BAD_REQUEST);
    }

    rpc->rx_frame_size -= frame_size;
    memmove(rpc->rx_frame, &rpc->rx_frame[frame_size], rpc->rx_frame_size);
    return true;
}

static void rpc_maybe_receive_data(Rpc* rpc) {
    const size_t length = furi_hal_cdc_receive(&rpc->rx_frame[rpc->rx_frame_size], CDC_DATA_SZ);
    if(length == 0) return;
    rpc->rx_frame_size += length;

    while(rpc_process_rx_frame(rpc)) {
    }

    if(rpc->rx_frame_size > 0) {
        furi_event_loop_timer_start(rpc->frame_timer, furi_ms_to_ticks(RPC_FRAME_TIMEOUT_MS));
    } else {
        furi_event_loop_timer_stop(rpc->frame_timer);
    }
}

static void rpc_signal_internal_event(Rpc* rpc, RpcInternalEvent event) {
    furi_check(furi_message_queue_put(rpc->internal_evt_queue, &event, 0) == FuriStatusOk);
}

static void rpc_cdc_tx_done(void* context) {
    Rpc* rpc = context;
    rpc->is_currently_transmitting = false;
    rpc_signal_internal_event(rpc, RpcInternalEventTxDone);
}

static void rpc_cdc_rx(void* context) {
    Rpc* rpc = context;
    rpc_signal_internal_event(rpc, RpcInternalEventRx);
}

static void rpc_cdc_state_callback(void* context, CdcState state) {
    Rpc* rpc = context;
    if(state == CdcStateConnected) {
        rpc_signal_internal_event(rpc, RpcInternalEventConnected);
    } else {
        rpc_signal_internal_event(rpc, RpcInternalEventDisconnected);
    }
}

static CdcCallbacks cdc_callbacks = {
    .tx_ep_callback = rpc_cdc_tx_done,
    .rx_ep_callback = rpc_cdc_rx,
    .state_callback = rpc_cdc_state_callback,
};

static void rpc_internal_event_happened(FuriEventLoopObject* object, void* context) {
    Rpc* rpc = context;
    RpcInternalEvent event;
    furi_check(furi_message_queue_get(object, &event, 0) == FuriStatusOk);

    switch(event) {
    case RpcInternalEventRx:
        rpc_maybe_receive_data(rpc);
        break;

    case RpcInternalEventTxDone:
        rpc_maybe_send_data(rpc);
        break;

    case RpcInternalEventDisconnected:
        if(!rpc->is_connected) return;
        FURI_LOG_D(TAG, "Disconnected");
        rpc->is_connected = false;
        rpc->is_currently_transmitting = false;
        furi_stream_buffer_reset(rpc->tx_stream);
        rpc_drop_rx_frame(rpc);
        break;

    case RpcInternalEventConnected:
        if(rpc->is_connected) return;
        FURI_LOG_D(TAG, "Connected");
        rpc->is_connected = true;
        break;
    }
}

static void rpc_init(Rpc* rpc) {
    furi_event_loop_subscribe_message_queue(
        rpc->event_loop,
        rpc->internal_evt_queue,
        FuriEventLoopEventIn,
        rpc_internal_event_happened,
        rpc);
    furi_hal_cdc_set_callbacks(&cdc_callbacks, rpc);
}

static void rpc_deinit(Rpc* rpc) {
    furi_hal_cdc_set_callbacks(NULL, NULL);
    furi_event_loop_unsubscribe(rpc->event_loop, rpc->internal_evt_queue);
    furi_event_loop_timer_stop(rpc->frame_timer);
}

int32_t rpc_srv(void* context) {
    UNUSED(context);

    Rpc* rpc = rpc_alloc();
    rpc_init(rpc);

    furi_event_loop_run(rpc->event_loop);

    rpc_deinit(rpc);
    rpc_free(rpc);

    return 0;
}
