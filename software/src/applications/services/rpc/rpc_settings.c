#include <applications/services/rpc/rpc_i.h>
#include <applications/services/audio/audio.h>
#include <applications/services/settings/settings_store.h>

#include <furi.h>

struct RpcSettings {
    Rpc* rpc;
    SettingsStore* store;
    Audio* audio;
};

static const adapter_SettingsSaveResult rpc_settings_save_results[] = {
    [SettingsStoreSaveResultOk] = adapter_SettingsSaveResult_SETTINGS_SAVE_RESULT_OK,
    [SettingsStoreSaveResultEncodeFailed] =
        adapter_SettingsSaveResult_SETTINGS_SAVE_RESULT_ENCODE_FAILED,
    [SettingsStoreSaveResultEraseFailed] =
        adapter_SettingsSaveResult_SETTINGS_SAVE_RESULT_ERASE_FAILED,
    [SettingsStoreSaveResultWriteFailed] =
        adapter_SettingsSaveResult_SETTINGS_SAVE_RESULT_WRITE_FAILED,
    [SettingsStoreSaveResultVerifyFailed] =
        adapter_SettingsSaveResult_SETTINGS_SAVE_RESULT_VERIFY_FAILED,
};

static void rpc_settings_handle_settings_get(const adapter_RpcRequest* request, void* context) {
    RpcSettings* rpc_settings = context;

    Settings settings;
    settings_store_get(rpc_settings->store, &settings);

    adapter_RpcResponse* response = rpc_begin_response(
        rpc_settings->rpc, request->request_id, adapter_RpcResponse_settings_get_tag);
    adapter_SettingsGetResponse* settings_get = &response->payload.settings_get;
    settings_get->has_settings = true;
    settings_to_proto(&settings, &settings_get->settings);

    rpc_send_response(rpc_settings->rpc);
}

static void rpc_settings_handle_settings_set(const adapter_RpcRequest* request, void* context) {
    RpcSettings* rpc_settings = context;
    const adapter_SettingsSetRequest* settings_set_request = &request->payload.settings_set;

    Settings settings;
    settings_store_get(rpc_settings->store, &settings);
    if(settings_set_request->has_settings) {
        settings_apply_proto(&settings, &settings_set_request->settings);
        settings_store_set(rpc_settings->store, &settings);
    }

    adapter_RpcResponse* response = rpc_begin_response(
        rpc_settings->rpc, request->request_id, adapter_RpcResponse_settings_set_tag);
    adapter_SettingsSetResponse* settings_set_response = &response->payload.settings_set;
    settings_set_response->has_settings = true;
    settings_to_proto(&settings, &settings_set_response->settings);

    rpc_send_response(rpc_settings->rpc);
}

static void rpc_settings_handle_settings_save(const adapter_RpcRequest* request, void* context) {
    RpcSettings* rpc_settings = context;

    audio_pause(rpc_settings->audio);
    const SettingsStoreSaveResult result = settings_store_save(rpc_settings->store);
    audio_resume(rpc_settings->audio);

    adapter_RpcResponse* response = rpc_begin_response(
        rpc_settings->rpc, request->request_id, adapter_RpcResponse_settings_save_tag);
    response->payload.settings_save.result = rpc_settings_save_results[result];

    rpc_send_response(rpc_settings->rpc);
}

RpcSettings* rpc_settings_alloc(Rpc* rpc) {
    furi_check(rpc);

    RpcSettings* rpc_settings = malloc(sizeof(RpcSettings));
    rpc_settings->rpc = rpc;
    rpc_settings->store = furi_record_open(RECORD_SETTINGS_STORE);
    rpc_settings->audio = furi_record_open(RECORD_AUDIO);

    rpc_add_handler(
        rpc, adapter_RpcRequest_settings_get_tag, rpc_settings_handle_settings_get, rpc_settings);
    rpc_add_handler(
        rpc, adapter_RpcRequest_settings_set_tag, rpc_settings_handle_settings_set, rpc_settings);
    rpc_add_handler(
        rpc, adapter_RpcRequest_settings_save_tag, rpc_settings_handle_settings_save, rpc_settings);

    return rpc_settings;
}

void rpc_settings_free(RpcSettings* rpc_settings) {
    furi_check(rpc_settings);

    furi_record_close(RECORD_AUDIO);
    furi_record_close(RECORD_SETTINGS_STORE);
    free(rpc_settings);
}
