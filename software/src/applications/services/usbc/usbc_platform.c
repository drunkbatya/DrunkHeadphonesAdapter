#include <applications/services/usbc/usbc_platform.h>

#include <furi.h>
#include <furi_hal.h>

#include <usb_pd_platform.h>

#define USBC_PLATFORM_TIMER_COUNT (UsbPdPlatformTimerRetry + 1)

typedef struct {
    UsbcVbus* vbus;
    FuriHalCortexTimer timers[UsbcPortCount][USBC_PLATFORM_TIMER_COUNT];
} UsbcPlatform;

static UsbcPlatform usbc_platform = {0};

static const UsbcPort usbc_platform_ports[UsbcPortCount] = {
    [UsbcPortPhone] = UsbcPortPhone,
    [UsbcPortCharger] = UsbcPortCharger,
};

static FuriHalUsbPdId usbc_platform_get_id(uint8_t port) {
    furi_check(port < UsbcPortCount);
    return (FuriHalUsbPdId)port;
}

static UsbPdCcState usbc_platform_get_cc_state(FuriHalUsbPdCcState state) {
    switch(state) {
    case FuriHalUsbPdCcStateRa:
        return UsbPdCcStateRa;
    case FuriHalUsbPdCcStateRd:
        return UsbPdCcStateRd;
    case FuriHalUsbPdCcStateRpDefault:
        return UsbPdCcStateRpDefault;
    case FuriHalUsbPdCcStateRp1A5:
        return UsbPdCcStateRp1A5;
    case FuriHalUsbPdCcStateRp3A0:
        return UsbPdCcStateRp3A0;
    case FuriHalUsbPdCcStateOpen:
    default:
        return UsbPdCcStateOpen;
    }
}

static void usbc_platform_usbpd_callback(FuriHalUsbPdEvent event, void* context) {
    const uint8_t port = *(const UsbcPort*)context;

    switch(event) {
    case FuriHalUsbPdEventCcChanged:
        usb_pd_on_cc_changed(port);
        break;
    case FuriHalUsbPdEventMessageReceived:
        usb_pd_on_message_received(port);
        break;
    case FuriHalUsbPdEventHardResetReceived:
        usb_pd_on_hard_reset_received(port);
        break;
    case FuriHalUsbPdEventHardResetSent:
        usb_pd_on_hard_reset_sent(port);
        break;
    case FuriHalUsbPdEventTransmitSent:
        usb_pd_on_transmit_completed(port, true);
        break;
    case FuriHalUsbPdEventTransmitFailed:
        usb_pd_on_transmit_completed(port, false);
        break;
    case FuriHalUsbPdEventFastRoleSwapReceived:
        furi_check(!usb_pd_on_fast_role_swap_received(port));
        break;
    }
}

void usbc_platform_init(UsbcVbus* vbus) {
    furi_check(vbus);
    furi_check(!usbc_platform.vbus);

    usbc_platform.vbus = vbus;
    for(UsbcPort port = 0; port < UsbcPortCount; port++) {
        furi_hal_usbpd_start(
            usbc_platform_get_id(port),
            usbc_platform_usbpd_callback,
            (void*)&usbc_platform_ports[port]);
    }
}

void usbc_platform_deinit(void) {
    furi_check(usbc_platform.vbus);

    for(UsbcPort port = 0; port < UsbcPortCount; port++) {
        furi_hal_usbpd_stop(usbc_platform_get_id(port));
    }
    usbc_platform.vbus = NULL;
}

uint32_t usb_pd_platform_get_tick_ms(void) {
    return furi_get_tick();
}

uint32_t usb_pd_platform_disable_interrupts(void) {
    const uint32_t primask = __get_PRIMASK();
    __disable_irq();
    return primask;
}

void usb_pd_platform_restore_interrupts(uint32_t state) {
    __set_PRIMASK(state);
}

void usb_pd_platform_timer_start(uint8_t port, UsbPdPlatformTimer timer, uint32_t time_us) {
    furi_check(port < UsbcPortCount);
    furi_check(timer < USBC_PLATFORM_TIMER_COUNT);

    const FuriHalCortexTimer cortex_timer = furi_hal_cortex_timer_get(time_us);
    const uint32_t primask = usb_pd_platform_disable_interrupts();
    usbc_platform.timers[port][timer] = cortex_timer;
    usb_pd_platform_restore_interrupts(primask);
}

bool usb_pd_platform_timer_is_expired(uint8_t port, UsbPdPlatformTimer timer) {
    furi_check(port < UsbcPortCount);
    furi_check(timer < USBC_PLATFORM_TIMER_COUNT);

    const uint32_t primask = usb_pd_platform_disable_interrupts();
    const FuriHalCortexTimer cortex_timer = usbc_platform.timers[port][timer];
    usb_pd_platform_restore_interrupts(primask);
    return furi_hal_cortex_timer_is_expired(cortex_timer);
}

void usb_pd_platform_set_rp(uint8_t port, UsbPdRpCurrent current) {
    FuriHalUsbPdRp rp = FuriHalUsbPdRpDefault;
    if(current == UsbPdRpCurrent1A5) {
        rp = FuriHalUsbPdRp1A5;
    } else if(current == UsbPdRpCurrent3A0) {
        rp = FuriHalUsbPdRp3A0;
    }
    furi_hal_usbpd_set_rp(usbc_platform_get_id(port), rp);
}

void usb_pd_platform_set_rd(uint8_t port) {
    furi_hal_usbpd_set_rd(usbc_platform_get_id(port));
}

void usb_pd_platform_set_cc_open(uint8_t port) {
    furi_hal_usbpd_set_cc_open(usbc_platform_get_id(port));
}

void usb_pd_platform_get_cc(uint8_t port, UsbPdCcState* cc1, UsbPdCcState* cc2) {
    FuriHalUsbPdCcState hal_cc1;
    FuriHalUsbPdCcState hal_cc2;

    furi_hal_usbpd_get_cc(usbc_platform_get_id(port), &hal_cc1, &hal_cc2);
    *cc1 = usbc_platform_get_cc_state(hal_cc1);
    *cc2 = usbc_platform_get_cc_state(hal_cc2);
}

UsbPdVbusLevel usb_pd_platform_get_vbus_level(uint8_t port) {
    furi_check(port < UsbcPortCount);
    return usbc_vbus_get_level(usbc_platform.vbus, (UsbcPort)port);
}

void usb_pd_platform_start_communication(uint8_t port, UsbPdCcPin pin) {
    furi_check((pin == UsbPdCcPin1) || (pin == UsbPdCcPin2));
    furi_hal_usbpd_start_communication(
        usbc_platform_get_id(port),
        (pin == UsbPdCcPin1) ? FuriHalUsbPdCcPin1 : FuriHalUsbPdCcPin2);
}

void usb_pd_platform_stop_communication(uint8_t port) {
    furi_hal_usbpd_stop_communication(usbc_platform_get_id(port));
}

void usb_pd_platform_set_receive_buffer(uint8_t port, uint8_t* buffer, size_t size) {
    furi_hal_usbpd_set_receive_buffer(usbc_platform_get_id(port), buffer, size);
}

void usb_pd_platform_enable_receive(uint8_t port) {
    furi_hal_usbpd_enable_receive(usbc_platform_get_id(port));
}

void usb_pd_platform_disable_receive(uint8_t port) {
    furi_hal_usbpd_disable_receive(usbc_platform_get_id(port));
}

bool usb_pd_platform_send_message(uint8_t port, const uint8_t* data, size_t size) {
    return furi_hal_usbpd_send_message(usbc_platform_get_id(port), data, size);
}

void usb_pd_platform_send_hard_reset(uint8_t port) {
    furi_hal_usbpd_send_hard_reset(usbc_platform_get_id(port));
}

bool usb_pd_platform_start_bist_carrier(uint8_t port) {
    furi_hal_usbpd_start_bist_carrier(usbc_platform_get_id(port));
    return true;
}

bool usb_pd_platform_stop_bist_carrier(uint8_t port) {
    furi_hal_usbpd_stop_bist_carrier(usbc_platform_get_id(port));
    return true;
}

void usb_pd_platform_set_sink_tx_ok(uint8_t port, bool ok) {
    furi_hal_usbpd_set_sink_tx_ok(usbc_platform_get_id(port), ok);
}

bool usb_pd_platform_is_sink_tx_ok(uint8_t port) {
    return furi_hal_usbpd_is_sink_tx_ok(usbc_platform_get_id(port));
}

void usb_pd_platform_send_fast_role_swap(uint8_t port) {
    furi_hal_usbpd_send_fast_role_swap(usbc_platform_get_id(port));
}

void usb_pd_platform_set_fast_role_swap_detection(uint8_t port, bool enabled) {
    furi_hal_usbpd_set_fast_role_swap_detection(usbc_platform_get_id(port), enabled);
}
