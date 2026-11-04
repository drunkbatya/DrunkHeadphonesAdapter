#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    FuriHalI2sIdHeadphones,
    FuriHalI2sIdMic,

    FuriHalI2sIdMax,
} FuriHalI2sId;

typedef enum {
    FuriHalI2sDataFormat16Bit,
    FuriHalI2sDataFormat16BitIn32BitFrame,
    FuriHalI2sDataFormat24BitIn32BitFrame,
    FuriHalI2sDataFormat32Bit,
} FuriHalI2sDataFormat;

typedef enum {
    FuriHalI2sEventHalfTransfer,
    FuriHalI2sEventTransferComplete,
    FuriHalI2sEventUnderrun,
    FuriHalI2sEventOverrun,
    FuriHalI2sEventFrameError,
    FuriHalI2sEventDmaError,
} FuriHalI2sEvent;

typedef void (*FuriHalI2sCallback)(FuriHalI2sEvent event, void* context);

void furi_hal_i2s_init(void);

void furi_hal_i2s_start(
    FuriHalI2sId id,
    FuriHalI2sDataFormat data_format,
    uint16_t* buffer,
    size_t length,
    FuriHalI2sCallback callback,
    void* context);

void furi_hal_i2s_stop(FuriHalI2sId id);

#ifdef __cplusplus
}
#endif
