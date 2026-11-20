#pragma once

#include <applications/services/settings/settings.h>

#include <furi.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RECORD_SETTINGS_STORE "settings_store"

typedef struct SettingsStore SettingsStore;

typedef enum {
    SettingsStoreSaveResultOk,
    SettingsStoreSaveResultEncodeFailed,
    SettingsStoreSaveResultEraseFailed,
    SettingsStoreSaveResultWriteFailed,
    SettingsStoreSaveResultVerifyFailed,
} SettingsStoreSaveResult;

void settings_store_get(SettingsStore* store, Settings* settings);

void settings_store_set(SettingsStore* store, const Settings* settings);

SettingsStoreSaveResult settings_store_save(SettingsStore* store);

FuriPubSub* settings_store_get_pubsub(SettingsStore* store);

int32_t settings_store_srv(void* context);

#ifdef __cplusplus
}
#endif
