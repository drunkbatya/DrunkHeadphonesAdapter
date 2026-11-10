#include <furi_hal_usb.h>
#include <furi_hal_usb_i.h>
#include <furi_hal_bus.h>
#include <furi_hal_interrupt.h>
#include <furi_hal_power.h>

#include <stm32g0xx_ll_pwr.h>
#include <stm32g0xx_ll_utils.h>
#include <furi.h>
#include <toolbox/api_lock.h>

#include <inttypes.h>
#include <stdio.h>

#include <tusb.h>
#include <device/dcd.h>

#define TAG "FuriHalUsb"

#define FURI_HAL_USB_THREAD_STACK_SIZE 1024

#define FURI_HAL_USB_VID        0x1209
#define FURI_HAL_USB_PID        0x0001
#define FURI_HAL_USB_BCD_DEVICE 0x0100
#define FURI_HAL_USB_BCD_USB    0x0200

#define FURI_HAL_USB_CONFIGURATION_NUMBER     1
#define FURI_HAL_USB_CONFIGURATION_COUNT      1
#define FURI_HAL_USB_MAX_POWER_MA             100
#define FURI_HAL_USB_HID_POLL_INTERVAL_MS     10
#define FURI_HAL_USB_CDC_NOTIFICATION_EP_SIZE 8

#define FURI_HAL_USB_LANGID_ENG_US        0x0409
#define FURI_HAL_USB_MANUFACTURER         "DrunkBatya"
#define FURI_HAL_USB_PRODUCT              "DrunkHeadphonesAdapter"
#define FURI_HAL_USB_STRING_NONE          0
#define FURI_HAL_USB_STRING_HEADER_LENGTH 1
#define FURI_HAL_USB_STRING_MAX_LENGTH    32

#define FURI_HAL_USB_UID_WORD_COUNT      3
#define FURI_HAL_USB_UID_WORD_HEX_LENGTH (2 * sizeof(uint32_t))
#define FURI_HAL_USB_SERIAL_NUMBER_LENGTH \
    (FURI_HAL_USB_UID_WORD_COUNT * FURI_HAL_USB_UID_WORD_HEX_LENGTH)

#define FURI_HAL_USB_AUDIO_BCD_ADC 0x0200
#define FURI_HAL_USB_AUDIO_INTERFACE_COUNT \
    (FuriHalUsbInterfaceAudioMic - FuriHalUsbInterfaceAudioControl + 1)
#define FURI_HAL_USB_AUDIO_CONTROL_ENDPOINT_COUNT 0
#define FURI_HAL_USB_AUDIO_IDLE_ENDPOINT_COUNT    0
#define FURI_HAL_USB_AUDIO_SPEAKER_ENDPOINT_COUNT 2
#define FURI_HAL_USB_AUDIO_MIC_ENDPOINT_COUNT     1
#define FURI_HAL_USB_AUDIO_TERMINAL_NONE          0
#define FURI_HAL_USB_AUDIO_LOCK_DELAY_NONE        0
#define FURI_HAL_USB_AUDIO_DATA_INTERVAL          1
#define FURI_HAL_USB_AUDIO_FEEDBACK_INTERVAL      1

#define FURI_HAL_USB_AUDIO_CLOCK_CONTROLS                         \
    ((AUDIO20_CTRL_RW << AUDIO20_CLOCK_SOURCE_CTRL_CLK_FRQ_POS) | \
     (AUDIO20_CTRL_R << AUDIO20_CLOCK_SOURCE_CTRL_CLK_VAL_POS))

#define FURI_HAL_USB_AUDIO_FEATURE_UNIT_MASTER_CONTROLS        \
    ((AUDIO20_CTRL_RW << AUDIO20_FEATURE_UNIT_CTRL_MUTE_POS) | \
     (AUDIO20_CTRL_RW << AUDIO20_FEATURE_UNIT_CTRL_VOLUME_POS))

#define FURI_HAL_USB_AUDIO_FEATURE_UNIT_CHANNEL_CONTROLS AUDIO20_CTRL_NONE

#define FURI_HAL_USB_AUDIO_SPEAKER_CHANNEL_CONFIG \
    (AUDIO20_CHANNEL_CONFIG_FRONT_LEFT | AUDIO20_CHANNEL_CONFIG_FRONT_RIGHT)

#define FURI_HAL_USB_AUDIO_MIC_CHANNEL_CONFIG AUDIO20_CHANNEL_CONFIG_FRONT_CENTER

#define FURI_HAL_USB_AUDIO_SPEAKER_CONTROL_LENGTH                             \
    (TUD_AUDIO20_DESC_CLK_SRC_LEN + TUD_AUDIO20_DESC_INPUT_TERM_LEN +         \
     TUD_AUDIO20_DESC_FEATURE_UNIT_LEN(FURI_HAL_USB_AUDIO_SPEAKER_CHANNELS) + \
     TUD_AUDIO20_DESC_OUTPUT_TERM_LEN)

#define FURI_HAL_USB_AUDIO_MIC_CONTROL_LENGTH                             \
    (TUD_AUDIO20_DESC_CLK_SRC_LEN + TUD_AUDIO20_DESC_INPUT_TERM_LEN +     \
     TUD_AUDIO20_DESC_FEATURE_UNIT_LEN(FURI_HAL_USB_AUDIO_MIC_CHANNELS) + \
     TUD_AUDIO20_DESC_OUTPUT_TERM_LEN)

#define FURI_HAL_USB_AUDIO_CONTROL_LENGTH \
    (FURI_HAL_USB_AUDIO_SPEAKER_CONTROL_LENGTH + FURI_HAL_USB_AUDIO_MIC_CONTROL_LENGTH)

#define FURI_HAL_USB_AUDIO_SPEAKER_ALT_LENGTH                                  \
    (TUD_AUDIO20_DESC_STD_AS_LEN + TUD_AUDIO20_DESC_CS_AS_INT_LEN +            \
     TUD_AUDIO20_DESC_TYPE_I_FORMAT_LEN + TUD_AUDIO20_DESC_STD_AS_ISO_EP_LEN + \
     TUD_AUDIO20_DESC_CS_AS_ISO_EP_LEN + TUD_AUDIO20_DESC_STD_AS_ISO_FB_EP_LEN)

#define FURI_HAL_USB_AUDIO_MIC_ALT_LENGTH                                      \
    (TUD_AUDIO20_DESC_STD_AS_LEN + TUD_AUDIO20_DESC_CS_AS_INT_LEN +            \
     TUD_AUDIO20_DESC_TYPE_I_FORMAT_LEN + TUD_AUDIO20_DESC_STD_AS_ISO_EP_LEN + \
     TUD_AUDIO20_DESC_CS_AS_ISO_EP_LEN)

#define FURI_HAL_USB_AUDIO_SPEAKER_STREAMING_LENGTH                        \
    (TUD_AUDIO20_DESC_STD_AS_LEN + FURI_HAL_USB_AUDIO_SPEAKER_ALT_LENGTH + \
     FURI_HAL_USB_AUDIO_SPEAKER_ALT_LENGTH)

#define FURI_HAL_USB_AUDIO_MIC_STREAMING_LENGTH                        \
    (TUD_AUDIO20_DESC_STD_AS_LEN + FURI_HAL_USB_AUDIO_MIC_ALT_LENGTH + \
     FURI_HAL_USB_AUDIO_MIC_ALT_LENGTH)

#define FURI_HAL_USB_AUDIO_LENGTH                                                          \
    (TUD_AUDIO20_DESC_IAD_LEN + TUD_AUDIO20_DESC_STD_AC_LEN + TUD_AUDIO20_DESC_CS_AC_LEN + \
     FURI_HAL_USB_AUDIO_CONTROL_LENGTH + FURI_HAL_USB_AUDIO_SPEAKER_STREAMING_LENGTH +     \
     FURI_HAL_USB_AUDIO_MIC_STREAMING_LENGTH)

#define FURI_HAL_USB_CONFIGURATION_LENGTH \
    (TUD_CONFIG_DESC_LEN + FURI_HAL_USB_AUDIO_LENGTH + TUD_HID_DESC_LEN + TUD_CDC_DESC_LEN)

#define FURI_HAL_USB_AUDIO_SPEAKER_ALT(_alt_setting, _subslot_size, _resolution, _ep_size) \
    TUD_AUDIO20_DESC_STD_AS_INT(                                                           \
        FuriHalUsbInterfaceAudioSpeaker,                                                   \
        _alt_setting,                                                                      \
        FURI_HAL_USB_AUDIO_SPEAKER_ENDPOINT_COUNT,                                         \
        FURI_HAL_USB_STRING_NONE),                                                         \
        TUD_AUDIO20_DESC_CS_AS_INT(                                                        \
            FuriHalUsbAudioEntitySpeakerInputTerminal,                                     \
            AUDIO20_CTRL_NONE,                                                             \
            AUDIO20_FORMAT_TYPE_I,                                                         \
            AUDIO20_DATA_FORMAT_TYPE_I_PCM,                                                \
            FURI_HAL_USB_AUDIO_SPEAKER_CHANNELS,                                           \
            FURI_HAL_USB_AUDIO_SPEAKER_CHANNEL_CONFIG,                                     \
            FURI_HAL_USB_STRING_NONE),                                                     \
        TUD_AUDIO20_DESC_TYPE_I_FORMAT(_subslot_size, _resolution),                        \
        TUD_AUDIO20_DESC_STD_AS_ISO_EP(                                                    \
            FuriHalUsbEndpointAudioSpeakerOut,                                             \
            (uint8_t)(TUSB_XFER_ISOCHRONOUS | TUSB_ISO_EP_ATT_ASYNCHRONOUS |               \
                      TUSB_ISO_EP_ATT_DATA),                                               \
            _ep_size,                                                                      \
            FURI_HAL_USB_AUDIO_DATA_INTERVAL),                                             \
        TUD_AUDIO20_DESC_CS_AS_ISO_EP(                                                     \
            AUDIO20_CS_AS_ISO_DATA_EP_ATT_NON_MAX_PACKETS_OK,                              \
            AUDIO20_CTRL_NONE,                                                             \
            AUDIO20_CS_AS_ISO_DATA_EP_LOCK_DELAY_UNIT_UNDEFINED,                           \
            FURI_HAL_USB_AUDIO_LOCK_DELAY_NONE),                                           \
        TUD_AUDIO20_DESC_STD_AS_ISO_FB_EP(                                                 \
            FuriHalUsbEndpointAudioFeedbackIn,                                             \
            FURI_HAL_USB_AUDIO_FEEDBACK_EP_SIZE,                                           \
            FURI_HAL_USB_AUDIO_FEEDBACK_INTERVAL)

#define FURI_HAL_USB_AUDIO_MIC_ALT(_alt_setting, _subslot_size, _resolution, _ep_size) \
    TUD_AUDIO20_DESC_STD_AS_INT(                                                       \
        FuriHalUsbInterfaceAudioMic,                                                   \
        _alt_setting,                                                                  \
        FURI_HAL_USB_AUDIO_MIC_ENDPOINT_COUNT,                                         \
        FURI_HAL_USB_STRING_NONE),                                                     \
        TUD_AUDIO20_DESC_CS_AS_INT(                                                    \
            FuriHalUsbAudioEntityMicOutputTerminal,                                    \
            AUDIO20_CTRL_NONE,                                                         \
            AUDIO20_FORMAT_TYPE_I,                                                     \
            AUDIO20_DATA_FORMAT_TYPE_I_PCM,                                            \
            FURI_HAL_USB_AUDIO_MIC_CHANNELS,                                           \
            FURI_HAL_USB_AUDIO_MIC_CHANNEL_CONFIG,                                     \
            FURI_HAL_USB_STRING_NONE),                                                 \
        TUD_AUDIO20_DESC_TYPE_I_FORMAT(_subslot_size, _resolution),                    \
        TUD_AUDIO20_DESC_STD_AS_ISO_EP(                                                \
            FuriHalUsbEndpointAudioMicIn,                                              \
            (uint8_t)(TUSB_XFER_ISOCHRONOUS | TUSB_ISO_EP_ATT_ASYNCHRONOUS |           \
                      TUSB_ISO_EP_ATT_DATA),                                           \
            _ep_size,                                                                  \
            FURI_HAL_USB_AUDIO_DATA_INTERVAL),                                         \
        TUD_AUDIO20_DESC_CS_AS_ISO_EP(                                                 \
            AUDIO20_CS_AS_ISO_DATA_EP_ATT_NON_MAX_PACKETS_OK,                          \
            AUDIO20_CTRL_NONE,                                                         \
            AUDIO20_CS_AS_ISO_DATA_EP_LOCK_DELAY_UNIT_UNDEFINED,                       \
            FURI_HAL_USB_AUDIO_LOCK_DELAY_NONE)

typedef enum {
    UsbApiEventTypeEnable,
    UsbApiEventTypeDisable,
    UsbApiEventTypeSetStateCallback,
} UsbApiEventType;

typedef struct {
    FuriHalUsbStateCallback callback;
    void* context;
} UsbApiEventDataStateCallback;

typedef union {
    UsbApiEventDataStateCallback state_callback;
} UsbApiEventData;

typedef struct {
    FuriApiLock lock;
    UsbApiEventType type;
    UsbApiEventData data;
} UsbApiEventMessage;

typedef struct {
    FuriThread* thread;
    FuriMessageQueue* queue;
    bool enabled;
    bool suspended;
    bool insomnia;
    FuriHalUsbStateCallback callback;
    void* callback_context;
    char serial_number[FURI_HAL_USB_SERIAL_NUMBER_LENGTH + 1];
} UsbSrv;

typedef enum {
    UsbEventStack = (1 << 0),
    UsbEventMessage = (1 << 1),
    UsbEventHidReport = (1 << 2),
    UsbEventCdcRx = (1 << 3),
    UsbEventCdcTx = (1 << 4),
} UsbEvent;

#define USB_SRV_ALL_EVENTS \
    (UsbEventStack | UsbEventMessage | UsbEventHidReport | UsbEventCdcRx | UsbEventCdcTx)

static UsbSrv usb = {0};

static const uint16_t usb_language_ids[] = {FURI_HAL_USB_LANGID_ENG_US};

static uint16_t
    usb_string_descriptor[FURI_HAL_USB_STRING_HEADER_LENGTH + FURI_HAL_USB_STRING_MAX_LENGTH];

static const tusb_desc_device_t usb_device_descriptor = {
    .bLength = sizeof(tusb_desc_device_t),
    .bDescriptorType = TUSB_DESC_DEVICE,
    .bcdUSB = FURI_HAL_USB_BCD_USB,
    .bDeviceClass = TUSB_CLASS_MISC,
    .bDeviceSubClass = MISC_SUBCLASS_COMMON,
    .bDeviceProtocol = MISC_PROTOCOL_IAD,
    .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor = FURI_HAL_USB_VID,
    .idProduct = FURI_HAL_USB_PID,
    .bcdDevice = FURI_HAL_USB_BCD_DEVICE,
    .iManufacturer = FuriHalUsbStringManufacturer,
    .iProduct = FuriHalUsbStringProduct,
    .iSerialNumber = FuriHalUsbStringSerialNumber,
    .bNumConfigurations = FURI_HAL_USB_CONFIGURATION_COUNT,
};

static const uint8_t usb_hid_report_descriptor[] = {
    TUD_HID_REPORT_DESC_CONSUMER(),
};

static const uint8_t usb_configuration_descriptor[] = {
    TUD_CONFIG_DESCRIPTOR(
        FURI_HAL_USB_CONFIGURATION_NUMBER,
        FuriHalUsbInterfaceCount,
        FURI_HAL_USB_STRING_NONE,
        FURI_HAL_USB_CONFIGURATION_LENGTH,
        TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP,
        FURI_HAL_USB_MAX_POWER_MA),

    TUD_AUDIO20_DESC_IAD(
        FuriHalUsbInterfaceAudioControl,
        FURI_HAL_USB_AUDIO_INTERFACE_COUNT,
        FURI_HAL_USB_STRING_NONE),
    TUD_AUDIO20_DESC_STD_AC(
        FuriHalUsbInterfaceAudioControl,
        FURI_HAL_USB_AUDIO_CONTROL_ENDPOINT_COUNT,
        FURI_HAL_USB_STRING_NONE),
    TUD_AUDIO20_DESC_CS_AC(
        FURI_HAL_USB_AUDIO_BCD_ADC,
        AUDIO20_FUNC_HEADSET,
        FURI_HAL_USB_AUDIO_CONTROL_LENGTH,
        AUDIO20_CTRL_NONE),

    TUD_AUDIO20_DESC_CLK_SRC(
        FuriHalUsbAudioEntitySpeakerClock,
        AUDIO20_CLOCK_SOURCE_ATT_INT_PRO_CLK,
        FURI_HAL_USB_AUDIO_CLOCK_CONTROLS,
        FURI_HAL_USB_AUDIO_TERMINAL_NONE,
        FURI_HAL_USB_STRING_NONE),
    TUD_AUDIO20_DESC_INPUT_TERM(
        FuriHalUsbAudioEntitySpeakerInputTerminal,
        AUDIO_TERM_TYPE_USB_STREAMING,
        FURI_HAL_USB_AUDIO_TERMINAL_NONE,
        FuriHalUsbAudioEntitySpeakerClock,
        FURI_HAL_USB_AUDIO_SPEAKER_CHANNELS,
        FURI_HAL_USB_AUDIO_SPEAKER_CHANNEL_CONFIG,
        FURI_HAL_USB_STRING_NONE,
        AUDIO20_CTRL_NONE,
        FURI_HAL_USB_STRING_NONE),
    TUD_AUDIO20_DESC_FEATURE_UNIT(
        FuriHalUsbAudioEntitySpeakerFeatureUnit,
        FuriHalUsbAudioEntitySpeakerInputTerminal,
        FURI_HAL_USB_STRING_NONE,
        FURI_HAL_USB_AUDIO_FEATURE_UNIT_MASTER_CONTROLS,
        FURI_HAL_USB_AUDIO_FEATURE_UNIT_CHANNEL_CONTROLS,
        FURI_HAL_USB_AUDIO_FEATURE_UNIT_CHANNEL_CONTROLS),
    TUD_AUDIO20_DESC_OUTPUT_TERM(
        FuriHalUsbAudioEntitySpeakerOutputTerminal,
        AUDIO_TERM_TYPE_OUT_HEADPHONES,
        FURI_HAL_USB_AUDIO_TERMINAL_NONE,
        FuriHalUsbAudioEntitySpeakerFeatureUnit,
        FuriHalUsbAudioEntitySpeakerClock,
        AUDIO20_CTRL_NONE,
        FURI_HAL_USB_STRING_NONE),

    TUD_AUDIO20_DESC_CLK_SRC(
        FuriHalUsbAudioEntityMicClock,
        AUDIO20_CLOCK_SOURCE_ATT_INT_PRO_CLK,
        FURI_HAL_USB_AUDIO_CLOCK_CONTROLS,
        FURI_HAL_USB_AUDIO_TERMINAL_NONE,
        FURI_HAL_USB_STRING_NONE),
    TUD_AUDIO20_DESC_INPUT_TERM(
        FuriHalUsbAudioEntityMicInputTerminal,
        AUDIO_TERM_TYPE_IN_GENERIC_MIC,
        FURI_HAL_USB_AUDIO_TERMINAL_NONE,
        FuriHalUsbAudioEntityMicClock,
        FURI_HAL_USB_AUDIO_MIC_CHANNELS,
        FURI_HAL_USB_AUDIO_MIC_CHANNEL_CONFIG,
        FURI_HAL_USB_STRING_NONE,
        AUDIO20_CTRL_NONE,
        FURI_HAL_USB_STRING_NONE),
    TUD_AUDIO20_DESC_FEATURE_UNIT(
        FuriHalUsbAudioEntityMicFeatureUnit,
        FuriHalUsbAudioEntityMicInputTerminal,
        FURI_HAL_USB_STRING_NONE,
        FURI_HAL_USB_AUDIO_FEATURE_UNIT_MASTER_CONTROLS,
        FURI_HAL_USB_AUDIO_FEATURE_UNIT_CHANNEL_CONTROLS),
    TUD_AUDIO20_DESC_OUTPUT_TERM(
        FuriHalUsbAudioEntityMicOutputTerminal,
        AUDIO_TERM_TYPE_USB_STREAMING,
        FURI_HAL_USB_AUDIO_TERMINAL_NONE,
        FuriHalUsbAudioEntityMicFeatureUnit,
        FuriHalUsbAudioEntityMicClock,
        AUDIO20_CTRL_NONE,
        FURI_HAL_USB_STRING_NONE),

    TUD_AUDIO20_DESC_STD_AS_INT(
        FuriHalUsbInterfaceAudioSpeaker,
        FuriHalUsbAudioAltSettingIdle,
        FURI_HAL_USB_AUDIO_IDLE_ENDPOINT_COUNT,
        FURI_HAL_USB_STRING_NONE),
    FURI_HAL_USB_AUDIO_SPEAKER_ALT(
        FuriHalUsbAudioAltSetting16Bit,
        FURI_HAL_USB_AUDIO_16BIT_SUBSLOT_SIZE,
        FURI_HAL_USB_AUDIO_16BIT_RESOLUTION,
        FURI_HAL_USB_AUDIO_SPEAKER_16BIT_EP_SIZE),
    FURI_HAL_USB_AUDIO_SPEAKER_ALT(
        FuriHalUsbAudioAltSetting24Bit,
        FURI_HAL_USB_AUDIO_24BIT_SUBSLOT_SIZE,
        FURI_HAL_USB_AUDIO_24BIT_RESOLUTION,
        FURI_HAL_USB_AUDIO_SPEAKER_24BIT_EP_SIZE),

    TUD_AUDIO20_DESC_STD_AS_INT(
        FuriHalUsbInterfaceAudioMic,
        FuriHalUsbAudioAltSettingIdle,
        FURI_HAL_USB_AUDIO_IDLE_ENDPOINT_COUNT,
        FURI_HAL_USB_STRING_NONE),
    FURI_HAL_USB_AUDIO_MIC_ALT(
        FuriHalUsbAudioAltSetting16Bit,
        FURI_HAL_USB_AUDIO_16BIT_SUBSLOT_SIZE,
        FURI_HAL_USB_AUDIO_16BIT_RESOLUTION,
        FURI_HAL_USB_AUDIO_MIC_16BIT_EP_SIZE),
    FURI_HAL_USB_AUDIO_MIC_ALT(
        FuriHalUsbAudioAltSetting24Bit,
        FURI_HAL_USB_AUDIO_24BIT_SUBSLOT_SIZE,
        FURI_HAL_USB_AUDIO_24BIT_RESOLUTION,
        FURI_HAL_USB_AUDIO_MIC_24BIT_EP_SIZE),

    TUD_HID_DESCRIPTOR(
        FuriHalUsbInterfaceHid,
        FURI_HAL_USB_STRING_NONE,
        HID_ITF_PROTOCOL_NONE,
        sizeof(usb_hid_report_descriptor),
        FuriHalUsbEndpointHidIn,
        CFG_TUD_HID_EP_BUFSIZE,
        FURI_HAL_USB_HID_POLL_INTERVAL_MS),

    TUD_CDC_DESCRIPTOR(
        FuriHalUsbInterfaceCdcControl,
        FURI_HAL_USB_STRING_NONE,
        FuriHalUsbEndpointCdcNotificationIn,
        FURI_HAL_USB_CDC_NOTIFICATION_EP_SIZE,
        FuriHalUsbEndpointCdcDataOut,
        FuriHalUsbEndpointCdcDataIn,
        CFG_TUD_CDC_EP_BUFSIZE),
};

_Static_assert(
    sizeof(usb_configuration_descriptor) == FURI_HAL_USB_CONFIGURATION_LENGTH,
    "USB configuration descriptor length mismatch");

static int32_t furi_hal_usb_thread(void* context);

static void furi_hal_usb_serial_number_init(void) {
    const uint32_t uid[FURI_HAL_USB_UID_WORD_COUNT] = {
        LL_GetUID_Word2(),
        LL_GetUID_Word1(),
        LL_GetUID_Word0(),
    };

    char* position = usb.serial_number;
    for(size_t i = 0; i < COUNT_OF(uid); i++) {
        position += snprintf(
            position,
            FURI_HAL_USB_UID_WORD_HEX_LENGTH + 1,
            "%0*" PRIX32,
            (int)FURI_HAL_USB_UID_WORD_HEX_LENGTH,
            uid[i]);
    }
}

void furi_hal_usb_init(void) {
    LL_PWR_EnableVddUSB();

    furi_hal_usb_serial_number_init();
    furi_hal_usb_hid_init();

    usb.queue = furi_message_queue_alloc(1, sizeof(UsbApiEventMessage));
    usb.thread = furi_thread_alloc_service(
        "UsbDriver", FURI_HAL_USB_THREAD_STACK_SIZE, furi_hal_usb_thread, NULL);
    furi_thread_set_priority(usb.thread, FuriThreadPriorityHighest);
    furi_thread_start(usb.thread);

    FURI_LOG_I(TAG, "Init OK");
}

static void furi_hal_usb_send_message(UsbApiEventMessage* message) {
    furi_message_queue_put(usb.queue, message, FuriWaitForever);
    furi_thread_flags_set(furi_thread_get_id(usb.thread), UsbEventMessage);
    api_lock_wait_unlock_and_free(message->lock);
}

void furi_hal_usb_enable(void) {
    furi_check(furi_hal_usb_audio_is_configured());

    UsbApiEventMessage msg = {
        .lock = api_lock_alloc_locked(),
        .type = UsbApiEventTypeEnable,
    };

    furi_hal_usb_send_message(&msg);
}

void furi_hal_usb_disable(void) {
    UsbApiEventMessage msg = {
        .lock = api_lock_alloc_locked(),
        .type = UsbApiEventTypeDisable,
    };

    furi_hal_usb_send_message(&msg);
}

void furi_hal_usb_set_state_callback(FuriHalUsbStateCallback callback, void* context) {
    UsbApiEventMessage msg = {
        .lock = api_lock_alloc_locked(),
        .type = UsbApiEventTypeSetStateCallback,
        .data.state_callback =
            {
                .callback = callback,
                .context = context,
            },
    };

    furi_hal_usb_send_message(&msg);
}

void furi_hal_usb_hid_request_send(void) {
    furi_thread_flags_set(furi_thread_get_id(usb.thread), UsbEventHidReport);
}

void furi_hal_usb_cdc_request_rx(void) {
    furi_thread_flags_set(furi_thread_get_id(usb.thread), UsbEventCdcRx);
}

void furi_hal_usb_cdc_request_tx(void) {
    furi_thread_flags_set(furi_thread_get_id(usb.thread), UsbEventCdcTx);
}

static void furi_hal_usb_isr(void* context) {
    UNUSED(context);
    tud_int_handler(FURI_HAL_USB_ROOT_PORT);
}

static void furi_hal_usb_update_insomnia(void) {
    const bool insomnia = usb.enabled && !usb.suspended;
    if(insomnia == usb.insomnia) {
        return;
    }

    if(insomnia) {
        furi_hal_power_insomnia_enter();
    } else {
        furi_hal_power_insomnia_exit();
    }
    usb.insomnia = insomnia;
}

static void furi_hal_usb_notify_state(FuriHalUsbStateEvent state) {
    if(usb.callback != NULL) {
        usb.callback(state, usb.callback_context);
    }
}

void tud_event_hook_cb(uint8_t rhport, uint32_t eventid, bool in_isr) {
    UNUSED(rhport);
    UNUSED(in_isr);

    if(eventid == DCD_EVENT_BUS_RESET_START || eventid == DCD_EVENT_BUS_RESET_END ||
       eventid == DCD_EVENT_UNPLUGGED) {
        furi_hal_usb_audio_begin_bus_reset();
    }

    furi_thread_flags_set(furi_thread_get_id(usb.thread), UsbEventStack);
}

void tud_mount_cb(void) {
    FURI_LOG_I(TAG, "Mount");
    furi_hal_usb_cdc_mount();
    furi_hal_usb_notify_state(FuriHalUsbStateEventMount);
}

void tud_umount_cb(void) {
    FURI_LOG_I(TAG, "Unmount");
    furi_hal_usb_hid_reset();
    furi_hal_usb_audio_reset();
    furi_hal_usb_cdc_reset();
    furi_hal_usb_notify_state(FuriHalUsbStateEventUnmount);
}

void tud_suspend_cb(bool remote_wakeup_en) {
    FURI_LOG_I(TAG, "Suspend, remote wakeup %u", remote_wakeup_en);
    usb.suspended = true;
    furi_hal_usb_update_insomnia();
    furi_hal_usb_notify_state(FuriHalUsbStateEventSuspend);
}

void tud_resume_cb(void) {
    FURI_LOG_I(TAG, "Resume");
    usb.suspended = false;
    furi_hal_usb_update_insomnia();
    furi_hal_usb_notify_state(FuriHalUsbStateEventResume);
    furi_hal_usb_hid_send_pending();
}

const uint8_t* tud_descriptor_device_cb(void) {
    return (const uint8_t*)&usb_device_descriptor;
}

const uint8_t* tud_descriptor_configuration_cb(uint8_t index) {
    UNUSED(index);
    return usb_configuration_descriptor;
}

const uint8_t* tud_hid_descriptor_report_cb(uint8_t instance) {
    UNUSED(instance);
    return usb_hid_report_descriptor;
}

const uint16_t* tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
    UNUSED(langid);

    uint16_t* string_data = &usb_string_descriptor[FURI_HAL_USB_STRING_HEADER_LENGTH];
    size_t length;
    if(index == FuriHalUsbStringLanguage) {
        length = COUNT_OF(usb_language_ids);
        for(size_t i = 0; i < length; i++) {
            string_data[i] = usb_language_ids[i];
        }
    } else {
        const char* string;
        switch(index) {
        case FuriHalUsbStringManufacturer:
            string = FURI_HAL_USB_MANUFACTURER;
            break;
        case FuriHalUsbStringProduct:
            string = FURI_HAL_USB_PRODUCT;
            break;
        case FuriHalUsbStringSerialNumber:
            string = usb.serial_number;
            break;
        default:
            return NULL;
        }

        length = strlen(string);
        furi_check(length <= FURI_HAL_USB_STRING_MAX_LENGTH);
        for(size_t i = 0; i < length; i++) {
            string_data[i] = string[i];
        }
    }

    const size_t descriptor_size = sizeof(uint16_t) * (FURI_HAL_USB_STRING_HEADER_LENGTH + length);
    usb_string_descriptor[0] = TU_U16(TUSB_DESC_STRING, descriptor_size);
    return usb_string_descriptor;
}

static void usb_process_enable(bool enable) {
    if(enable == usb.enabled) {
        return;
    }

    if(enable) {
        furi_hal_bus_enable(FuriHalBusUSB);
        furi_hal_interrupt_set_isr_ex(
            FuriHalInterruptIdUsb, FuriHalInterruptPriorityHighest, furi_hal_usb_isr, NULL);

        const tusb_rhport_init_t device_init = {
            .role = TUSB_ROLE_DEVICE,
            .speed = TUSB_SPEED_FULL,
        };
        furi_check(tusb_init(FURI_HAL_USB_ROOT_PORT, &device_init));

        usb.enabled = true;
        usb.suspended = false;
        FURI_LOG_I(TAG, "USB Enable");
    } else {
        tud_deinit(FURI_HAL_USB_ROOT_PORT);
        furi_hal_interrupt_set_isr(FuriHalInterruptIdUsb, NULL, NULL);
        furi_hal_bus_disable(FuriHalBusUSB);

        usb.enabled = false;
        furi_hal_usb_hid_reset();
        furi_hal_usb_audio_reset();
        furi_hal_usb_cdc_reset();
        FURI_LOG_I(TAG, "USB Disable");
    }

    furi_hal_usb_update_insomnia();
}

static void usb_process_message(UsbApiEventMessage* message) {
    switch(message->type) {
    case UsbApiEventTypeEnable:
        usb_process_enable(true);
        break;
    case UsbApiEventTypeDisable:
        usb_process_enable(false);
        break;
    case UsbApiEventTypeSetStateCallback:
        usb.callback = message->data.state_callback.callback;
        usb.callback_context = message->data.state_callback.context;
        break;
    }

    api_lock_unlock(message->lock);
}

static int32_t furi_hal_usb_thread(void* context) {
    UNUSED(context);

    if(furi_message_queue_get_count(usb.queue) > 0) {
        furi_thread_flags_set(furi_thread_get_id(usb.thread), UsbEventMessage);
    }

    while(true) {
        uint32_t flags =
            furi_thread_flags_wait(USB_SRV_ALL_EVENTS, FuriFlagWaitAny, FuriWaitForever);
        furi_check((flags & FuriFlagError) == 0);

        if(flags & UsbEventMessage) {
            UsbApiEventMessage message;
            if(furi_message_queue_get(usb.queue, &message, 0) == FuriStatusOk) {
                usb_process_message(&message);
            }
        }

        if(!usb.enabled) {
            continue;
        }

        if(flags & UsbEventStack) {
            tud_task();
            furi_hal_usb_audio_finish_bus_reset();
        }

        if(flags & UsbEventHidReport) {
            furi_hal_usb_hid_send_pending();
        }

        if(flags & UsbEventCdcRx) {
            furi_hal_usb_cdc_process_rx();
        }

        if(flags & UsbEventCdcTx) {
            furi_hal_usb_cdc_process_tx();
        }
    }

    return 0;
}
