#include <applications/services/usbc/usbc_vbus.h>

#include <furi.h>
#include <furi_hal.h>

#define USBC_VBUS_DIVIDER                 14
#define USBC_VBUS_POLL_MS                 5
#define USBC_VBUS_SAFE0V_MAX_MV           800
#define USBC_VBUS_SAFE5V_MAX_MV           5500
#define USBC_VBUS_SAFE5V_CONTRACT_MV      5000
#define USBC_VBUS_SINK_DISCONNECT_MV      3670
#define USBC_VBUS_SRC_NEW_MIN_PERCENT     95
#define USBC_VBUS_SINK_PD_DROP_MV         1250
#define USBC_VBUS_SINK_DISCONNECT_PERCENT 90

typedef struct {
    UsbcVbus* vbus;
    FuriHalCompId comp;
    FuriHalAdcChannel adc_channel;
    volatile bool edge;
    bool above;
    UsbPdVbusLevel level;
    uint32_t contract_mv;
    uint32_t disconnect_mv;
} UsbcVbusPort;

struct UsbcVbus {
    UsbcVbusWakeupCallback wakeup_callback;
    void* context;
    bool adc_started;
    UsbcVbusPort ports[UsbcPortCount];
};

static const FuriHalCompId usbc_vbus_comps[UsbcPortCount] = {
    [UsbcPortPhone] = FuriHalCompIdPhoneVbus,
    [UsbcPortCharger] = FuriHalCompIdChargerVbus,
};

static const FuriHalAdcChannel usbc_vbus_adc_channels[UsbcPortCount] = {
    [UsbcPortPhone] = FuriHalAdcChannelPhoneVbusSense,
    [UsbcPortCharger] = FuriHalAdcChannelChargerVbusSense,
};

static uint32_t usbc_vbus_get_sink_pd_min_mv(uint32_t contract_mv) {
    return contract_mv * USBC_VBUS_SRC_NEW_MIN_PERCENT / 100 - USBC_VBUS_SINK_PD_DROP_MV;
}

static uint32_t usbc_vbus_get_disconnect_mv(uint32_t contract_mv) {
    if(contract_mv <= USBC_VBUS_SAFE5V_CONTRACT_MV) {
        return USBC_VBUS_SINK_DISCONNECT_MV;
    }
    return usbc_vbus_get_sink_pd_min_mv(contract_mv) * USBC_VBUS_SINK_DISCONNECT_PERCENT / 100;
}

static UsbPdVbusLevel usbc_vbus_get_stable_level(const UsbcVbusPort* port) {
    if(!port->above) {
        return UsbPdVbusLevelSafe0V;
    }
    if(port->contract_mv > USBC_VBUS_SAFE5V_CONTRACT_MV) {
        return UsbPdVbusLevelAboveSafe5V;
    }
    return UsbPdVbusLevelPresent;
}

static UsbPdVbusLevel usbc_vbus_classify(const UsbcVbusPort* port, uint32_t vbus_mv) {
    if(vbus_mv < USBC_VBUS_SAFE0V_MAX_MV) {
        return UsbPdVbusLevelSafe0V;
    }
    if(vbus_mv < port->disconnect_mv) {
        return UsbPdVbusLevelBelowSinkDisconnect;
    }
    if(vbus_mv > USBC_VBUS_SAFE5V_MAX_MV) {
        return UsbPdVbusLevelAboveSafe5V;
    }
    return UsbPdVbusLevelPresent;
}

static void usbc_vbus_comp_callback(bool above, void* context) {
    UNUSED(above);
    UsbcVbusPort* port = context;

    port->edge = true;
    port->vbus->wakeup_callback(port->vbus->context);
}

static void usbc_vbus_set_adc_started(UsbcVbus* vbus, bool started) {
    if(vbus->adc_started == started) {
        return;
    }
    if(started) {
        furi_hal_adc_start();
    } else {
        furi_hal_adc_stop();
    }
    vbus->adc_started = started;
}

static bool usbc_vbus_take_edges(UsbcVbus* vbus) {
    bool edge = false;

    FURI_CRITICAL_ENTER();
    for(UsbcPort i = 0; i < UsbcPortCount; i++) {
        edge |= vbus->ports[i].edge;
        vbus->ports[i].edge = false;
    }
    FURI_CRITICAL_EXIT();
    return edge;
}

static bool usbc_vbus_measure(UsbcVbus* vbus, bool notify) {
    bool stable = true;

    for(UsbcPort i = 0; i < UsbcPortCount; i++) {
        UsbcVbusPort* port = &vbus->ports[i];
        port->above = furi_hal_comp_is_above(port->comp);

        const uint32_t vbus_mv = furi_hal_adc_read_mv(port->adc_channel) * USBC_VBUS_DIVIDER;
        const UsbPdVbusLevel level = usbc_vbus_classify(port, vbus_mv);
        if(level != port->level) {
            port->level = level;
            if(notify) {
                usb_pd_on_vbus_changed(i);
            }
        }
        stable &= (level == usbc_vbus_get_stable_level(port));
    }
    return stable;
}

UsbcVbus* usbc_vbus_alloc(UsbcVbusWakeupCallback wakeup_callback, void* context) {
    furi_check(wakeup_callback);

    UsbcVbus* vbus = malloc(sizeof(UsbcVbus));
    vbus->wakeup_callback = wakeup_callback;
    vbus->context = context;

    for(UsbcPort i = 0; i < UsbcPortCount; i++) {
        UsbcVbusPort* port = &vbus->ports[i];
        port->vbus = vbus;
        port->comp = usbc_vbus_comps[i];
        port->adc_channel = usbc_vbus_adc_channels[i];
        port->level = UsbPdVbusLevelSafe0V;
        port->contract_mv = USBC_VBUS_SAFE5V_CONTRACT_MV;
        port->disconnect_mv = usbc_vbus_get_disconnect_mv(port->contract_mv);
        furi_hal_comp_start(port->comp, usbc_vbus_comp_callback, port);
    }

    usbc_vbus_set_adc_started(vbus, true);
    if(usbc_vbus_measure(vbus, false)) {
        usbc_vbus_set_adc_started(vbus, false);
    }

    return vbus;
}

void usbc_vbus_free(UsbcVbus* vbus) {
    for(UsbcPort i = 0; i < UsbcPortCount; i++) {
        furi_hal_comp_stop(vbus->ports[i].comp);
    }
    usbc_vbus_set_adc_started(vbus, false);
    free(vbus);
}

UsbPdVbusLevel usbc_vbus_get_level(UsbcVbus* vbus, UsbcPort port) {
    furi_check(port < UsbcPortCount);
    return vbus->ports[port].level;
}

void usbc_vbus_set_contract_voltage(UsbcVbus* vbus, UsbcPort port, uint32_t voltage_mv) {
    furi_check(port < UsbcPortCount);
    UsbcVbusPort* vbus_port = &vbus->ports[port];

    if(vbus_port->contract_mv == voltage_mv) {
        return;
    }
    vbus_port->contract_mv = voltage_mv;
    vbus_port->disconnect_mv = usbc_vbus_get_disconnect_mv(voltage_mv);

    if(voltage_mv <= USBC_VBUS_SAFE5V_CONTRACT_MV) {
        furi_hal_comp_set_threshold_quarter_vrefint(vbus_port->comp);
    } else {
        const uint32_t wakeup_mv =
            (vbus_port->disconnect_mv + usbc_vbus_get_sink_pd_min_mv(voltage_mv)) / 2;
        furi_hal_comp_set_threshold_mv(vbus_port->comp, wakeup_mv / USBC_VBUS_DIVIDER);
    }

    vbus_port->edge = true;
    vbus->wakeup_callback(vbus->context);
}

uint32_t usbc_vbus_process(UsbcVbus* vbus) {
    const bool edge = usbc_vbus_take_edges(vbus);
    if(!edge && !vbus->adc_started) {
        return UINT32_MAX;
    }

    usbc_vbus_set_adc_started(vbus, true);
    if(usbc_vbus_measure(vbus, true)) {
        usbc_vbus_set_adc_started(vbus, false);
        return UINT32_MAX;
    }
    return USBC_VBUS_POLL_MS;
}
