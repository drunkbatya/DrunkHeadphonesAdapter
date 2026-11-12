#include <applications/services/usbc/usbc_vbus.h>

#include <furi.h>
#include <furi_hal.h>

struct UsbcVbus {
    UsbcVbusWakeupCallback wakeup_callback;
    void* context;
    UsbPdVbusLevel levels[UsbcPortCount];
    uint32_t contract_voltages_mv[UsbcPortCount];
};

UsbcVbus* usbc_vbus_alloc(UsbcVbusWakeupCallback wakeup_callback, void* context) {
    UsbcVbus* vbus = malloc(sizeof(UsbcVbus));
    vbus->wakeup_callback = wakeup_callback;
    vbus->context = context;
    return vbus;
}

void usbc_vbus_free(UsbcVbus* vbus) {
    free(vbus);
}

UsbPdVbusLevel usbc_vbus_get_level(UsbcVbus* vbus, UsbcPort port) {
    UNUSED(vbus);
    UNUSED(port);
    return UsbPdVbusLevelSafe0V;
}

void usbc_vbus_set_contract_voltage(UsbcVbus* vbus, UsbcPort port, uint32_t voltage_mv) {
    UNUSED(vbus);
    UNUSED(port);
    UNUSED(voltage_mv);
}

uint32_t usbc_vbus_process(UsbcVbus* vbus) {
    UNUSED(vbus);
    return UINT32_MAX;
}
