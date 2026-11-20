#include <applications/services/settings/settings.h>

#include <furi.h>

#define SETTINGS_SPEAKER_VOLUME_OFFSET_DEFAULT 0

void settings_reset(Settings* settings) {
    furi_check(settings);

    settings->speaker_volume_offset = SETTINGS_SPEAKER_VOLUME_OFFSET_DEFAULT;
}

void settings_to_proto(const Settings* settings, adapter_Settings* message) {
    furi_check(settings);
    furi_check(message);

    *message = (adapter_Settings)adapter_Settings_init_zero;
    message->has_speaker_volume_offset = true;
    message->speaker_volume_offset = settings->speaker_volume_offset;
}

void settings_apply_proto(Settings* settings, const adapter_Settings* message) {
    furi_check(settings);
    furi_check(message);

    if(message->has_speaker_volume_offset) {
        settings->speaker_volume_offset = (int8_t)CLAMP(
            message->speaker_volume_offset,
            SETTINGS_SPEAKER_VOLUME_OFFSET_MAX_STEPS,
            -SETTINGS_SPEAKER_VOLUME_OFFSET_MAX_STEPS);
    }
}
