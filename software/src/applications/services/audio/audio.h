#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RECORD_AUDIO          "audio"
#define RECORD_HEADSET_EVENTS "headset_events"

typedef enum {
    HeadsetEventPlugged,
    HeadsetEventUnplugged,
} HeadsetEvent;

int32_t audio_srv(void* context);

#ifdef __cplusplus
}
#endif
