#include <applications/services/settings/settings.h>

#include <furi.h>

#define SETTINGS_SPEAKER_VOLUME_OFFSET_DEFAULT 0
#define SETTINGS_MIC_GAIN_DEFAULT              6

void settings_reset(Settings* settings) {
    furi_check(settings);

    settings->speaker_volume_offset = SETTINGS_SPEAKER_VOLUME_OFFSET_DEFAULT;
    settings->mic_gain = SETTINGS_MIC_GAIN_DEFAULT;
}

void settings_to_proto(const Settings* settings, adapter_Settings* message) {
    furi_check(settings);
    furi_check(message);

    *message = (adapter_Settings)adapter_Settings_init_zero;
    message->has_speaker_volume_offset = true;
    message->speaker_volume_offset = settings->speaker_volume_offset;
    message->has_mic_gain = true;
    message->mic_gain = settings->mic_gain;
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
    if(message->has_mic_gain) {
        settings->mic_gain = (uint8_t)MIN(message->mic_gain, SETTINGS_MIC_GAIN_MAX_STEPS);
    }
}
