#include <applications/services/usbc/usbc_platform.h>

#include <furi.h>
#include <furi_hal.h>

#include <usb_pd_platform.h>

void usbc_platform_init(UsbcVbus* vbus) {
    UNUSED(vbus);
}

void usbc_platform_deinit(void) {
}

uint32_t usb_pd_platform_get_tick_ms(void) {
    return 0;
}

uint32_t usb_pd_platform_disable_interrupts(void) {
    return 0;
}

void usb_pd_platform_restore_interrupts(uint32_t state) {
    UNUSED(state);
}

void usb_pd_platform_timer_start(uint8_t port, UsbPdPlatformTimer timer, uint32_t time_us) {
    UNUSED(port);
    UNUSED(timer);
    UNUSED(time_us);
}

bool usb_pd_platform_timer_is_expired(uint8_t port, UsbPdPlatformTimer timer) {
    UNUSED(port);
    UNUSED(timer);
    return true;
}

void usb_pd_platform_set_rp(uint8_t port, UsbPdRpCurrent current) {
    UNUSED(port);
    UNUSED(current);
}

void usb_pd_platform_set_rd(uint8_t port) {
    UNUSED(port);
}

void usb_pd_platform_set_cc_open(uint8_t port) {
    UNUSED(port);
}

void usb_pd_platform_get_cc(uint8_t port, UsbPdCcState* cc1, UsbPdCcState* cc2) {
    UNUSED(port);
    *cc1 = UsbPdCcStateOpen;
    *cc2 = UsbPdCcStateOpen;
}

UsbPdVbusLevel usb_pd_platform_get_vbus_level(uint8_t port) {
    UNUSED(port);
    return UsbPdVbusLevelSafe0V;
}

void usb_pd_platform_start_communication(uint8_t port, UsbPdCcPin pin) {
    UNUSED(port);
    UNUSED(pin);
}

void usb_pd_platform_stop_communication(uint8_t port) {
    UNUSED(port);
}

void usb_pd_platform_set_receive_buffer(uint8_t port, uint8_t* buffer, size_t size) {
    UNUSED(port);
    UNUSED(buffer);
    UNUSED(size);
}

void usb_pd_platform_enable_receive(uint8_t port) {
    UNUSED(port);
}

void usb_pd_platform_disable_receive(uint8_t port) {
    UNUSED(port);
}

bool usb_pd_platform_send_message(uint8_t port, const uint8_t* data, size_t size) {
    UNUSED(port);
    UNUSED(data);
    UNUSED(size);
    return false;
}

void usb_pd_platform_send_hard_reset(uint8_t port) {
    UNUSED(port);
}

bool usb_pd_platform_start_bist_carrier(uint8_t port) {
    UNUSED(port);
    return false;
}

bool usb_pd_platform_stop_bist_carrier(uint8_t port) {
    UNUSED(port);
    return false;
}

void usb_pd_platform_set_sink_tx_ok(uint8_t port, bool ok) {
    UNUSED(port);
    UNUSED(ok);
}

bool usb_pd_platform_is_sink_tx_ok(uint8_t port) {
    UNUSED(port);
    return false;
}

void usb_pd_platform_send_fast_role_swap(uint8_t port) {
    UNUSED(port);
}

void usb_pd_platform_set_fast_role_swap_detection(uint8_t port, bool enabled) {
    UNUSED(port);
    UNUSED(enabled);
}
