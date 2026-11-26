#include <applications/services/rpc/rpc_i.h>
#include <applications/services/audio/audio.h>

#include <furi.h>
#include <furi_hal.h>
#include <toolbox/version.h>

#include <string.h>

#define TAG "RpcSystem"

#define RPC_SYSTEM_REBOOT_DELAY_MS         100
#define RPC_SYSTEM_MICROSECONDS_PER_SECOND 1000000

struct RpcSystem {
    Rpc* rpc;
    FuriEventLoopTimer* reboot_timer;
    FuriHalBootMode reboot_mode;
};

static const adapter_ResetReason rpc_system_reset_reasons[] = {
    [FuriHalPowerResetReasonUnknown] = adapter_ResetReason_RESET_REASON_UNKNOWN,
    [FuriHalPowerResetReasonPowerOn] = adapter_ResetReason_RESET_REASON_POWER_ON,
    [FuriHalPowerResetReasonPin] = adapter_ResetReason_RESET_REASON_PIN,
    [FuriHalPowerResetReasonSoftware] = adapter_ResetReason_RESET_REASON_SOFTWARE,
    [FuriHalPowerResetReasonIndependentWatchdog] =
        adapter_ResetReason_RESET_REASON_INDEPENDENT_WATCHDOG,
    [FuriHalPowerResetReasonWindowWatchdog] = adapter_ResetReason_RESET_REASON_WINDOW_WATCHDOG,
    [FuriHalPowerResetReasonLowPower] = adapter_ResetReason_RESET_REASON_LOW_POWER,
    [FuriHalPowerResetReasonOptionBytesLoad] = adapter_ResetReason_RESET_REASON_OPTION_BYTES_LOAD,
};

static void rpc_system_reboot_timer_callback(void* context) {
    RpcSystem* rpc_system = context;

    Audio* audio = furi_record_open(RECORD_AUDIO);
    audio_shutdown(audio);
    furi_record_close(RECORD_AUDIO);

    FURI_LOG_I(
        TAG,
        "Rebooting to %s",
        (rpc_system->reboot_mode == FuriHalBootModeDfu) ? "bootloader" : "firmware");
    furi_hal_boot_set_mode(rpc_system->reboot_mode);
    furi_hal_power_reset();
}

static void rpc_system_handle_version(const adapter_RpcRequest* request, void* context) {
    RpcSystem* rpc_system = context;

    adapter_RpcResponse* response =
        rpc_begin_response(rpc_system->rpc, request->request_id, adapter_RpcResponse_version_tag);
    adapter_VersionResponse* version = &response->payload.version;

    strlcpy(version->version, version_get_version(NULL), sizeof(version->version));
    strlcpy(version->git_commit, version_get_githash(NULL), sizeof(version->git_commit));
    strlcpy(version->git_branch, version_get_gitbranch(NULL), sizeof(version->git_branch));
    strlcpy(version->build_date, version_get_builddate(NULL), sizeof(version->build_date));
    version->dirty = version_get_dirty_flag(NULL);
    version->target = version_get_target(NULL);

    rpc_send_response(rpc_system->rpc);
}

static void rpc_system_handle_reboot(const adapter_RpcRequest* request, void* context) {
    RpcSystem* rpc_system = context;

    const adapter_RebootMode mode = request->payload.reboot.mode;
    if((mode != adapter_RebootMode_REBOOT_MODE_NORMAL) &&
       (mode != adapter_RebootMode_REBOOT_MODE_BOOTLOADER)) {
        FURI_LOG_E(TAG, "Reboot mode %d", mode);
        rpc_send_status(
            rpc_system->rpc, request->request_id, adapter_ErrType_ERROR_TYPE_BAD_REQUEST);
        return;
    }

    rpc_system->reboot_mode = (mode == adapter_RebootMode_REBOOT_MODE_BOOTLOADER) ?
                                  FuriHalBootModeDfu :
                                  FuriHalBootModeNormal;
    rpc_send_status(rpc_system->rpc, request->request_id, adapter_ErrType_ERROR_TYPE_OK);
    furi_event_loop_timer_start(
        rpc_system->reboot_timer, furi_ms_to_ticks(RPC_SYSTEM_REBOOT_DELAY_MS));
}

static void rpc_system_handle_system_status(const adapter_RpcRequest* request, void* context) {
    RpcSystem* rpc_system = context;

    adapter_RpcResponse* response = rpc_begin_response(
        rpc_system->rpc, request->request_id, adapter_RpcResponse_system_status_tag);
    adapter_SystemStatusResponse* system_status = &response->payload.system_status;

    system_status->tick = furi_get_tick();
    system_status->tick_frequency_hz = furi_kernel_get_tick_frequency();
    system_status->cycle_counter = furi_hal_cortex_get_cycle_counter();
    system_status->cycle_frequency_hz =
        furi_hal_cortex_instructions_per_microsecond() * RPC_SYSTEM_MICROSECONDS_PER_SECOND;
    system_status->isr_cycles = furi_hal_interrupt_get_time_in_isr_total();
    system_status->heap_total = memmgr_get_total_heap();
    system_status->heap_free = memmgr_get_free_heap();
    system_status->heap_min_free = memmgr_get_minimum_free_heap();
    system_status->reset_reason = rpc_system_reset_reasons[furi_hal_power_get_reset_reason()];

    rpc_send_response(rpc_system->rpc);
}

RpcSystem* rpc_system_alloc(Rpc* rpc) {
    furi_check(rpc);

    RpcSystem* rpc_system = malloc(sizeof(RpcSystem));
    rpc_system->rpc = rpc;
    rpc_system->reboot_timer = furi_event_loop_timer_alloc(
        rpc_get_event_loop(rpc),
        rpc_system_reboot_timer_callback,
        FuriEventLoopTimerTypeOnce,
        rpc_system);
    rpc_system->reboot_mode = FuriHalBootModeNormal;

    rpc_add_handler(rpc, adapter_RpcRequest_version_tag, rpc_system_handle_version, rpc_system);
    rpc_add_handler(rpc, adapter_RpcRequest_reboot_tag, rpc_system_handle_reboot, rpc_system);
    rpc_add_handler(
        rpc, adapter_RpcRequest_system_status_tag, rpc_system_handle_system_status, rpc_system);

    return rpc_system;
}

void rpc_system_free(RpcSystem* rpc_system) {
    furi_check(rpc_system);

    furi_event_loop_timer_stop(rpc_system->reboot_timer);
    furi_event_loop_timer_free(rpc_system->reboot_timer);
    free(rpc_system);
}
