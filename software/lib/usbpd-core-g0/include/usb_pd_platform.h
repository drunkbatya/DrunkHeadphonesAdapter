#ifndef USB_PD_PLATFORM_H_
#define USB_PD_PLATFORM_H_

#include "usb_pd_types.h"

typedef enum {
    UsbPdCcStateOpen,
    UsbPdCcStateRa,
    UsbPdCcStateRd,
    UsbPdCcStateRpDefault,
    UsbPdCcStateRp1A5,
    UsbPdCcStateRp3A0,
} UsbPdCcState;

typedef enum {
    UsbPdVbusLevelSafe0V,
    UsbPdVbusLevelBelowSinkDisconnect,
    UsbPdVbusLevelPresent,
    UsbPdVbusLevelAboveSafe5V,
} UsbPdVbusLevel;

typedef enum {
    UsbPdPlatformTimerGoodCrc,
    UsbPdPlatformTimerRetry,
} UsbPdPlatformTimer;

uint32_t usb_pd_platform_get_tick_ms(void);
uint32_t usb_pd_platform_disable_interrupts(void);
void usb_pd_platform_restore_interrupts(uint32_t state);

void usb_pd_platform_timer_start(uint8_t port, UsbPdPlatformTimer timer, uint32_t time_us);
bool usb_pd_platform_timer_is_expired(uint8_t port, UsbPdPlatformTimer timer);

void usb_pd_platform_set_rp(uint8_t port, UsbPdRpCurrent current);
void usb_pd_platform_set_rd(uint8_t port);
void usb_pd_platform_set_cc_open(uint8_t port);
void usb_pd_platform_get_cc(uint8_t port, UsbPdCcState* cc1, UsbPdCcState* cc2);

UsbPdVbusLevel usb_pd_platform_get_vbus_level(uint8_t port);

void usb_pd_platform_start_communication(uint8_t port, UsbPdCcPin pin);
void usb_pd_platform_stop_communication(uint8_t port);
void usb_pd_platform_set_receive_buffer(uint8_t port, uint8_t* buffer, size_t size);
void usb_pd_platform_enable_receive(uint8_t port);
void usb_pd_platform_disable_receive(uint8_t port);
bool usb_pd_platform_send_message(uint8_t port, const uint8_t* data, size_t size);
void usb_pd_platform_send_hard_reset(uint8_t port);
bool usb_pd_platform_start_bist_carrier(uint8_t port);
bool usb_pd_platform_stop_bist_carrier(uint8_t port);
void usb_pd_platform_set_sink_tx_ok(uint8_t port, bool ok);
bool usb_pd_platform_is_sink_tx_ok(uint8_t port);

void usb_pd_platform_send_fast_role_swap(uint8_t port);
void usb_pd_platform_set_fast_role_swap_detection(uint8_t port, bool enabled);

void usb_pd_on_cc_changed(uint8_t port);
void usb_pd_on_vbus_changed(uint8_t port);
void usb_pd_on_message_received(uint8_t port);
void usb_pd_on_hard_reset_received(uint8_t port);
void usb_pd_on_hard_reset_sent(uint8_t port);
void usb_pd_on_transmit_completed(uint8_t port, bool sent);
bool usb_pd_on_fast_role_swap_received(uint8_t port);

#endif
