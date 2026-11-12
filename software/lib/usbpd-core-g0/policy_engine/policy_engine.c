#include "policy_engine/policy_engine_i.h"

#define POLICY_ENGINE_FIXED_PDO_FRS_POSITION 23U
#define POLICY_ENGINE_FIXED_PDO_FRS_MASK     (0x3U << POLICY_ENGINE_FIXED_PDO_FRS_POSITION)

PolicyEngine policy_engines[USB_PD_PORT_COUNT];

void policy_engine_init(
    uint8_t port,
    const UsbPdPortConfig* config,
    const UsbPdCallbacks* callbacks,
    void* context) {
    PolicyEngine* pe = &policy_engines[port];

    memset(pe, 0, sizeof(PolicyEngine));
    pe->port = port;
    pe->config = *config;
    pe->callbacks = callbacks;
    pe->context = context;
    pe->spec_revision = config->spec_revision;
    pe->state = PolicyEngineStateReset;
}

void policy_engine_attach(uint8_t port, UsbPdPowerRole power_role, UsbPdCcPin pin) {
    PolicyEngine* pe = &policy_engines[port];

    pe->power_role = power_role;
    pe->data_role = (power_role == UsbPdPowerRoleSource) ? UsbPdDataRoleDfp : UsbPdDataRoleUfp;
    pe->pin = pin;
    pe->vconn_source = false;
    pe->sourcing_vbus = false;
    pe->swap_ongoing = false;
    pe->hard_reset_in_progress = false;
    pe->state = PolicyEngineStateReset;
    pe->connected = true;
    policy_engine_wakeup(port);
}

void policy_engine_detach(uint8_t port) {
    PolicyEngine* pe = &policy_engines[port];

    pe->connected = false;
    pe->fast_role_swap_signal_allowed = false;
    protocol_layer_disable_receive(port);
    if(pe->sourcing_vbus) {
        policy_engine_set_power(pe, UsbPdPowerSourceOff);
    }
    if(pe->fast_role_swap_armed) {
        pe->fast_role_swap_armed = false;
        usb_pd_platform_set_fast_role_swap_detection(port, false);
    }
    pe->hard_reset_in_progress = false;
    policy_engine_disable_vconn_if_sourcing(pe);
    policy_engine_discard_requests(pe);
    pe->swap_ongoing = false;
    pe->power = PolicyEnginePowerNone;
    pe->pin = UsbPdCcPinNone;
    pe->state = PolicyEngineStateReset;
}

uint32_t policy_engine_process(uint8_t port) {
    PolicyEngine* pe = &policy_engines[port];
    PolicyEngineRxEvent event;
    uint32_t next_call_ms;

    if(!pe->connected) {
        return POLICY_ENGINE_NEXT_CALL_IDLE;
    }
    pe->now_ms = usb_pd_platform_get_tick_ms();
    if(pe->state == PolicyEngineStateReset) {
        if(pe->power_role == UsbPdPowerRoleSource) {
            policy_engine_role_enter_startup(
                pe, UsbPdPowerRoleSource, UsbPdDataRoleDfp, PolicyEngineStateSourceStartup);
        } else {
            policy_engine_role_enter_startup(
                pe, UsbPdPowerRoleSink, UsbPdDataRoleUfp, PolicyEngineStateSinkStartup);
        }
    }

    event = policy_engine_receive_queue_get(pe);
    if(event == PolicyEngineRxEventBusy) {
        return POLICY_ENGINE_NEXT_CALL_SOON;
    }

    do {
        if(!pe->connected) {
            return POLICY_ENGINE_NEXT_CALL_IDLE;
        }
        pe->now_ms = usb_pd_platform_get_tick_ms();
        if(pe->power_role == UsbPdPowerRoleSource) {
            policy_engine_source_handle_pending_requests(pe, &event);
            policy_engine_role_reject_wrong_data_role(pe, &event);
            next_call_ms = policy_engine_source_run_state(pe, &event);
        } else {
            policy_engine_sink_handle_pending_requests(pe, &event);
            policy_engine_role_reject_wrong_data_role(pe, &event);
            next_call_ms = policy_engine_sink_run_state(pe, &event);
        }
        next_call_ms = policy_engine_role_finish_step(pe, &event, next_call_ms);
    } while(next_call_ms == POLICY_ENGINE_NEXT_CALL_NOW);

    return next_call_ms;
}

void policy_engine_power_ready(uint8_t port) {
    policy_engines[port].power_ready = true;
    policy_engine_wakeup(port);
}

bool policy_engine_signal_fast_role_swap(uint8_t port) {
    PolicyEngine* pe = &policy_engines[port];

    if(!pe->connected || !pe->fast_role_swap_signal_allowed || pe->fast_role_swap_signalled ||
       pe->swap_ongoing || (pe->power != PolicyEnginePowerExplicitContract) ||
       (pe->power_role != UsbPdPowerRoleSource)) {
        return false;
    }
    usb_pd_platform_send_fast_role_swap(port);
    pe->fast_role_swap_signalled_ms = usb_pd_platform_get_tick_ms();
    pe->fast_role_swap_signalled = true;
    policy_engine_set_swap_ongoing(pe, true);
    policy_engine_wakeup(port);
    return true;
}

bool policy_engine_on_fast_role_swap_received(uint8_t port) {
    PolicyEngine* pe = &policy_engines[port];

    if(!pe->connected || !pe->fast_role_swap_armed ||
       (pe->current_ams == PolicyEngineAmsFastRoleSwap) ||
       (pe->power != PolicyEnginePowerExplicitContract) ||
       (pe->power_role != UsbPdPowerRoleSink)) {
        return false;
    }
    policy_engine_set_swap_ongoing(pe, true);
    pe->fast_role_swap_received = true;
    policy_engine_wakeup(port);
    return true;
}

void policy_engine_get_port_status(uint8_t port, UsbPdPortStatus* status) {
    PolicyEngine* pe = &policy_engines[port];

    status->attached = pe->connected;
    status->explicit_contract = (pe->power == PolicyEnginePowerExplicitContract);
    status->power_role = pe->power_role;
    status->data_role = pe->data_role;
    status->vconn_source = pe->vconn_source;
    status->spec_revision = pe->spec_revision;
    status->pin = pe->pin;
    status->contract.pdo = pe->contract_pdo;
    status->contract.rdo = pe->rdo;
}

bool policy_engine_is_connected(uint8_t port) {
    return policy_engines[port].connected;
}

void policy_engine_wakeup(uint8_t port) {
    PolicyEngine* pe = &policy_engines[port];

    pe->callbacks->wakeup(pe->context);
}

void policy_engine_on_hard_reset_received(uint8_t port) {
    PolicyEngine* pe = &policy_engines[port];

    policy_engine_set_swap_ongoing(pe, false);
    pe->hard_reset_pending = true;
    pe->rx_event_slot = 0u;
    policy_engine_wakeup(port);
}

void policy_engine_timer_start(PolicyEngine* pe, PolicyEngineTimer* timer, uint32_t duration_ms) {
    pe->now_ms = usb_pd_platform_get_tick_ms();
    policy_engine_timer_start_at(timer, pe->now_ms, duration_ms);
}

void policy_engine_timer_start_at(
    PolicyEngineTimer* timer,
    uint32_t start_ms,
    uint32_t duration_ms) {
    timer->deadline_ms = start_ms + duration_ms;
    timer->running = true;
}

void policy_engine_timer_stop(PolicyEngineTimer* timer) {
    timer->running = false;
}

bool policy_engine_timer_is_expired(const PolicyEngine* pe, const PolicyEngineTimer* timer) {
    return timer->running && ((int32_t)(pe->now_ms - timer->deadline_ms) >= 0);
}

bool policy_engine_timer_is_counting(const PolicyEngine* pe, const PolicyEngineTimer* timer) {
    return timer->running && ((int32_t)(pe->now_ms - timer->deadline_ms) < 0);
}

uint32_t policy_engine_timer_remaining(const PolicyEngine* pe, const PolicyEngineTimer* timer) {
    if(!policy_engine_timer_is_counting(pe, timer)) {
        return 0u;
    }
    return timer->deadline_ms - pe->now_ms;
}

void policy_engine_notify(PolicyEngine* pe, const UsbPdEvent* event) {
    pe->callbacks->on_event(pe->context, pe->port, event);
}

void policy_engine_notify_type(PolicyEngine* pe, UsbPdEventType type) {
    UsbPdEvent event;

    memset(&event, 0, sizeof(event));
    event.type = type;
    policy_engine_notify(pe, &event);
}

void policy_engine_answer_request(
    PolicyEngine* pe,
    UsbPdRequest request,
    UsbPdRequestResult result) {
    UsbPdEvent event;

    if(!pe->request_pending || (pe->active_request != request)) {
        return;
    }
    pe->request_pending = false;
    memset(&event, 0, sizeof(event));
    event.type = UsbPdEventTypeRequestAnswered;
    event.request_answer.request = request;
    event.request_answer.result = result;
    policy_engine_notify(pe, &event);
}

void policy_engine_set_power(PolicyEngine* pe, UsbPdPower power) {
    if(power == UsbPdPowerSourceNewContract) {
        pe->power_ready = false;
    }
    if((power == UsbPdPowerSourceOn) || (power == UsbPdPowerSourceNewContract)) {
        pe->sourcing_vbus = true;
    } else if(power == UsbPdPowerSourceOff) {
        pe->sourcing_vbus = false;
    }
    pe->callbacks->set_power(pe->context, pe->port, power);
}

void policy_engine_set_vconn(PolicyEngine* pe, bool enabled) {
    pe->vconn_source = enabled;
    pe->callbacks->set_vconn(pe->context, pe->port, enabled);
}

void policy_engine_set_swap_ongoing(PolicyEngine* pe, bool ongoing) {
    pe->swap_ongoing = ongoing;
    type_c_connection_set_swap_in_progress(pe->port, ongoing);
}

void policy_engine_set_hard_reset_in_progress(PolicyEngine* pe, bool in_progress) {
    pe->hard_reset_in_progress = in_progress;
    policy_engine_update_vbus_detection(pe);
}

void policy_engine_update_vbus_detection(PolicyEngine* pe) {
    bool enabled = !pe->hard_reset_in_progress && !pe->fast_role_swap_armed;

    type_c_connection_set_vbus_disconnect_detection(pe->port, enabled);
    if(enabled) {
        policy_engine_wakeup(pe->port);
    }
}

void policy_engine_disarm_fast_role_swap(PolicyEngine* pe) {
    if(pe->fast_role_swap_armed) {
        pe->fast_role_swap_armed = false;
        usb_pd_platform_set_fast_role_swap_detection(pe->port, false);
        policy_engine_update_vbus_detection(pe);
    }
}

void policy_engine_stop_fast_role_swap(PolicyEngine* pe) {
    pe->fast_role_swap_signal_allowed = false;
    policy_engine_disarm_fast_role_swap(pe);
}

void policy_engine_drop_dpm_request(PolicyEngine* pe) {
    PolicyEngineState dpm_request = pe->dpm_request;
    UsbPdRequest request = pe->dpm_request_id;
    bool answerable = pe->dpm_request_answerable;
    UsbPdEvent event;

    pe->dpm_request = PolicyEngineStateDisabled;
    if((dpm_request == PolicyEngineStateDisabled) || !answerable) {
        return;
    }
    memset(&event, 0, sizeof(event));
    event.type = UsbPdEventTypeRequestAnswered;
    event.request_answer.request = request;
    event.request_answer.result = UsbPdRequestResultDiscarded;
    policy_engine_notify(pe, &event);
}

void policy_engine_discard_requests(PolicyEngine* pe) {
    if(pe->request_pending) {
        policy_engine_answer_request(pe, pe->active_request, UsbPdRequestResultDiscarded);
    }
    policy_engine_drop_dpm_request(pe);
}

void policy_engine_start_dpm_request(PolicyEngine* pe) {
    PolicyEngineState dpm_request = pe->dpm_request;
    UsbPdRequest request = pe->dpm_request_id;
    bool answerable = pe->dpm_request_answerable;

    if(pe->request_pending) {
        policy_engine_answer_request(pe, pe->active_request, UsbPdRequestResultDiscarded);
    }
    pe->dpm_request = PolicyEngineStateDisabled;
    pe->state = dpm_request;
    pe->flags.dpm_request_ongoing = 1U;
    if(answerable) {
        pe->request_pending = true;
        pe->active_request = request;
    }
}

bool policy_engine_is_dual_role(const PolicyEngine* pe) {
    return pe->config.power_role_swap && ((pe->config.mode == UsbPdPortModeDualRole) ||
                                          (pe->config.mode == UsbPdPortModeDualRoleTrySink));
}

bool policy_engine_vbus_is(const PolicyEngine* pe, UsbPdVbusLevel level) {
    return usb_pd_platform_get_vbus_level(pe->port) == level;
}

uint32_t policy_engine_capabilities_object(const UsbPdCapabilities* capabilities, uint32_t rdo) {
    uint32_t position = (rdo & USB_PD_RDO_OBJECT_POSITION_MASK) >>
                        USB_PD_RDO_OBJECT_POSITION_POSITION;

    if((position == 0u) || (position > capabilities->count)) {
        return 0u;
    }
    return capabilities->objects[position - 1u];
}

bool policy_engine_pdo_is_pps(uint32_t pdo) {
    return (((pdo & USB_PD_PDO_TYPE_MASK) >> USB_PD_PDO_TYPE_POSITION) == UsbPdPdoTypeAugmented) &&
           (((pdo & USB_PD_APDO_TYPE_MASK) >> USB_PD_APDO_TYPE_POSITION) == USB_PD_APDO_TYPE_PPS);
}

bool policy_engine_sink_capabilities_request_fast_role_swap(const UsbPdCapabilities* capabilities) {
    return (capabilities->count != 0u) &&
           ((capabilities->objects[0] & POLICY_ENGINE_FIXED_PDO_FRS_MASK) != 0u);
}

void policy_engine_read_capabilities(PolicyEngine* pe, UsbPdCapabilities* capabilities) {
    uint8_t index;

    capabilities->count = (uint8_t)POLICY_ENGINE_HEADER_DATA_OBJECT_COUNT(pe->rx_header);
    for(index = 0u; index < capabilities->count; index++) {
        capabilities->objects[index] = usb_pd_message_read_u32(
            &pe->rx_message[USB_PD_MESSAGE_HEADER_SIZE + (index * USB_PD_DATA_OBJECT_SIZE)]);
    }
}

void policy_engine_store_source_capabilities(PolicyEngine* pe) {
    UsbPdEvent event;

    policy_engine_read_capabilities(pe, &pe->source_capabilities);
    memset(&event, 0, sizeof(event));
    event.type = UsbPdEventTypeSourceCapabilities;
    event.capabilities = &pe->source_capabilities;
    policy_engine_notify(pe, &event);
}

void policy_engine_send_hard_reset(PolicyEngine* pe) {
    uint32_t interrupts;

    protocol_layer_send_hard_reset(pe->port);
    interrupts = usb_pd_platform_disable_interrupts();
    pe->rx_event_slot = 0u;
    usb_pd_platform_restore_interrupts(interrupts);
}

void policy_engine_set_power_role(PolicyEngine* pe, UsbPdPowerRole role) {
    pe->power_role = role;
    protocol_layer_set_header_power_role(pe->port, role);
}

bool policy_engine_end_ams_on_not_supported(PolicyEngine* pe, PolicyEngineRxEvent* event) {
    if((*event != PolicyEngineRxEventMessage) ||
       (policy_engine_get_control_message(pe) != UsbPdControlMessageNotSupported)) {
        return false;
    }
    policy_engine_consume_message(pe, event, PolicyEngineStateReady);
    return true;
}

void policy_engine_reset_after_hard_reset(PolicyEngine* pe) {
    UsbPdPowerRole power_role;
    UsbPdDataRole data_role;

    policy_engine_reset_session(pe);

    power_role = pe->power_role;
    data_role = (power_role == UsbPdPowerRoleSource) ? UsbPdDataRoleDfp : UsbPdDataRoleUfp;
    protocol_layer_set_header(pe->port, power_role, data_role, pe->config.spec_revision);
    pe->data_role = data_role;

    pe->spec_revision = pe->config.spec_revision;
    protocol_layer_reset(pe->port);
    policy_engine_set_hard_reset_in_progress(pe, false);
}

void policy_engine_reset_session(PolicyEngine* pe) {
    uint32_t interrupts;

    policy_engine_discard_requests(pe);
    policy_engine_stop_fast_role_swap(pe);
    policy_engine_set_power_state(pe, PolicyEnginePowerNone);
    interrupts = usb_pd_platform_disable_interrupts();
    policy_engine_set_swap_ongoing(pe, false);
    memset(
        &pe->flags, 0, offsetof(PolicyEngine, hard_reset_counter) - offsetof(PolicyEngine, flags));
    usb_pd_platform_restore_interrupts(interrupts);
}

void policy_engine_reset_counters(PolicyEngine* pe) {
    pe->hard_reset_counter = 0u;
    pe->caps_counter = 0u;
}

void policy_engine_reset_state_machine(PolicyEngine* pe) {
    policy_engine_reset_session(pe);
    policy_engine_set_power_state(pe, PolicyEnginePowerDefault);
    protocol_layer_reset(pe->port);

    protocol_layer_set_header(pe->port, pe->power_role, pe->data_role, pe->spec_revision);
    if(pe->power_role == UsbPdPowerRoleSource) {
        pe->state = PolicyEngineStateSourceStartup;
    } else {
        pe->state = PolicyEngineStateSinkStartup;
    }
}

void policy_engine_adopt_spec_revision(PolicyEngine* pe, UsbPdSpecRevision spec_revision) {
    if(((spec_revision == UsbPdSpecRevision2) || (spec_revision == UsbPdSpecRevision3)) &&
       (spec_revision < pe->spec_revision)) {
        pe->spec_revision = spec_revision;
    }
    protocol_layer_set_header_spec_revision(pe->port, pe->spec_revision);
}

uint32_t policy_engine_get_nearest_timer_ms(PolicyEngine* pe) {
    if(policy_engine_timer_is_counting(pe, &pe->pps_timer)) {
        return policy_engine_timer_remaining(pe, &pe->pps_timer);
    }
    return POLICY_ENGINE_NEXT_CALL_IDLE;
}

void policy_engine_set_power_state(PolicyEngine* pe, PolicyEnginePower power) {
    pe->power = power;
}

void policy_engine_swap_data_role(PolicyEngine* pe) {
    UsbPdEvent event;

    pe->data_role = (pe->data_role == UsbPdDataRoleUfp) ? UsbPdDataRoleDfp : UsbPdDataRoleUfp;
    protocol_layer_set_header_data_role(pe->port, pe->data_role);
    memset(&event, 0, sizeof(event));
    event.type = UsbPdEventTypeDataRoleSwapped;
    event.data_role = pe->data_role;
    policy_engine_notify(pe, &event);
}

uint32_t policy_engine_start_state_timer(PolicyEngine* pe, uint32_t duration_ms) {
    policy_engine_timer_start(pe, &pe->state_timer, duration_ms);
    return duration_ms;
}

uint32_t policy_engine_start_sender_response_timer(PolicyEngine* pe) {
    uint32_t duration_ms = (pe->spec_revision == UsbPdSpecRevision3) ?
                               POLICY_ENGINE_T_SENDER_RESPONSE :
                               POLICY_ENGINE_T_SENDER_RESPONSE_PD2;

    return policy_engine_start_state_timer(pe, duration_ms);
}

PolicyEngineState policy_engine_hard_reset_give_up_state(PolicyEngine* pe) {
    if(pe->flags.pd_connected == 1U) {
        return PolicyEngineStateErrorRecovery;
    }
    return PolicyEngineStateDisabled;
}

void policy_engine_disable_vconn_if_sourcing(PolicyEngine* pe) {
    if(pe->vconn_source) {
        policy_engine_set_vconn(pe, false);
    }
}

bool policy_engine_is_busy(PolicyEngine* pe) {
    return (pe->dpm_request != PolicyEngineStateDisabled) || !pe->connected;
}
