#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RECORD_INPUT_EVENTS "input_events"

typedef enum {
    InputKeyPlayPause,
    InputKeyVolumeUp,
    InputKeyVolumeDown,
    InputKeyAssistant,
    InputKeyMAX,
} InputKey;

typedef enum {
    InputTypePress,
    InputTypeRelease,
    InputTypeShort,
    InputTypeLong,
    InputTypeRepeat,
    InputTypeMAX,
} InputType;

typedef struct {
    InputKey key;
    InputType type;
} InputEvent;

int32_t input_srv(void* context);

#ifdef __cplusplus
}
#endif
