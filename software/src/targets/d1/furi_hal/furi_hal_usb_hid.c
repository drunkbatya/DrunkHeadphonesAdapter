#include <furi_hal_usb_hid.h>
#include <furi_hal_usb_i.h>

#include <furi.h>

#include <tusb.h>

#define FURI_HAL_USB_HID_REPORT_QUEUE_SIZE 8
#define FURI_HAL_USB_HID_CONSUMER_RELEASED 0x0000
#define FURI_HAL_USB_HID_REPORT_ID_NONE    0

typedef struct {
    FuriMessageQueue* consumer_reports;
} FuriHalUsbHid;

static FuriHalUsbHid usb_hid = {0};

void furi_hal_usb_hid_init(void) {
    usb_hid.consumer_reports =
        furi_message_queue_alloc(FURI_HAL_USB_HID_REPORT_QUEUE_SIZE, sizeof(uint16_t));
}

void furi_hal_usb_hid_reset(void) {
    furi_message_queue_reset(usb_hid.consumer_reports);
}

void furi_hal_usb_hid_send_pending(void) {
    if(furi_message_queue_get_count(usb_hid.consumer_reports) == 0) {
        return;
    }

    if(tud_suspended()) {
        furi_hal_usb_remote_wakeup();
        return;
    }

    if(!tud_hid_ready()) {
        return;
    }

    uint16_t consumer_usage;
    furi_check(
        furi_message_queue_get(usb_hid.consumer_reports, &consumer_usage, 0) == FuriStatusOk);
    tud_hid_report(FURI_HAL_USB_HID_REPORT_ID_NONE, &consumer_usage, sizeof(consumer_usage));
}

static bool furi_hal_usb_hid_queue_consumer_report(uint16_t consumer_usage) {
    if(!tud_mounted()) {
        return false;
    }

    if(furi_message_queue_put(usb_hid.consumer_reports, &consumer_usage, 0) != FuriStatusOk) {
        return false;
    }

    furi_hal_usb_hid_request_send();
    return true;
}

bool furi_hal_hid_is_connected(void) {
    return tud_mounted();
}

bool furi_hal_hid_consumer_key_press(HidConsumerKey key) {
    return furi_hal_usb_hid_queue_consumer_report(key);
}

bool furi_hal_hid_consumer_key_release(void) {
    return furi_hal_usb_hid_queue_consumer_report(FURI_HAL_USB_HID_CONSUMER_RELEASED);
}

void tud_hid_report_complete_cb(uint8_t instance, uint8_t const* report, uint16_t len) {
    UNUSED(instance);
    UNUSED(report);
    UNUSED(len);
    furi_hal_usb_hid_send_pending();
}

uint16_t tud_hid_get_report_cb(
    uint8_t instance,
    uint8_t report_id,
    hid_report_type_t report_type,
    uint8_t* buffer,
    uint16_t reqlen) {
    UNUSED(instance);
    UNUSED(report_id);
    UNUSED(report_type);
    UNUSED(buffer);
    UNUSED(reqlen);
    return 0;
}

void tud_hid_set_report_cb(
    uint8_t instance,
    uint8_t report_id,
    hid_report_type_t report_type,
    uint8_t const* buffer,
    uint16_t bufsize) {
    UNUSED(instance);
    UNUSED(report_id);
    UNUSED(report_type);
    UNUSED(buffer);
    UNUSED(bufsize);
}
