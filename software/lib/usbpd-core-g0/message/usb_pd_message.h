#ifndef USB_PD_MESSAGE_H_
#define USB_PD_MESSAGE_H_

#include "usb_pd_types.h"

#define USB_PD_MESSAGE_HEADER_SIZE     2u
#define USB_PD_EXTENDED_HEADER_SIZE    2u
#define USB_PD_DATA_OBJECT_SIZE        4u
#define USB_PD_MAX_DATA_OBJECTS        7u
#define USB_PD_MAX_EXTENDED_DATA_SIZE  260u
#define USB_PD_CHUNK_DATA_SIZE         26u
#define USB_PD_EXTENDED_PAYLOAD_OFFSET (USB_PD_MESSAGE_HEADER_SIZE + USB_PD_EXTENDED_HEADER_SIZE)
#define USB_PD_MESSAGE_BUFFER_SIZE     (USB_PD_EXTENDED_PAYLOAD_OFFSET + USB_PD_MAX_EXTENDED_DATA_SIZE)
#define USB_PD_EXTENDED_MESSAGE_FLAG   0x80u

#define USB_PD_REVISION_MESSAGE_DATA_OBJECT 0x32120000u

#define USB_PD_PDO_TYPE_POSITION            30u
#define USB_PD_PDO_TYPE_MASK                (0x3u << USB_PD_PDO_TYPE_POSITION)
#define USB_PD_APDO_TYPE_POSITION           28u
#define USB_PD_APDO_TYPE_MASK               (0x3u << USB_PD_APDO_TYPE_POSITION)
#define USB_PD_APDO_TYPE_PPS                0u
#define USB_PD_FIXED_PDO_VOLTAGE_MASK       (0x3FFu << 10)
#define USB_PD_FIXED_PDO_CURRENT_MASK       0x3FFu
#define USB_PD_RDO_OBJECT_POSITION_POSITION 28u
#define USB_PD_RDO_OBJECT_POSITION_MASK     (0xFu << USB_PD_RDO_OBJECT_POSITION_POSITION)

typedef enum {
    UsbPdControlMessageGoodCrc = 0x01,
    UsbPdControlMessageGotoMin = 0x02,
    UsbPdControlMessageAccept = 0x03,
    UsbPdControlMessageReject = 0x04,
    UsbPdControlMessagePing = 0x05,
    UsbPdControlMessagePsRdy = 0x06,
    UsbPdControlMessageGetSourceCap = 0x07,
    UsbPdControlMessageGetSinkCap = 0x08,
    UsbPdControlMessageDrSwap = 0x09,
    UsbPdControlMessagePrSwap = 0x0A,
    UsbPdControlMessageVconnSwap = 0x0B,
    UsbPdControlMessageWait = 0x0C,
    UsbPdControlMessageSoftReset = 0x0D,
    UsbPdControlMessageDataReset = 0x0E,
    UsbPdControlMessageDataResetComplete = 0x0F,
    UsbPdControlMessageNotSupported = 0x10,
    UsbPdControlMessageGetSourceCapExtended = 0x11,
    UsbPdControlMessageGetStatus = 0x12,
    UsbPdControlMessageFrSwap = 0x13,
    UsbPdControlMessageGetPpsStatus = 0x14,
    UsbPdControlMessageGetCountryCodes = 0x15,
    UsbPdControlMessageGetSinkCapExtended = 0x16,
    UsbPdControlMessageGetSourceInfo = 0x17,
    UsbPdControlMessageGetRevision = 0x18,
} UsbPdControlMessage;

typedef enum {
    UsbPdDataMessageSourceCapabilities = 0x01,
    UsbPdDataMessageRequest = 0x02,
    UsbPdDataMessageBist = 0x03,
    UsbPdDataMessageSinkCapabilities = 0x04,
    UsbPdDataMessageBatteryStatus = 0x05,
    UsbPdDataMessageAlert = 0x06,
    UsbPdDataMessageGetCountryInfo = 0x07,
    UsbPdDataMessageEnterUsb = 0x08,
    UsbPdDataMessageEprRequest = 0x09,
    UsbPdDataMessageEprMode = 0x0A,
    UsbPdDataMessageSourceInfo = 0x0B,
    UsbPdDataMessageRevision = 0x0C,
    UsbPdDataMessageVendorDefined = 0x0F,
} UsbPdDataMessage;

typedef enum {
    UsbPdExtendedMessagePpsStatus = 0x0C,
    UsbPdExtendedMessageSinkCapabilitiesExtended = 0x0F,
} UsbPdExtendedMessage;

typedef enum {
    UsbPdBistModeCarrier2 = 0x5,
    UsbPdBistModeTestData = 0x8,
    UsbPdBistModeSharedTestModeEntry = 0x9,
    UsbPdBistModeSharedTestModeExit = 0xA,
} UsbPdBistMode;

static inline uint32_t usb_pd_message_read_u32(const uint8_t* bytes) {
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) | ((uint32_t)bytes[2] << 16) |
           ((uint32_t)bytes[3] << 24);
}

static inline uint16_t usb_pd_message_read_u16(const uint8_t* bytes) {
    return (uint16_t)(bytes[0] | (bytes[1] << 8));
}

#endif
