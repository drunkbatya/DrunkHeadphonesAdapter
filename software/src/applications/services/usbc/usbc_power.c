#include <applications/services/usbc/usbc_power.h>

#include <furi.h>
#include <furi_hal.h>

#include <drivers/tcpp03.h>

struct UsbcPower {
    UsbPdPower states[UsbcPortCount];
};

UsbcPower* usbc_power_alloc(void) {
    UsbcPower* power = malloc(sizeof(UsbcPower));
    return power;
}

void usbc_power_free(UsbcPower* power) {
    free(power);
}

void usbc_power_set(UsbcPower* power, UsbcPort port, UsbPdPower state) {
    UNUSED(power);
    UNUSED(port);
    UNUSED(state);
}
