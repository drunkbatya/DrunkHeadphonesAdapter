#include "usb_pd_pdo.h"

#define USB_PD_PDO_TYPE_POSITION        30u
#define USB_PD_PDO_APDO_TYPE_POSITION   28u
#define USB_PD_PDO_APDO_TYPE_PPS        0u
#define USB_PD_PDO_FIELD_10_BITS        0x3FFu
#define USB_PD_PDO_FIELD_8_BITS         0xFFu
#define USB_PD_PDO_FIELD_7_BITS         0x7Fu
#define USB_PD_PDO_FIELD_12_BITS        0xFFFu
#define USB_PD_PDO_VOLTAGE_POSITION     10u
#define USB_PD_PDO_MAX_VOLTAGE_POSITION 20u
#define USB_PD_PPS_MAX_VOLTAGE_POSITION 17u
#define USB_PD_PPS_MIN_VOLTAGE_POSITION 8u
#define USB_PD_PDO_DUAL_ROLE_POWER      (1u << 29)
#define USB_PD_PDO_UNCONSTRAINED_POWER  (1u << 27)
#define USB_PD_PDO_USB_COMMUNICATIONS   (1u << 26)
#define USB_PD_PDO_DUAL_ROLE_DATA       (1u << 25)
#define USB_PD_PDO_FRS_POSITION         23u
#define USB_PD_PDO_FRS_MASK             (0x3u << USB_PD_PDO_FRS_POSITION)
#define USB_PD_RDO_POSITION_POSITION    28u
#define USB_PD_RDO_CURRENT_POSITION     10u
#define USB_PD_RDO_PPS_VOLTAGE_POSITION 9u
#define USB_PD_FIXED_VOLTAGE_UNIT_MV    50u
#define USB_PD_PPS_VOLTAGE_UNIT_MV      100u
#define USB_PD_RDO_PPS_VOLTAGE_UNIT_MV  20u
#define USB_PD_FIXED_CURRENT_UNIT_MA    10u
#define USB_PD_PPS_CURRENT_UNIT_MA      50u
#define USB_PD_BATTERY_POWER_UNIT_MW    250u

UsbPdPdoType usb_pd_pdo_get_type(uint32_t pdo) {
    return (UsbPdPdoType)(pdo >> USB_PD_PDO_TYPE_POSITION);
}

bool usb_pd_pdo_is_pps(uint32_t pdo) {
    return (usb_pd_pdo_get_type(pdo) == UsbPdPdoTypeAugmented) &&
           (((pdo >> USB_PD_PDO_APDO_TYPE_POSITION) & 0x3u) == USB_PD_PDO_APDO_TYPE_PPS);
}

uint32_t usb_pd_pdo_get_max_voltage_mv(uint32_t pdo) {
    switch(usb_pd_pdo_get_type(pdo)) {
    case UsbPdPdoTypeFixed:
        return ((pdo >> USB_PD_PDO_VOLTAGE_POSITION) & USB_PD_PDO_FIELD_10_BITS) *
               USB_PD_FIXED_VOLTAGE_UNIT_MV;
    case UsbPdPdoTypeBattery:
    case UsbPdPdoTypeVariable:
        return ((pdo >> USB_PD_PDO_MAX_VOLTAGE_POSITION) & USB_PD_PDO_FIELD_10_BITS) *
               USB_PD_FIXED_VOLTAGE_UNIT_MV;
    default:
        if(!usb_pd_pdo_is_pps(pdo)) {
            return 0u;
        }
        return ((pdo >> USB_PD_PPS_MAX_VOLTAGE_POSITION) & USB_PD_PDO_FIELD_8_BITS) *
               USB_PD_PPS_VOLTAGE_UNIT_MV;
    }
}

uint32_t usb_pd_pdo_get_min_voltage_mv(uint32_t pdo) {
    switch(usb_pd_pdo_get_type(pdo)) {
    case UsbPdPdoTypeFixed:
        return usb_pd_pdo_get_max_voltage_mv(pdo);
    case UsbPdPdoTypeBattery:
    case UsbPdPdoTypeVariable:
        return ((pdo >> USB_PD_PDO_VOLTAGE_POSITION) & USB_PD_PDO_FIELD_10_BITS) *
               USB_PD_FIXED_VOLTAGE_UNIT_MV;
    default:
        if(!usb_pd_pdo_is_pps(pdo)) {
            return 0u;
        }
        return ((pdo >> USB_PD_PPS_MIN_VOLTAGE_POSITION) & USB_PD_PDO_FIELD_8_BITS) *
               USB_PD_PPS_VOLTAGE_UNIT_MV;
    }
}

uint32_t usb_pd_pdo_get_max_current_ma(uint32_t pdo) {
    switch(usb_pd_pdo_get_type(pdo)) {
    case UsbPdPdoTypeFixed:
    case UsbPdPdoTypeVariable:
        return (pdo & USB_PD_PDO_FIELD_10_BITS) * USB_PD_FIXED_CURRENT_UNIT_MA;
    case UsbPdPdoTypeBattery: {
        uint32_t min_voltage_mv = usb_pd_pdo_get_min_voltage_mv(pdo);
        uint32_t power_mw = (pdo & USB_PD_PDO_FIELD_10_BITS) * USB_PD_BATTERY_POWER_UNIT_MW;

        return (min_voltage_mv == 0u) ? 0u : ((power_mw * 1000u) / min_voltage_mv);
    }
    default:
        if(!usb_pd_pdo_is_pps(pdo)) {
            return 0u;
        }
        return (pdo & USB_PD_PDO_FIELD_7_BITS) * USB_PD_PPS_CURRENT_UNIT_MA;
    }
}

uint32_t usb_pd_pdo_set_max_current_ma(uint32_t pdo, uint32_t current_ma) {
    uint32_t units;

    switch(usb_pd_pdo_get_type(pdo)) {
    case UsbPdPdoTypeFixed:
    case UsbPdPdoTypeVariable:
        units = current_ma / USB_PD_FIXED_CURRENT_UNIT_MA;
        if(units > USB_PD_PDO_FIELD_10_BITS) {
            units = USB_PD_PDO_FIELD_10_BITS;
        }
        return (pdo & ~USB_PD_PDO_FIELD_10_BITS) | units;
    default:
        if(!usb_pd_pdo_is_pps(pdo)) {
            return pdo;
        }
        units = current_ma / USB_PD_PPS_CURRENT_UNIT_MA;
        if(units > USB_PD_PDO_FIELD_7_BITS) {
            units = USB_PD_PDO_FIELD_7_BITS;
        }
        return (pdo & ~USB_PD_PDO_FIELD_7_BITS) | units;
    }
}

static bool usb_pd_pdo_fixed_flag(uint32_t pdo, uint32_t flag) {
    return (usb_pd_pdo_get_type(pdo) == UsbPdPdoTypeFixed) && ((pdo & flag) != 0u);
}

bool usb_pd_pdo_is_dual_role_power(uint32_t pdo) {
    return usb_pd_pdo_fixed_flag(pdo, USB_PD_PDO_DUAL_ROLE_POWER);
}

bool usb_pd_pdo_is_unconstrained_power(uint32_t pdo) {
    return usb_pd_pdo_fixed_flag(pdo, USB_PD_PDO_UNCONSTRAINED_POWER);
}

bool usb_pd_pdo_is_usb_communications_capable(uint32_t pdo) {
    return usb_pd_pdo_fixed_flag(pdo, USB_PD_PDO_USB_COMMUNICATIONS);
}

bool usb_pd_pdo_is_dual_role_data(uint32_t pdo) {
    return usb_pd_pdo_fixed_flag(pdo, USB_PD_PDO_DUAL_ROLE_DATA);
}

UsbPdFastRoleSwapCurrent usb_pd_pdo_get_fast_role_swap_current(uint32_t sink_pdo) {
    if(usb_pd_pdo_get_type(sink_pdo) != UsbPdPdoTypeFixed) {
        return UsbPdFastRoleSwapCurrentNone;
    }
    return (UsbPdFastRoleSwapCurrent)((sink_pdo & USB_PD_PDO_FRS_MASK) >> USB_PD_PDO_FRS_POSITION);
}

uint32_t
    usb_pd_pdo_set_fast_role_swap_current(uint32_t sink_pdo, UsbPdFastRoleSwapCurrent current) {
    if(usb_pd_pdo_get_type(sink_pdo) != UsbPdPdoTypeFixed) {
        return sink_pdo;
    }
    return (sink_pdo & ~USB_PD_PDO_FRS_MASK) |
           (((uint32_t)current << USB_PD_PDO_FRS_POSITION) & USB_PD_PDO_FRS_MASK);
}

uint8_t usb_pd_rdo_get_object_position(uint32_t rdo) {
    return (uint8_t)(rdo >> USB_PD_RDO_POSITION_POSITION);
}

uint32_t usb_pd_rdo_get_operating_current_ma(uint32_t rdo, uint32_t pdo) {
    if(usb_pd_pdo_is_pps(pdo)) {
        return (rdo & USB_PD_PDO_FIELD_7_BITS) * USB_PD_PPS_CURRENT_UNIT_MA;
    }
    if((usb_pd_pdo_get_type(pdo) == UsbPdPdoTypeFixed) ||
       (usb_pd_pdo_get_type(pdo) == UsbPdPdoTypeVariable)) {
        return ((rdo >> USB_PD_RDO_CURRENT_POSITION) & USB_PD_PDO_FIELD_10_BITS) *
               USB_PD_FIXED_CURRENT_UNIT_MA;
    }
    return 0u;
}

uint32_t usb_pd_rdo_get_pps_voltage_mv(uint32_t rdo) {
    return ((rdo >> USB_PD_RDO_PPS_VOLTAGE_POSITION) & USB_PD_PDO_FIELD_12_BITS) *
           USB_PD_RDO_PPS_VOLTAGE_UNIT_MV;
}

uint32_t usb_pd_rdo_make(
    uint8_t object_position,
    uint32_t pdo,
    uint32_t current_ma,
    uint32_t pps_voltage_mv) {
    uint32_t rdo = (uint32_t)object_position << USB_PD_RDO_POSITION_POSITION;
    uint32_t units;

    if(usb_pd_pdo_is_pps(pdo)) {
        units = current_ma / USB_PD_PPS_CURRENT_UNIT_MA;
        if(units > USB_PD_PDO_FIELD_7_BITS) {
            units = USB_PD_PDO_FIELD_7_BITS;
        }
        rdo |= ((pps_voltage_mv / USB_PD_RDO_PPS_VOLTAGE_UNIT_MV) & USB_PD_PDO_FIELD_12_BITS)
               << USB_PD_RDO_PPS_VOLTAGE_POSITION;
        return rdo | units;
    }
    units = current_ma / USB_PD_FIXED_CURRENT_UNIT_MA;
    if(units > USB_PD_PDO_FIELD_10_BITS) {
        units = USB_PD_PDO_FIELD_10_BITS;
    }
    return rdo | (units << USB_PD_RDO_CURRENT_POSITION) | units;
}
