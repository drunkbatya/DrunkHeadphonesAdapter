#ifndef USB_PD_PDO_H_
#define USB_PD_PDO_H_

#include "usb_pd_types.h"

typedef enum {
    UsbPdFastRoleSwapCurrentNone,
    UsbPdFastRoleSwapCurrentDefault,
    UsbPdFastRoleSwapCurrent1A5,
    UsbPdFastRoleSwapCurrent3A0,
} UsbPdFastRoleSwapCurrent;

UsbPdPdoType usb_pd_pdo_get_type(uint32_t pdo);
bool usb_pd_pdo_is_pps(uint32_t pdo);
uint32_t usb_pd_pdo_get_max_voltage_mv(uint32_t pdo);
uint32_t usb_pd_pdo_get_min_voltage_mv(uint32_t pdo);
uint32_t usb_pd_pdo_get_max_current_ma(uint32_t pdo);
uint32_t usb_pd_pdo_set_max_current_ma(uint32_t pdo, uint32_t current_ma);
bool usb_pd_pdo_is_dual_role_power(uint32_t pdo);
bool usb_pd_pdo_is_unconstrained_power(uint32_t pdo);
bool usb_pd_pdo_is_usb_communications_capable(uint32_t pdo);
bool usb_pd_pdo_is_dual_role_data(uint32_t pdo);
UsbPdFastRoleSwapCurrent usb_pd_pdo_get_fast_role_swap_current(uint32_t sink_pdo);
uint32_t
    usb_pd_pdo_set_fast_role_swap_current(uint32_t sink_pdo, UsbPdFastRoleSwapCurrent current);

uint8_t usb_pd_rdo_get_object_position(uint32_t rdo);
uint32_t usb_pd_rdo_get_operating_current_ma(uint32_t rdo, uint32_t pdo);
uint32_t usb_pd_rdo_get_pps_voltage_mv(uint32_t rdo);
uint32_t usb_pd_rdo_make(
    uint8_t object_position,
    uint32_t pdo,
    uint32_t current_ma,
    uint32_t pps_voltage_mv);

#endif
