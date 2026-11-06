#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    HidConsumerScanNextTrack = 0x00B5,
    HidConsumerScanPreviousTrack = 0x00B6,
    HidConsumerPlayPause = 0x00CD,
    HidConsumerVoiceCommand = 0x00CF,
    HidConsumerVolumeIncrement = 0x00E9,
    HidConsumerVolumeDecrement = 0x00EA,
} HidConsumerKey;

bool furi_hal_hid_is_connected(void);

bool furi_hal_hid_consumer_key_press(HidConsumerKey key);

bool furi_hal_hid_consumer_key_release(void);

#ifdef __cplusplus
}
#endif
