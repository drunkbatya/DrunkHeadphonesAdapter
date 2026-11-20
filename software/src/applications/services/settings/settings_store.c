#include <applications/services/settings/settings_store.h>

#include <furi.h>
#include <furi_hal_flash.h>
#include <toolbox/crc32.h>

#include <pb_decode.h>
#include <pb_encode.h>

#include <string.h>

#define TAG "SettingsStore"

#define SETTINGS_STORE_RECORD_MAGIC 0x53455454U

typedef struct {
    uint32_t magic;
    uint32_t payload_size;
    uint32_t crc;
} SettingsStoreRecordHeader;

#define SETTINGS_STORE_RECORD_DWORD_COUNT                                                  \
    ((sizeof(SettingsStoreRecordHeader) + adapter_Settings_size + sizeof(uint64_t) - 1U) / \
     sizeof(uint64_t))

typedef struct {
    uint64_t dwords[SETTINGS_STORE_RECORD_DWORD_COUNT];
    size_t size;
} SettingsStoreRecord;

struct SettingsStore {
    FuriEventLoop* event_loop;
    FuriMutex* mutex;
    FuriPubSub* pubsub;
    Settings settings;
};

static size_t settings_store_get_record_address(void) {
    return furi_hal_flash_get_storage_start_address();
}

static bool
    settings_store_decode(const uint8_t* payload, size_t payload_size, Settings* settings) {
    adapter_Settings message = adapter_Settings_init_zero;
    pb_istream_t stream = pb_istream_from_buffer(payload, payload_size);
    if(!pb_decode(&stream, adapter_Settings_fields, &message)) {
        FURI_LOG_E(TAG, "Decode of %u bytes failed: %s", payload_size, PB_GET_ERROR(&stream));
        return false;
    }
    settings_reset(settings);
    settings_apply_proto(settings, &message);
    return true;
}

static bool settings_store_build_record(const Settings* settings, SettingsStoreRecord* record) {
    uint8_t* record_bytes = (uint8_t*)record->dwords;
    uint8_t* payload = record_bytes + sizeof(SettingsStoreRecordHeader);

    memset(record->dwords, 0, sizeof(record->dwords));

    adapter_Settings message;
    settings_to_proto(settings, &message);
    pb_ostream_t stream = pb_ostream_from_buffer(payload, adapter_Settings_size);
    if(!pb_encode(&stream, adapter_Settings_fields, &message)) {
        FURI_LOG_E(TAG, "Encode failed: %s", PB_GET_ERROR(&stream));
        return false;
    }

    const SettingsStoreRecordHeader header = {
        .magic = SETTINGS_STORE_RECORD_MAGIC,
        .payload_size = stream.bytes_written,
        .crc = crc32_update(CRC32_INITIAL, payload, stream.bytes_written),
    };
    memcpy(record_bytes, &header, sizeof(header));

    const size_t record_size = sizeof(header) + stream.bytes_written;
    record->size = (record_size + sizeof(uint64_t) - 1U) / sizeof(uint64_t) * sizeof(uint64_t);
    return true;
}

static void settings_store_load(SettingsStore* store) {
    const size_t address = settings_store_get_record_address();
    SettingsStoreRecordHeader header;
    uint8_t payload[adapter_Settings_size];

    settings_reset(&store->settings);

    bool success = false;
    do {
        if(!furi_hal_flash_read(address, &header, sizeof(header))) break;
        if(header.magic != SETTINGS_STORE_RECORD_MAGIC) {
            FURI_LOG_I(TAG, "No record at 0x%08X, magic 0x%08lX", address, header.magic);
            break;
        }
        if(header.payload_size > sizeof(payload)) {
            FURI_LOG_E(TAG, "Payload size %lu exceeds %u", header.payload_size, sizeof(payload));
            break;
        }
        if(!furi_hal_flash_read(address + sizeof(header), payload, header.payload_size)) break;
        const uint32_t crc = crc32_update(CRC32_INITIAL, payload, header.payload_size);
        if(crc != header.crc) {
            FURI_LOG_E(TAG, "CRC 0x%08lX, expected 0x%08lX", crc, header.crc);
            break;
        }
        if(!settings_store_decode(payload, header.payload_size, &store->settings)) break;
        success = true;
    } while(0);

    if(success) {
        FURI_LOG_I(TAG, "Loaded %lu bytes", header.payload_size);
    } else {
        settings_reset(&store->settings);
        FURI_LOG_I(TAG, "Defaults");
    }
}

static bool settings_store_is_record_stored(const SettingsStoreRecord* record) {
    SettingsStoreRecord stored_record;
    if(!furi_hal_flash_read(
           settings_store_get_record_address(), stored_record.dwords, record->size)) {
        return false;
    }
    return memcmp(stored_record.dwords, record->dwords, record->size) == 0;
}

static bool settings_store_erase_record(void) {
    const int16_t page = furi_hal_flash_get_page_number(settings_store_get_record_address());
    furi_check(page >= 0);
    return furi_hal_flash_erase((uint8_t)page);
}

static bool settings_store_write_record(const SettingsStoreRecord* record) {
    const size_t address = settings_store_get_record_address();
    for(size_t i = 0; i < record->size / sizeof(uint64_t); i++) {
        if(!furi_hal_flash_write_dword(address + i * sizeof(uint64_t), record->dwords[i])) {
            return false;
        }
    }
    return true;
}

static SettingsStore* settings_store_alloc(void) {
    furi_check(furi_hal_flash_get_write_block_size() == sizeof(uint64_t));

    SettingsStore* store = malloc(sizeof(SettingsStore));
    store->event_loop = furi_event_loop_alloc();
    store->mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    store->pubsub = furi_pubsub_alloc();
    return store;
}

static void settings_store_free(SettingsStore* store) {
    furi_pubsub_free(store->pubsub);
    furi_mutex_free(store->mutex);
    furi_event_loop_free(store->event_loop);
    free(store);
}

void settings_store_get(SettingsStore* store, Settings* settings) {
    furi_check(store);
    furi_check(settings);

    furi_check(furi_mutex_acquire(store->mutex, FuriWaitForever) == FuriStatusOk);
    *settings = store->settings;
    furi_mutex_release(store->mutex);
}

void settings_store_set(SettingsStore* store, const Settings* settings) {
    furi_check(store);
    furi_check(settings);

    Settings new_settings = *settings;

    furi_check(furi_mutex_acquire(store->mutex, FuriWaitForever) == FuriStatusOk);
    const bool changed = memcmp(&store->settings, &new_settings, sizeof(Settings)) != 0;
    if(changed) {
        store->settings = new_settings;
    }
    furi_mutex_release(store->mutex);

    if(changed) {
        furi_pubsub_publish(store->pubsub, &new_settings);
    }
}

SettingsStoreSaveResult settings_store_save(SettingsStore* store) {
    furi_check(store);

    Settings settings;
    settings_store_get(store, &settings);

    SettingsStoreRecord record;
    SettingsStoreSaveResult result = SettingsStoreSaveResultOk;
    do {
        if(!settings_store_build_record(&settings, &record)) {
            result = SettingsStoreSaveResultEncodeFailed;
            break;
        }
        if(settings_store_is_record_stored(&record)) {
            FURI_LOG_I(TAG, "Stored record matches, %u bytes", record.size);
            break;
        }
        if(!settings_store_erase_record()) {
            result = SettingsStoreSaveResultEraseFailed;
            break;
        }
        if(!settings_store_write_record(&record)) {
            result = SettingsStoreSaveResultWriteFailed;
            break;
        }
        if(!settings_store_is_record_stored(&record)) {
            FURI_LOG_E(TAG, "Read back of %u bytes differs", record.size);
            result = SettingsStoreSaveResultVerifyFailed;
            break;
        }
        FURI_LOG_I(TAG, "Saved %u bytes", record.size);
    } while(0);

    return result;
}

FuriPubSub* settings_store_get_pubsub(SettingsStore* store) {
    furi_check(store);

    return store->pubsub;
}

int32_t settings_store_srv(void* context) {
    UNUSED(context);

    SettingsStore* store = settings_store_alloc();
    settings_store_load(store);
    furi_record_create(RECORD_SETTINGS_STORE, store);

    furi_event_loop_run(store->event_loop);

    furi_record_destroy(RECORD_SETTINGS_STORE);
    settings_store_free(store);

    return 0;
}
