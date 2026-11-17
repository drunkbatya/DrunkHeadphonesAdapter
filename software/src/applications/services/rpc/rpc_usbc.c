#include <applications/services/rpc/rpc_i.h>
#include <applications/services/usbc/usbc.h>

#include <furi.h>

#include <string.h>

struct RpcUsbc {
    Rpc* rpc;
    Usbc* usbc;
    UsbcStatus status;
};

static const adapter_UsbcPowerRole rpc_usbc_power_roles[] = {
    [UsbPdPowerRoleSink] = adapter_UsbcPowerRole_USBC_POWER_ROLE_SINK,
    [UsbPdPowerRoleSource] = adapter_UsbcPowerRole_USBC_POWER_ROLE_SOURCE,
};

static const adapter_UsbcDataRole rpc_usbc_data_roles[] = {
    [UsbPdDataRoleUfp] = adapter_UsbcDataRole_USBC_DATA_ROLE_UFP,
    [UsbPdDataRoleDfp] = adapter_UsbcDataRole_USBC_DATA_ROLE_DFP,
};

static const adapter_UsbcCcPin rpc_usbc_cc_pins[] = {
    [UsbPdCcPinNone] = adapter_UsbcCcPin_USBC_CC_PIN_NONE,
    [UsbPdCcPin1] = adapter_UsbcCcPin_USBC_CC_PIN_CC1,
    [UsbPdCcPin2] = adapter_UsbcCcPin_USBC_CC_PIN_CC2,
};

static const adapter_UsbcRpCurrent rpc_usbc_rp_currents[] = {
    [UsbPdRpCurrentDefault] = adapter_UsbcRpCurrent_USBC_RP_CURRENT_DEFAULT,
    [UsbPdRpCurrent1A5] = adapter_UsbcRpCurrent_USBC_RP_CURRENT_1A5,
    [UsbPdRpCurrent3A0] = adapter_UsbcRpCurrent_USBC_RP_CURRENT_3A0,
};

static const adapter_UsbcSpecRevision rpc_usbc_spec_revisions[] = {
    [UsbPdSpecRevision2] = adapter_UsbcSpecRevision_USBC_SPEC_REVISION_2,
    [UsbPdSpecRevision3] = adapter_UsbcSpecRevision_USBC_SPEC_REVISION_3,
};

static const adapter_UsbcPortMode rpc_usbc_port_modes[] = {
    [UsbPdPortModeDisabled] = adapter_UsbcPortMode_USBC_PORT_MODE_DISABLED,
    [UsbPdPortModeSink] = adapter_UsbcPortMode_USBC_PORT_MODE_SINK,
    [UsbPdPortModeSource] = adapter_UsbcPortMode_USBC_PORT_MODE_SOURCE,
    [UsbPdPortModeDualRole] = adapter_UsbcPortMode_USBC_PORT_MODE_DUAL_ROLE,
    [UsbPdPortModeDualRoleTrySink] = adapter_UsbcPortMode_USBC_PORT_MODE_DUAL_ROLE_TRY_SINK,
};

static const adapter_UsbcVbusLevel rpc_usbc_vbus_levels[] = {
    [UsbPdVbusLevelSafe0V] = adapter_UsbcVbusLevel_USBC_VBUS_LEVEL_SAFE_0V,
    [UsbPdVbusLevelBelowSinkDisconnect] =
        adapter_UsbcVbusLevel_USBC_VBUS_LEVEL_BELOW_SINK_DISCONNECT,
    [UsbPdVbusLevelPresent] = adapter_UsbcVbusLevel_USBC_VBUS_LEVEL_PRESENT,
    [UsbPdVbusLevelAboveSafe5V] = adapter_UsbcVbusLevel_USBC_VBUS_LEVEL_ABOVE_SAFE_5V,
};

static pb_size_t
    rpc_usbc_copy_capabilities(uint32_t* pdos, const UsbPdCapabilities* capabilities) {
    memcpy(pdos, capabilities->objects, capabilities->count * sizeof(capabilities->objects[0]));
    return capabilities->count;
}

static void rpc_usbc_fill_port(
    adapter_UsbcPortStatus* port_status,
    const UsbcPortStatus* usbc_port_status) {
    port_status->mode = rpc_usbc_port_modes[usbc_port_status->mode];
    port_status->attached = usbc_port_status->attached;
    port_status->partner_not_pd = usbc_port_status->partner_not_pd;
    port_status->power_role = rpc_usbc_power_roles[usbc_port_status->power_role];
    port_status->data_role = rpc_usbc_data_roles[usbc_port_status->data_role];
    port_status->cc_pin = rpc_usbc_cc_pins[usbc_port_status->cc_pin];
    port_status->partner_rp_current = rpc_usbc_rp_currents[usbc_port_status->partner_rp_current];
    port_status->spec_revision = rpc_usbc_spec_revisions[usbc_port_status->spec_revision];
    port_status->vbus_level = rpc_usbc_vbus_levels[usbc_port_status->vbus_level];
    port_status->contract_pdo = usbc_port_status->contract.pdo;
    port_status->contract_rdo = usbc_port_status->contract.rdo;
    port_status->partner_source_capabilities_count = rpc_usbc_copy_capabilities(
        port_status->partner_source_capabilities, &usbc_port_status->partner_source_capabilities);
    port_status->partner_sink_capabilities_count = rpc_usbc_copy_capabilities(
        port_status->partner_sink_capabilities, &usbc_port_status->partner_sink_capabilities);
    port_status->offer_count =
        rpc_usbc_copy_capabilities(port_status->offer, &usbc_port_status->offer);
    port_status->tcpp03_faulted = usbc_port_status->tcpp03_faulted;
}

static void rpc_usbc_handle_usbc_status(const adapter_RpcRequest* request, void* context) {
    RpcUsbc* rpc_usbc = context;

    UsbcStatus* status = &rpc_usbc->status;
    usbc_get_status(rpc_usbc->usbc, status);

    adapter_RpcResponse* response = rpc_begin_response(
        rpc_usbc->rpc, request->request_id, adapter_RpcResponse_usbc_status_tag);
    adapter_UsbcStatusResponse* usbc_status = &response->payload.usbc_status;

    usbc_status->has_phone = true;
    rpc_usbc_fill_port(&usbc_status->phone, &status->ports[UsbcPortPhone]);
    usbc_status->has_charger = true;
    rpc_usbc_fill_port(&usbc_status->charger, &status->ports[UsbcPortCharger]);

    rpc_send_response(rpc_usbc->rpc);
}

RpcUsbc* rpc_usbc_alloc(Rpc* rpc) {
    furi_check(rpc);

    RpcUsbc* rpc_usbc = malloc(sizeof(RpcUsbc));
    rpc_usbc->rpc = rpc;
    rpc_usbc->usbc = furi_record_open(RECORD_USBC);

    rpc_add_handler(
        rpc, adapter_RpcRequest_usbc_status_tag, rpc_usbc_handle_usbc_status, rpc_usbc);

    return rpc_usbc;
}

void rpc_usbc_free(RpcUsbc* rpc_usbc) {
    furi_check(rpc_usbc);

    furi_record_close(RECORD_USBC);
    free(rpc_usbc);
}
