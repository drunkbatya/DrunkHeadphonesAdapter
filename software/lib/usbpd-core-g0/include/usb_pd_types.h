#ifndef USB_PD_TYPES_H_
#define USB_PD_TYPES_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define USB_PD_PORT_COUNT    2u
#define USB_PD_MAX_PDO_COUNT 7u

typedef enum {
    UsbPdPowerRoleSink,
    UsbPdPowerRoleSource,
} UsbPdPowerRole;

typedef enum {
    UsbPdDataRoleUfp,
    UsbPdDataRoleDfp,
} UsbPdDataRole;

typedef enum {
    UsbPdSpecRevision2 = 1,
    UsbPdSpecRevision3 = 2,
} UsbPdSpecRevision;

typedef enum {
    UsbPdCcPinNone,
    UsbPdCcPin1,
    UsbPdCcPin2,
} UsbPdCcPin;

typedef enum {
    UsbPdRpCurrentDefault,
    UsbPdRpCurrent1A5,
    UsbPdRpCurrent3A0,
} UsbPdRpCurrent;

typedef enum {
    UsbPdPortModeDisabled,
    UsbPdPortModeSink,
    UsbPdPortModeSource,
    UsbPdPortModeDualRole,
    UsbPdPortModeDualRoleTrySink,
} UsbPdPortMode;

typedef enum {
    UsbPdAnswerAccept,
    UsbPdAnswerReject,
    UsbPdAnswerWait,
    UsbPdAnswerNotSupported,
} UsbPdAnswer;

typedef enum {
    UsbPdPdoTypeFixed,
    UsbPdPdoTypeBattery,
    UsbPdPdoTypeVariable,
    UsbPdPdoTypeAugmented,
} UsbPdPdoType;

typedef struct {
    uint32_t objects[USB_PD_MAX_PDO_COUNT];
    uint8_t count;
} UsbPdCapabilities;

#endif
