#include <applications/services/rpc/rpc_update.h>
#include <applications/services/audio/audio.h>

#include <furi.h>
#include <furi_hal_crc.h>
#include <furi_hal_flash.h>
#include <toolbox/firmware_header.h>

#include <string.h>

#define TAG "RpcUpdate"

#define RPC_UPDATE_ERASED_BYTE 0xFF

adapter_ErrType rpc_update_begin(RpcUpdate* update, const adapter_UpdateBeginRequest* request) {
    if(update->state == RpcUpdateStateVerified) {
        FURI_LOG_E(TAG, "Begin failed, verified image waits for bank swap");
        return adapter_ErrType_ERROR_TYPE_BAD_STATE;
    }
    rpc_update_abort(update);

    if(!furi_hal_flash_is_dual_bank()) {
        FURI_LOG_E(TAG, "Begin failed, flash is single bank");
        return adapter_ErrType_ERROR_TYPE_BAD_STATE;
    }

    const size_t bank_size = furi_hal_flash_get_bank_size();
    if(request->size < firmware_header_get_offset() + sizeof(FirmwareHeader) ||
       request->size > bank_size) {
        FURI_LOG_E(TAG, "Begin failed, image size %lu, bank size %u", request->size, bank_size);
        return adapter_ErrType_ERROR_TYPE_BAD_REQUEST;
    }

    update->state = RpcUpdateStateWriting;
    update->image_size = request->size;
    update->image_crc32 = request->crc32;
    update->written = 0;
    FURI_LOG_I(TAG, "Begin, image size %lu, crc32 0x%08lX", request->size, request->crc32);
    return adapter_ErrType_ERROR_TYPE_OK;
}

static bool rpc_update_erase_pages(size_t offset, size_t size) {
    const size_t first_page = (offset + FURI_HAL_FLASH_PAGE_SIZE - 1) / FURI_HAL_FLASH_PAGE_SIZE;
    for(size_t page = first_page; page * FURI_HAL_FLASH_PAGE_SIZE < offset + size; page++) {
        if(!furi_hal_flash_erase_inactive_page(page)) {
            return false;
        }
    }
    return true;
}

adapter_ErrType rpc_update_write(RpcUpdate* update, adapter_UpdateWriteRequest* request) {
    if(update->state != RpcUpdateStateWriting) {
        FURI_LOG_E(TAG, "Write at %lu failed, update state %d", request->offset, update->state);
        return adapter_ErrType_ERROR_TYPE_BAD_STATE;
    }

    const size_t size = request->data.size;
    const bool is_last_chunk = update->written + size == update->image_size;
    if(request->offset != update->written || size == 0 ||
       update->written + size > update->image_size ||
       (!is_last_chunk && size % FURI_HAL_FLASH_PROGRAM_UNIT != 0)) {
        FURI_LOG_E(
            TAG,
            "Write failed, offset %lu, size %u, expected offset %lu, image size %lu",
            request->offset,
            size,
            update->written,
            update->image_size);
        rpc_update_abort(update);
        return adapter_ErrType_ERROR_TYPE_BAD_REQUEST;
    }

    const size_t program_size = (size + FURI_HAL_FLASH_PROGRAM_UNIT - 1) /
                                FURI_HAL_FLASH_PROGRAM_UNIT * FURI_HAL_FLASH_PROGRAM_UNIT;
    furi_check(program_size <= sizeof(request->data.bytes));
    memset(&request->data.bytes[size], RPC_UPDATE_ERASED_BYTE, program_size - size);

    if(!rpc_update_erase_pages(request->offset, program_size) ||
       !furi_hal_flash_program_inactive(request->offset, request->data.bytes, program_size)) {
        rpc_update_abort(update);
        return adapter_ErrType_ERROR_TYPE_FLASH;
    }

    update->written += size;
    return adapter_ErrType_ERROR_TYPE_OK;
}

static bool rpc_update_is_header_valid(const RpcUpdate* update) {
    FirmwareHeader header;
    memcpy(
        &header,
        furi_hal_flash_get_inactive_bank() + firmware_header_get_offset(),
        sizeof(FirmwareHeader));

    if(header.magic != FIRMWARE_HEADER_MAGIC || header.target != TARGET ||
       header.image_size != update->image_size) {
        FURI_LOG_E(
            TAG,
            "Image header invalid, magic 0x%08lX, target %lu, image size %lu of %lu",
            header.magic,
            header.target,
            header.image_size,
            update->image_size);
        return false;
    }
    return true;
}

adapter_ErrType rpc_update_finish(RpcUpdate* update) {
    if(update->state != RpcUpdateStateWriting || update->written != update->image_size) {
        FURI_LOG_E(
            TAG,
            "Finish failed, update state %d, written %lu of %lu",
            update->state,
            update->written,
            update->image_size);
        rpc_update_abort(update);
        return adapter_ErrType_ERROR_TYPE_BAD_STATE;
    }

    if(!rpc_update_is_header_valid(update)) {
        rpc_update_abort(update);
        return adapter_ErrType_ERROR_TYPE_IMAGE_HEADER;
    }

    const uint32_t crc32 = furi_hal_crc32(furi_hal_flash_get_inactive_bank(), update->image_size);
    if(crc32 != update->image_crc32) {
        FURI_LOG_E(TAG, "Image crc32 0x%08lX, expected 0x%08lX", crc32, update->image_crc32);
        rpc_update_abort(update);
        return adapter_ErrType_ERROR_TYPE_IMAGE_CRC;
    }

    update->state = RpcUpdateStateVerified;
    FURI_LOG_I(TAG, "Image verified, crc32 0x%08lX", crc32);
    return adapter_ErrType_ERROR_TYPE_OK;
}

void rpc_update_abort(RpcUpdate* update) {
    if(update->state == RpcUpdateStateVerified) {
        return;
    }
    update->state = RpcUpdateStateIdle;
    update->image_size = 0;
    update->image_crc32 = 0;
    update->written = 0;
}

FURI_NORETURN void rpc_update_apply(const RpcUpdate* update) {
    furi_check(update->state == RpcUpdateStateVerified);

    Audio* audio = furi_record_open(RECORD_AUDIO);
    audio_shutdown(audio);
    furi_record_close(RECORD_AUDIO);

    FURI_LOG_I(TAG, "Swapping flash banks");
    furi_hal_flash_swap_banks();
}
