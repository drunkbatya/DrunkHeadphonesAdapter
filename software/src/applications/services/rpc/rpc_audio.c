#include <applications/services/rpc/rpc_i.h>
#include <applications/services/audio/audio.h>

#include <furi.h>

struct RpcAudio {
    Rpc* rpc;
    Audio* audio;
    AudioStatus status;
};

static void rpc_audio_fill_stream(
    adapter_AudioStreamStatus* stream_status,
    const AudioStreamStatus* audio_stream_status) {
    stream_status->active = audio_stream_status->active;
    stream_status->sample_rate = audio_stream_status->sample_rate;
    stream_status->sample_bits = audio_stream_status->sample_bits;
    stream_status->volume_db_x256 = audio_stream_status->volume;
    stream_status->muted = audio_stream_status->muted;
    stream_status->i2s_error_count = audio_stream_status->i2s_error_count;
}

static void rpc_audio_handle_audio_status(const adapter_RpcRequest* request, void* context) {
    RpcAudio* rpc_audio = context;

    AudioStatus* status = &rpc_audio->status;
    audio_get_status(rpc_audio->audio, status);

    adapter_RpcResponse* response = rpc_begin_response(
        rpc_audio->rpc, request->request_id, adapter_RpcResponse_audio_status_tag);
    adapter_AudioStatusResponse* audio_status = &response->payload.audio_status;

    audio_status->headphone_plugged = status->headphone_plugged;
    audio_status->headphone_output_powered = status->headphone_output_powered;
    audio_status->has_speaker = true;
    rpc_audio_fill_stream(&audio_status->speaker, &status->streams[FuriHalUsbAudioStreamSpeaker]);
    audio_status->has_mic = true;
    rpc_audio_fill_stream(&audio_status->mic, &status->streams[FuriHalUsbAudioStreamMic]);

    rpc_send_response(rpc_audio->rpc);
}

RpcAudio* rpc_audio_alloc(Rpc* rpc) {
    furi_check(rpc);

    RpcAudio* rpc_audio = malloc(sizeof(RpcAudio));
    rpc_audio->rpc = rpc;
    rpc_audio->audio = furi_record_open(RECORD_AUDIO);

    rpc_add_handler(
        rpc, adapter_RpcRequest_audio_status_tag, rpc_audio_handle_audio_status, rpc_audio);

    return rpc_audio;
}

void rpc_audio_free(RpcAudio* rpc_audio) {
    furi_check(rpc_audio);

    furi_record_close(RECORD_AUDIO);
    free(rpc_audio);
}
