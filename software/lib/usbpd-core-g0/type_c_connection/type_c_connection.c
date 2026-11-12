#include <string.h>

#include "type_c_connection/type_c_connection_i.h"

static TypeCConnection type_c_connections[USB_PD_PORT_COUNT];

static bool type_c_connection_is_rp(UsbPdCcState state) {
    return (state == UsbPdCcStateRpDefault) || (state == UsbPdCcStateRp1A5) ||
           (state == UsbPdCcStateRp3A0);
}

static bool type_c_connection_is_open_or_cable(UsbPdCcState state) {
    return (state == UsbPdCcStateOpen) || (state == UsbPdCcStateRa);
}

static bool type_c_connection_is_toggling(UsbPdPortMode mode) {
    return (mode == UsbPdPortModeDualRole) || (mode == UsbPdPortModeDualRoleTrySink);
}

static UsbPdRpCurrent type_c_connection_rp_current(UsbPdCcState state) {
    switch(state) {
    case UsbPdCcStateRp1A5:
        return UsbPdRpCurrent1A5;
    case UsbPdCcStateRp3A0:
        return UsbPdRpCurrent3A0;
    default:
        return UsbPdRpCurrentDefault;
    }
}

static uint32_t type_c_connection_time_left(uint32_t now, uint32_t since, uint32_t duration) {
    uint32_t elapsed = now - since;

    return (elapsed >= duration) ? 0u : (duration - elapsed);
}

static void type_c_connection_apply_termination(
    TypeCConnection* connection,
    uint8_t port,
    TypeCConnectionTermination termination) {
    connection->termination = termination;
    switch(termination) {
    case TypeCConnectionTerminationRd:
        usb_pd_platform_set_rd(port);
        break;
    case TypeCConnectionTerminationRp:
        connection->presented_rp_current = connection->rp_current;
        usb_pd_platform_set_rp(port, connection->rp_current);
        break;
    default:
        usb_pd_platform_set_cc_open(port);
        break;
    }
}

static void type_c_connection_change_termination(
    TypeCConnection* connection,
    uint8_t port,
    TypeCConnectionTermination termination) {
    if((connection->termination != termination) ||
       ((termination == TypeCConnectionTerminationRp) &&
        (connection->presented_rp_current != connection->rp_current))) {
        type_c_connection_apply_termination(connection, port, termination);
    }
}

static void type_c_connection_enter_state(
    TypeCConnection* connection,
    uint8_t port,
    TypeCConnectionState state,
    uint32_t now) {
    connection->state = state;
    connection->state_entered_ms = now;
    connection->partner_observed = false;

    switch(state) {
    case TypeCConnectionStateDisabled:
    case TypeCConnectionStateErrorRecovery:
        type_c_connection_change_termination(connection, port, TypeCConnectionTerminationOpen);
        break;
    case TypeCConnectionStateUnattachedSink:
    case TypeCConnectionStateTrySink:
        type_c_connection_change_termination(connection, port, TypeCConnectionTerminationRd);
        break;
    case TypeCConnectionStateUnattachedSource:
    case TypeCConnectionStateTryWaitSource:
        type_c_connection_change_termination(connection, port, TypeCConnectionTerminationRp);
        break;
    default:
        break;
    }
}

static void
    type_c_connection_enter_unattached(TypeCConnection* connection, uint8_t port, uint32_t now) {
    TypeCConnectionState state = TypeCConnectionStateUnattachedSink;

    if(connection->mode == UsbPdPortModeDisabled) {
        state = TypeCConnectionStateDisabled;
    } else if(connection->mode == UsbPdPortModeSource) {
        state = TypeCConnectionStateUnattachedSource;
    }
    type_c_connection_enter_state(connection, port, state, now);
}

static void type_c_connection_notify(
    TypeCConnection* connection,
    uint8_t port,
    const TypeCConnectionEvent* event) {
    if(connection->callback != NULL) {
        connection->callback(connection->context, port, event);
    }
}

static void type_c_connection_attach(
    TypeCConnection* connection,
    uint8_t port,
    UsbPdPowerRole power_role,
    const TypeCConnectionPartner* partner,
    uint32_t now) {
    TypeCConnectionEvent event = {
        .type = TypeCConnectionEventTypeAttached,
        .power_role = power_role,
        .pin = partner->pin,
        .partner_rp_current = type_c_connection_rp_current(partner->state),
        .vconn_required = partner->vconn_required,
    };

    type_c_connection_enter_state(
        connection,
        port,
        (power_role == UsbPdPowerRoleSource) ? TypeCConnectionStateAttachedSource :
                                               TypeCConnectionStateAttachedSink,
        now);
    connection->attached = true;
    connection->power_role = power_role;
    connection->pin = partner->pin;
    usb_pd_platform_start_communication(port, partner->pin);
    type_c_connection_notify(connection, port, &event);
}

static void type_c_connection_detach(TypeCConnection* connection, uint8_t port) {
    TypeCConnectionEvent event = {
        .type = TypeCConnectionEventTypeDetached,
        .power_role = connection->power_role,
        .pin = UsbPdCcPinNone,
        .partner_rp_current = UsbPdRpCurrentDefault,
        .vconn_required = false,
    };

    if(!connection->attached) {
        return;
    }
    usb_pd_platform_stop_communication(port);
    connection->attached = false;
    connection->swap_in_progress = false;
    connection->vbus_disconnect_detection = true;
    connection->pin = UsbPdCcPinNone;
    type_c_connection_notify(connection, port, &event);
}

static bool type_c_connection_read_partner(
    const TypeCConnection* connection,
    uint8_t port,
    TypeCConnectionPartner* partner) {
    UsbPdCcState cc1;
    UsbPdCcState cc2;
    UsbPdCcState cc1_again;
    UsbPdCcState cc2_again;

    usb_pd_platform_get_cc(port, &cc1, &cc2);
    usb_pd_platform_get_cc(port, &cc1_again, &cc2_again);
    if((cc1 != cc1_again) || (cc2 != cc2_again)) {
        return false;
    }

    partner->pin = UsbPdCcPinNone;
    partner->state = UsbPdCcStateOpen;
    partner->vconn_required = false;

    if(connection->termination == TypeCConnectionTerminationRd) {
        if(type_c_connection_is_rp(cc1) && (cc2 == UsbPdCcStateOpen)) {
            partner->pin = UsbPdCcPin1;
            partner->state = cc1;
        } else if((cc1 == UsbPdCcStateOpen) && type_c_connection_is_rp(cc2)) {
            partner->pin = UsbPdCcPin2;
            partner->state = cc2;
        }
    } else if(connection->termination == TypeCConnectionTerminationRp) {
        if((cc1 == UsbPdCcStateRd) && type_c_connection_is_open_or_cable(cc2)) {
            partner->pin = UsbPdCcPin1;
            partner->state = cc1;
            partner->vconn_required = (cc2 == UsbPdCcStateRa);
        } else if(type_c_connection_is_open_or_cable(cc1) && (cc2 == UsbPdCcStateRd)) {
            partner->pin = UsbPdCcPin2;
            partner->state = cc2;
            partner->vconn_required = (cc1 == UsbPdCcStateRa);
        }
    }
    return true;
}

static bool type_c_connection_read_active_pin(
    const TypeCConnection* connection,
    uint8_t port,
    UsbPdCcState* state) {
    UsbPdCcState cc1;
    UsbPdCcState cc2;
    UsbPdCcState cc1_again;
    UsbPdCcState cc2_again;

    usb_pd_platform_get_cc(port, &cc1, &cc2);
    usb_pd_platform_get_cc(port, &cc1_again, &cc2_again);
    if(connection->pin == UsbPdCcPin1) {
        *state = cc1;
        return cc1 == cc1_again;
    }
    *state = cc2;
    return cc2 == cc2_again;
}

static uint32_t type_c_connection_observe(
    TypeCConnection* connection,
    const TypeCConnectionPartner* partner,
    uint32_t now) {
    const TypeCConnectionPartner* observed = &connection->observed_partner;

    if(!connection->partner_observed || (observed->pin != partner->pin) ||
       (observed->state != partner->state) ||
       (observed->vconn_required != partner->vconn_required)) {
        connection->partner_observed = true;
        connection->observed_partner = *partner;
        connection->partner_stable_since_ms = now;
    }
    return now - connection->partner_stable_since_ms;
}

static uint32_t type_c_connection_run_unattached(
    TypeCConnection* connection,
    uint8_t port,
    uint32_t now,
    TypeCConnectionState attach_wait_state,
    TypeCConnectionState toggle_state,
    uint32_t toggle_ms) {
    TypeCConnectionPartner partner;
    uint32_t left;

    if(!type_c_connection_read_partner(connection, port, &partner)) {
        return TYPE_C_CONNECTION_POLL_MS;
    }
    if(partner.pin != UsbPdCcPinNone) {
        type_c_connection_enter_state(connection, port, attach_wait_state, now);
        return TYPE_C_CONNECTION_POLL_MS;
    }
    if(!type_c_connection_is_toggling(connection->mode)) {
        return TYPE_C_CONNECTION_WAIT_FOREVER;
    }
    left = type_c_connection_time_left(now, connection->state_entered_ms, toggle_ms);
    if(left == 0u) {
        type_c_connection_enter_state(connection, port, toggle_state, now);
        return TYPE_C_CONNECTION_POLL_MS;
    }
    return left;
}

static uint32_t type_c_connection_run_attach_wait_sink(
    TypeCConnection* connection,
    uint8_t port,
    uint32_t now) {
    TypeCConnectionPartner partner;
    uint32_t stable_ms;

    if(!type_c_connection_read_partner(connection, port, &partner)) {
        return TYPE_C_CONNECTION_POLL_MS;
    }
    stable_ms = type_c_connection_observe(connection, &partner, now);
    if(partner.pin == UsbPdCcPinNone) {
        if(stable_ms >= TYPE_C_CONNECTION_PD_DEBOUNCE_MS) {
            type_c_connection_enter_state(
                connection,
                port,
                type_c_connection_is_toggling(connection->mode) ?
                    TypeCConnectionStateUnattachedSource :
                    TypeCConnectionStateUnattachedSink,
                now);
        }
        return TYPE_C_CONNECTION_POLL_MS;
    }
    if((stable_ms >= TYPE_C_CONNECTION_CC_DEBOUNCE_MS) &&
       (usb_pd_platform_get_vbus_level(port) >= UsbPdVbusLevelPresent)) {
        type_c_connection_attach(connection, port, UsbPdPowerRoleSink, &partner, now);
        return TYPE_C_CONNECTION_WAIT_FOREVER;
    }
    return TYPE_C_CONNECTION_POLL_MS;
}

static uint32_t
    type_c_connection_run_attached_sink(TypeCConnection* connection, uint8_t port, uint32_t now) {
    TypeCConnectionPartner partner = {
        .pin = UsbPdCcPinNone,
        .state = UsbPdCcStateOpen,
        .vconn_required = false,
    };
    uint32_t stable_ms;

    if(!type_c_connection_read_active_pin(connection, port, &partner.state)) {
        return TYPE_C_CONNECTION_POLL_MS;
    }
    if((partner.state != UsbPdCcStateOpen) &&
       (!connection->vbus_disconnect_detection ||
        (usb_pd_platform_get_vbus_level(port) >= UsbPdVbusLevelPresent))) {
        partner.pin = connection->pin;
    }
    stable_ms = type_c_connection_observe(connection, &partner, now);
    if(partner.pin != UsbPdCcPinNone) {
        return TYPE_C_CONNECTION_WAIT_FOREVER;
    }
    if(stable_ms >= TYPE_C_CONNECTION_PD_DEBOUNCE_MS) {
        type_c_connection_detach(connection, port);
        type_c_connection_enter_unattached(connection, port, now);
    }
    return TYPE_C_CONNECTION_POLL_MS;
}

static uint32_t type_c_connection_run_attach_wait_source(
    TypeCConnection* connection,
    uint8_t port,
    uint32_t now) {
    TypeCConnectionPartner partner;
    uint32_t stable_ms;

    if(!type_c_connection_read_partner(connection, port, &partner)) {
        return TYPE_C_CONNECTION_POLL_MS;
    }
    stable_ms = type_c_connection_observe(connection, &partner, now);
    if(partner.pin == UsbPdCcPinNone) {
        if(stable_ms >= TYPE_C_CONNECTION_SRC_DISCONNECT_MS) {
            type_c_connection_enter_unattached(connection, port, now);
        }
        return TYPE_C_CONNECTION_POLL_MS;
    }
    if(stable_ms < TYPE_C_CONNECTION_CC_DEBOUNCE_MS) {
        return TYPE_C_CONNECTION_POLL_MS;
    }
    if(connection->mode == UsbPdPortModeDualRoleTrySink) {
        type_c_connection_enter_state(connection, port, TypeCConnectionStateTrySink, now);
        return TYPE_C_CONNECTION_DRP_TRY_MS;
    }
    if(usb_pd_platform_get_vbus_level(port) == UsbPdVbusLevelSafe0V) {
        type_c_connection_attach(connection, port, UsbPdPowerRoleSource, &partner, now);
        return TYPE_C_CONNECTION_WAIT_FOREVER;
    }
    return TYPE_C_CONNECTION_POLL_MS;
}

static uint32_t
    type_c_connection_run_attached_source(TypeCConnection* connection, uint8_t port, uint32_t now) {
    TypeCConnectionPartner partner = {
        .pin = UsbPdCcPinNone,
        .state = UsbPdCcStateOpen,
        .vconn_required = false,
    };
    uint32_t stable_ms;

    if(!type_c_connection_read_active_pin(connection, port, &partner.state)) {
        return TYPE_C_CONNECTION_POLL_MS;
    }
    if(partner.state == UsbPdCcStateRd) {
        partner.pin = connection->pin;
    }
    stable_ms = type_c_connection_observe(connection, &partner, now);
    if(partner.pin != UsbPdCcPinNone) {
        return TYPE_C_CONNECTION_WAIT_FOREVER;
    }
    if(stable_ms >= TYPE_C_CONNECTION_SRC_DISCONNECT_MS) {
        type_c_connection_detach(connection, port);
        type_c_connection_enter_unattached(connection, port, now);
    }
    return TYPE_C_CONNECTION_POLL_MS;
}

static uint32_t
    type_c_connection_run_try_sink(TypeCConnection* connection, uint8_t port, uint32_t now) {
    TypeCConnectionPartner partner;
    uint32_t left;
    uint32_t stable_ms;

    left = type_c_connection_time_left(
        now, connection->state_entered_ms, TYPE_C_CONNECTION_DRP_TRY_MS);
    if(left != 0u) {
        return left;
    }
    if(!type_c_connection_read_partner(connection, port, &partner)) {
        return TYPE_C_CONNECTION_POLL_MS;
    }
    stable_ms = type_c_connection_observe(connection, &partner, now);
    if(stable_ms < TYPE_C_CONNECTION_TRY_CC_DEBOUNCE_MS) {
        return TYPE_C_CONNECTION_POLL_MS;
    }
    if(partner.pin == UsbPdCcPinNone) {
        type_c_connection_enter_state(connection, port, TypeCConnectionStateTryWaitSource, now);
        return TYPE_C_CONNECTION_POLL_MS;
    }
    if(usb_pd_platform_get_vbus_level(port) >= UsbPdVbusLevelPresent) {
        type_c_connection_attach(connection, port, UsbPdPowerRoleSink, &partner, now);
        return TYPE_C_CONNECTION_WAIT_FOREVER;
    }
    return TYPE_C_CONNECTION_POLL_MS;
}

static uint32_t
    type_c_connection_run_try_wait_source(TypeCConnection* connection, uint8_t port, uint32_t now) {
    TypeCConnectionPartner partner;
    uint32_t stable_ms;

    if(!type_c_connection_read_partner(connection, port, &partner)) {
        return TYPE_C_CONNECTION_POLL_MS;
    }
    stable_ms = type_c_connection_observe(connection, &partner, now);
    if(partner.pin == UsbPdCcPinNone) {
        if(stable_ms >= TYPE_C_CONNECTION_DRP_TRY_MS) {
            type_c_connection_enter_state(
                connection, port, TypeCConnectionStateUnattachedSink, now);
        }
        return TYPE_C_CONNECTION_POLL_MS;
    }
    if((stable_ms >= TYPE_C_CONNECTION_TRY_CC_DEBOUNCE_MS) &&
       (usb_pd_platform_get_vbus_level(port) == UsbPdVbusLevelSafe0V)) {
        type_c_connection_attach(connection, port, UsbPdPowerRoleSource, &partner, now);
        return TYPE_C_CONNECTION_WAIT_FOREVER;
    }
    return TYPE_C_CONNECTION_POLL_MS;
}

static uint32_t
    type_c_connection_run_error_recovery(TypeCConnection* connection, uint8_t port, uint32_t now) {
    uint32_t left = type_c_connection_time_left(
        now, connection->state_entered_ms, TYPE_C_CONNECTION_ERROR_RECOVERY_MS);

    if(left != 0u) {
        return left;
    }
    type_c_connection_enter_unattached(connection, port, now);
    return TYPE_C_CONNECTION_POLL_MS;
}

static void
    type_c_connection_apply_requests(TypeCConnection* connection, uint8_t port, uint32_t now) {
    if(connection->mode_change_pending) {
        connection->mode_change_pending = false;
        connection->mode = connection->requested_mode;
        if(connection->state != TypeCConnectionStateErrorRecovery) {
            if(connection->attached && (connection->mode != UsbPdPortModeDisabled)) {
                type_c_connection_detach(connection, port);
                type_c_connection_enter_state(
                    connection, port, TypeCConnectionStateErrorRecovery, now);
            } else {
                type_c_connection_detach(connection, port);
                type_c_connection_enter_unattached(connection, port, now);
            }
        }
    }
    if(connection->error_recovery_pending) {
        connection->error_recovery_pending = false;
        if(connection->mode != UsbPdPortModeDisabled) {
            type_c_connection_detach(connection, port);
            type_c_connection_enter_state(
                connection, port, TypeCConnectionStateErrorRecovery, now);
        }
    }
    if(connection->rp_current_change_pending) {
        connection->rp_current_change_pending = false;
        connection->rp_current = connection->requested_rp_current;
        if((connection->termination == TypeCConnectionTerminationRp) && !connection->attached) {
            type_c_connection_apply_termination(connection, port, TypeCConnectionTerminationRp);
        }
    }
}

void type_c_connection_init(
    uint8_t port,
    UsbPdPortMode mode,
    UsbPdRpCurrent rp_current,
    TypeCConnectionCallback callback,
    void* context) {
    TypeCConnection* connection = &type_c_connections[port];
    TypeCConnectionTermination termination = TypeCConnectionTerminationRd;

    memset(connection, 0, sizeof(TypeCConnection));
    connection->mode = mode;
    connection->requested_mode = mode;
    connection->rp_current = rp_current;
    connection->requested_rp_current = rp_current;
    connection->pin = UsbPdCcPinNone;
    connection->vbus_disconnect_detection = true;
    connection->callback = callback;
    connection->context = context;

    if(mode == UsbPdPortModeDisabled) {
        termination = TypeCConnectionTerminationOpen;
    } else if(mode == UsbPdPortModeSource) {
        termination = TypeCConnectionTerminationRp;
    }
    type_c_connection_apply_termination(connection, port, termination);
    type_c_connection_enter_unattached(connection, port, usb_pd_platform_get_tick_ms());
}

void type_c_connection_set_mode(uint8_t port, UsbPdPortMode mode) {
    TypeCConnection* connection = &type_c_connections[port];

    connection->requested_mode = mode;
    connection->mode_change_pending = true;
}

void type_c_connection_set_rp_current(uint8_t port, UsbPdRpCurrent current) {
    TypeCConnection* connection = &type_c_connections[port];

    connection->requested_rp_current = current;
    connection->rp_current_change_pending = true;
}

void type_c_connection_enter_error_recovery(uint8_t port) {
    type_c_connections[port].error_recovery_pending = true;
}

void type_c_connection_set_swap_in_progress(uint8_t port, bool in_progress) {
    type_c_connections[port].swap_in_progress = in_progress;
}

void type_c_connection_set_vbus_disconnect_detection(uint8_t port, bool enabled) {
    type_c_connections[port].vbus_disconnect_detection = enabled;
}

void type_c_connection_swap_power_role(uint8_t port, UsbPdPowerRole power_role) {
    TypeCConnection* connection = &type_c_connections[port];
    uint32_t now = usb_pd_platform_get_tick_ms();

    if(!connection->attached) {
        return;
    }
    if(power_role == UsbPdPowerRoleSource) {
        type_c_connection_change_termination(connection, port, TypeCConnectionTerminationRp);
        type_c_connection_enter_state(connection, port, TypeCConnectionStateAttachedSource, now);
    } else {
        type_c_connection_change_termination(connection, port, TypeCConnectionTerminationRd);
        type_c_connection_enter_state(connection, port, TypeCConnectionStateAttachedSink, now);
    }
    connection->power_role = power_role;
}

uint32_t type_c_connection_process(uint8_t port) {
    TypeCConnection* connection = &type_c_connections[port];
    uint32_t now = usb_pd_platform_get_tick_ms();

    type_c_connection_apply_requests(connection, port, now);
    if(connection->swap_in_progress && connection->attached) {
        return TYPE_C_CONNECTION_POLL_MS;
    }

    switch(connection->state) {
    case TypeCConnectionStateErrorRecovery:
        return type_c_connection_run_error_recovery(connection, port, now);
    case TypeCConnectionStateUnattachedSink:
        return type_c_connection_run_unattached(
            connection,
            port,
            now,
            TypeCConnectionStateAttachWaitSink,
            TypeCConnectionStateUnattachedSource,
            TYPE_C_CONNECTION_DRP_SINK_MS);
    case TypeCConnectionStateAttachWaitSink:
        return type_c_connection_run_attach_wait_sink(connection, port, now);
    case TypeCConnectionStateAttachedSink:
        return type_c_connection_run_attached_sink(connection, port, now);
    case TypeCConnectionStateUnattachedSource:
        return type_c_connection_run_unattached(
            connection,
            port,
            now,
            TypeCConnectionStateAttachWaitSource,
            TypeCConnectionStateUnattachedSink,
            TYPE_C_CONNECTION_DRP_SOURCE_MS);
    case TypeCConnectionStateAttachWaitSource:
        return type_c_connection_run_attach_wait_source(connection, port, now);
    case TypeCConnectionStateAttachedSource:
        return type_c_connection_run_attached_source(connection, port, now);
    case TypeCConnectionStateTrySink:
        return type_c_connection_run_try_sink(connection, port, now);
    case TypeCConnectionStateTryWaitSource:
        return type_c_connection_run_try_wait_source(connection, port, now);
    default:
        return TYPE_C_CONNECTION_WAIT_FOREVER;
    }
}
