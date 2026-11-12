#ifndef POLICY_ENGINE_I_H_
#define POLICY_ENGINE_I_H_

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include "message/usb_pd_message.h"
#include "usb_pd_platform.h"
#include "protocol_layer/protocol_layer.h"
#include "policy_engine/policy_engine.h"
#include "policy_engine/policy_engine_timing.h"
#include "type_c_connection/type_c_connection.h"

typedef enum {
    PolicyEngineStateDisabled = 0x00U,
    PolicyEngineStateSendNotSupported = 0x01U,
    PolicyEngineStateSourceStartup = 0x02U,
    PolicyEngineStateReady = 0x03U,
    PolicyEngineStateReadyIdle = 0x04U,
    PolicyEngineStateSourceWaitSwapSourceStart = 0x06U,
    PolicyEngineStateSourceSendCapabilities = 0x07U,
    PolicyEngineStateSourceDiscovery = 0x08U,
    PolicyEngineStateSourceNegotiateCapability = 0x09U,
    PolicyEngineStateSourceTransitionSupply = 0x0CU,
    PolicyEngineStateSourceTransitionSupplyPsRdy = 0x0DU,
    PolicyEngineStateSourceWaitNewCapabilities = 0x0EU,
    PolicyEngineStateHardReset = 0x0FU,
    PolicyEngineStateSoftReset = 0x10U,
    PolicyEngineStateSendSoftReset = 0x11U,
    PolicyEngineStateSourceSendSoftResetWaitAccept = 0x12U,
    PolicyEngineStateSourceTransitionToDefaultWaitVsafe5v = 0x13U,
    PolicyEngineStateHardResetReceived = 0x14U,
    PolicyEngineStateSourceTransitionToDefault = 0x15U,
    PolicyEngineStateSourceWaitRequest = 0x16U,
    PolicyEngineStateSourceTransitionToDefaultWaitVsafe0v = 0x17U,
    PolicyEngineStateGetSinkCap = 0x18U,
    PolicyEngineStateGetSinkCapWaitResponse = 0x19U,
    PolicyEngineStateGetSourceCap = 0x1AU,
    PolicyEngineStateGetSourceCapWaitResponse = 0x1BU,
    PolicyEngineStateGiveSinkCap = 0x1DU,
    PolicyEngineStatePowerRoleSwapSend = 0x1EU,
    PolicyEngineStatePowerRoleSwapSourceToSinkWaitResponse = 0x1FU,
    PolicyEngineStatePowerRoleSwapSourceToSinkTransitionToOff = 0x20U,
    PolicyEngineStatePowerRoleSwapSourceToSinkWaitSourceOn = 0x21U,
    PolicyEngineStatePowerRoleSwapEvaluate = 0x22U,
    PolicyEngineStatePowerRoleSwapSourceToSinkAccept = 0x24U,
    PolicyEngineStatePowerRoleSwapSourceToSinkComplete = 0x26U,
    PolicyEngineStateBistCarrierMode = 0x27U,
    PolicyEngineStateBistCarrierModeWaitExit = 0x28U,
    PolicyEngineStateBistTestData = 0x29U,
    PolicyEngineStateSendReject = 0x2AU,
    PolicyEngineStateSourceGivePpsStatus = 0x2DU,
    PolicyEngineStateSinkStartup = 0x3EU,
    PolicyEngineStateSinkDiscovery = 0x40U,
    PolicyEngineStateSinkWaitForCapabilities = 0x43U,
    PolicyEngineStateSinkEvaluateCapability = 0x44U,
    PolicyEngineStateSinkSelectCapability = 0x45U,
    PolicyEngineStateSinkSelectCapabilityWaitResponse = 0x46U,
    PolicyEngineStateSinkTransitionSink = 0x47U,
    PolicyEngineStateReset = 0x48U,
    PolicyEngineStateSinkReadyAfterWait = 0x49U,
    PolicyEngineStateGiveRevision = 0x4BU,
    PolicyEngineStateGiveSourceCap = 0x4CU,
    PolicyEngineStateSinkSendSoftResetWaitAccept = 0x50U,
    PolicyEngineStatePowerRoleSwapSinkToSourceSourceOn = 0x53U,
    PolicyEngineStatePowerRoleSwapSinkToSourceWaitResponse = 0x58U,
    PolicyEngineStatePowerRoleSwapSinkToSourceTransitionToOff = 0x59U,
    PolicyEngineStatePowerRoleSwapSinkToSourceComplete = 0x5BU,
    PolicyEngineStateGiveSinkCapExtended = 0x64U,
    PolicyEngineStateSinkGetPpsStatus = 0x65U,
    PolicyEngineStateSinkGetPpsStatusWaitResponse = 0x66U,
    PolicyEngineStateErrorRecovery = 0x92U,
    PolicyEngineStateDataRoleSwapEvaluate = 0x93U,
    PolicyEngineStateDataRoleSwapSend = 0x94U,
    PolicyEngineStateDataRoleSwapWaitResponse = 0x95U,
    PolicyEngineStateSinkTransitionToDefault = 0x9BU,
    PolicyEngineStateVconnSwapEvaluate = 0x9DU,
    PolicyEngineStateVconnSwapAccept = 0x9EU,
    PolicyEngineStateVconnSwapWaitForVconn = 0x9FU,
    PolicyEngineStateVconnSwapTurnOnVconn = 0xA0U,
    PolicyEngineStateVconnSwapSendPsRdy = 0xA1U,
    PolicyEngineStateVconnSwapSend = 0xA2U,
    PolicyEngineStateVconnSwapWaitResponse = 0xA3U,
    PolicyEngineStateVconnSwapForceVconn = 0xA4U,
    PolicyEngineStateFastRoleSwapSourceToSinkEvaluate = 0xA6U,
    PolicyEngineStateFastRoleSwapSinkToSourceSend = 0xA7U,
    PolicyEngineStateGiveSourceInfo = 0xB1U,
} PolicyEngineState;

typedef enum {
    PolicyEngineAmsNone = 0x00U,
    PolicyEngineAmsPowerNegotiation = 0x01U,
    PolicyEngineAmsSoftReset = 0x03U,
    PolicyEngineAmsHardReset = 0x04U,
    PolicyEngineAmsGetSourceCap = 0x06U,
    PolicyEngineAmsGetSinkCap = 0x07U,
    PolicyEngineAmsPowerRoleSwap = 0x08U,
    PolicyEngineAmsFastRoleSwap = 0x09U,
    PolicyEngineAmsGetPpsStatus = 0x0BU,
    PolicyEngineAmsGetRevision = 0x0FU,
    PolicyEngineAmsGetSourceInfo = 0x10U,
    PolicyEngineAmsDataRoleSwap = 0x11U,
    PolicyEngineAmsVconnSwap = 0x12U,
    PolicyEngineAmsBist = 0x13U,
    PolicyEngineAmsGetSinkCapExtended = 0x5FU,
} PolicyEngineAms;

typedef enum {
    PolicyEnginePowerNone,
    PolicyEnginePowerDefault,
    PolicyEnginePowerTransition,
    PolicyEnginePowerExplicitContract,
} PolicyEnginePower;

typedef enum {
    PolicyEngineRxEventNone,
    PolicyEngineRxEventMessage,
    PolicyEngineRxEventBusy,
} PolicyEngineRxEvent;

typedef struct {
    bool running;
    uint32_t deadline_ms;
} PolicyEngineTimer;

typedef struct {
    uint32_t pd_connected         : 1;
    uint32_t requested_pps        : 1;
    uint32_t tx_discarded         : 1;
    uint32_t dpm_request_ongoing  : 1;
    uint32_t request_first_of_ams : 1;
    uint32_t disabled_reported    : 1;
    uint32_t vbus_on_requested    : 1;
} PolicyEngineFlags;

#define POLICY_ENGINE_NEXT_CALL_NOW     0U
#define POLICY_ENGINE_NEXT_CALL_SOON    1U
#define POLICY_ENGINE_NEXT_CALL_DEFAULT 2U
#define POLICY_ENGINE_NEXT_CALL_IDLE    0xFFFFFFFFU

#define POLICY_ENGINE_MESSAGE_WITHIN_AMS   false
#define POLICY_ENGINE_MESSAGE_FIRST_OF_AMS true

#define POLICY_ENGINE_SINK_CAPABILITIES_EXTENDED_SIZE 24U
#define POLICY_ENGINE_SOURCE_INFO_OBJECT_COUNT        2U

#define POLICY_ENGINE_HEADER_MESSAGE_TYPE(header)       ((header) & 0x1FU)
#define POLICY_ENGINE_HEADER_DATA_ROLE(header)          (((header) >> 5U) & 0x1U)
#define POLICY_ENGINE_HEADER_SPEC_REVISION(header)      (((header) >> 6U) & 0x3U)
#define POLICY_ENGINE_HEADER_MESSAGE_ID(header)         (((header) >> 9U) & 0x7U)
#define POLICY_ENGINE_HEADER_DATA_OBJECT_COUNT(header)  (((header) >> 12U) & 0x7U)
#define POLICY_ENGINE_HEADER_IS_EXTENDED(header)        (((header) >> 15U) & 0x1U)
#define POLICY_ENGINE_HEADER_IS_CONTROL(header)         (((header) & 0xF000U) == 0U)
#define POLICY_ENGINE_EXTENDED_HEADER_DATA_SIZE(header) ((header) & 0x1FFU)

typedef struct {
    uint8_t port;
    UsbPdPortConfig config;
    const UsbPdCallbacks* callbacks;
    void* context;
    uint8_t tx_buffer[USB_PD_MESSAGE_BUFFER_SIZE];
    bool connected;
    UsbPdPowerRole power_role;
    UsbPdDataRole data_role;
    UsbPdSpecRevision spec_revision;
    UsbPdCcPin pin;
    bool vconn_source;
    bool sourcing_vbus;
    volatile bool swap_ongoing;
    bool hard_reset_in_progress;
    PolicyEnginePower power;
    uint32_t now_ms;
    PolicyEngineState state;
    PolicyEngineFlags flags;
    bool pps_contract;
    volatile bool hard_reset_pending;
    volatile bool fast_role_swap_received;
    volatile bool fast_role_swap_signalled;
    volatile bool fast_role_swap_signal_allowed;
    volatile bool fast_role_swap_armed;
    volatile bool power_ready;
    uint32_t fast_role_swap_signalled_ms;
    bool fast_role_swap_query_pending;
    bool fast_role_swap_queried;
    bool hard_reset_by_partner;
    bool request_pending;
    UsbPdRequest active_request;
    uint32_t rdo;
    uint32_t requested_rdo;
    uint32_t contract_pdo;
    UsbPdCapabilities source_capabilities;
    PolicyEngineAms current_ams;
    PolicyEngineTimer state_timer;
    PolicyEngineTimer pps_timer;
    PolicyEngineTimer fast_role_swap_timer;
    volatile PolicyEngineState dpm_request;
    UsbPdRequest dpm_request_id;
    bool dpm_request_answerable;
    uint16_t rx_header;
    uint8_t* rx_message;
    volatile uint8_t rx_event_slot;
    uint8_t rx_buffers[2][USB_PD_MESSAGE_BUFFER_SIZE];
    volatile uint8_t rx_slot_flags[2];
    uint8_t hard_reset_counter;
    uint8_t caps_counter;
} PolicyEngine;

extern PolicyEngine policy_engines[USB_PD_PORT_COUNT];

void policy_engine_timer_start(PolicyEngine* pe, PolicyEngineTimer* timer, uint32_t duration_ms);
void policy_engine_timer_start_at(
    PolicyEngineTimer* timer,
    uint32_t start_ms,
    uint32_t duration_ms);
void policy_engine_timer_stop(PolicyEngineTimer* timer);
bool policy_engine_timer_is_expired(const PolicyEngine* pe, const PolicyEngineTimer* timer);
bool policy_engine_timer_is_counting(const PolicyEngine* pe, const PolicyEngineTimer* timer);
uint32_t policy_engine_timer_remaining(const PolicyEngine* pe, const PolicyEngineTimer* timer);

void policy_engine_notify(PolicyEngine* pe, const UsbPdEvent* event);
void policy_engine_notify_type(PolicyEngine* pe, UsbPdEventType type);
void policy_engine_answer_request(
    PolicyEngine* pe,
    UsbPdRequest request,
    UsbPdRequestResult result);
void policy_engine_set_power(PolicyEngine* pe, UsbPdPower power);
void policy_engine_set_vconn(PolicyEngine* pe, bool enabled);
void policy_engine_set_swap_ongoing(PolicyEngine* pe, bool ongoing);
void policy_engine_set_hard_reset_in_progress(PolicyEngine* pe, bool in_progress);
void policy_engine_update_vbus_detection(PolicyEngine* pe);
void policy_engine_disarm_fast_role_swap(PolicyEngine* pe);
void policy_engine_stop_fast_role_swap(PolicyEngine* pe);
void policy_engine_drop_dpm_request(PolicyEngine* pe);
void policy_engine_discard_requests(PolicyEngine* pe);
void policy_engine_start_dpm_request(PolicyEngine* pe);
bool policy_engine_is_dual_role(const PolicyEngine* pe);
bool policy_engine_vbus_is(const PolicyEngine* pe, UsbPdVbusLevel level);
uint32_t policy_engine_capabilities_object(const UsbPdCapabilities* capabilities, uint32_t rdo);
bool policy_engine_pdo_is_pps(uint32_t pdo);
bool policy_engine_sink_capabilities_request_fast_role_swap(const UsbPdCapabilities* capabilities);

void policy_engine_read_capabilities(PolicyEngine* pe, UsbPdCapabilities* capabilities);
void policy_engine_store_source_capabilities(PolicyEngine* pe);
void policy_engine_send_hard_reset(PolicyEngine* pe);
void policy_engine_set_power_role(PolicyEngine* pe, UsbPdPowerRole role);
PolicyEngineRxEvent policy_engine_receive_queue_get(PolicyEngine* pe);
void policy_engine_receive_queue_release(PolicyEngine* pe);
bool policy_engine_resolve_ams_conflict(PolicyEngine* pe);
void policy_engine_reset_after_hard_reset(PolicyEngine* pe);
void policy_engine_reset_session(PolicyEngine* pe);
void policy_engine_reset_counters(PolicyEngine* pe);
void policy_engine_reset_state_machine(PolicyEngine* pe);
void policy_engine_adopt_spec_revision(PolicyEngine* pe, UsbPdSpecRevision spec_revision);
uint32_t policy_engine_common_run(PolicyEngine* pe, PolicyEngineRxEvent* event);
void policy_engine_evaluate_data_role_swap(PolicyEngine* pe);
void policy_engine_set_power_state(PolicyEngine* pe, PolicyEnginePower power);
uint32_t policy_engine_get_nearest_timer_ms(PolicyEngine* pe);
bool policy_engine_end_ams_on_not_supported(PolicyEngine* pe, PolicyEngineRxEvent* event);
void policy_engine_swap_data_role(PolicyEngine* pe);
uint32_t policy_engine_start_state_timer(PolicyEngine* pe, uint32_t duration_ms);
uint32_t policy_engine_start_sender_response_timer(PolicyEngine* pe);
PolicyEngineState policy_engine_hard_reset_give_up_state(PolicyEngine* pe);
bool policy_engine_is_busy(PolicyEngine* pe);
void policy_engine_disable_vconn_if_sourcing(PolicyEngine* pe);

void policy_engine_role_enter_startup(
    PolicyEngine* pe,
    UsbPdPowerRole power_role,
    UsbPdDataRole data_role,
    PolicyEngineState startup_state);
void policy_engine_role_reject_wrong_data_role(PolicyEngine* pe, PolicyEngineRxEvent* event);
uint32_t policy_engine_role_finish_step(
    PolicyEngine* pe,
    PolicyEngineRxEvent* event,
    uint32_t next_call_ms);
void policy_engine_role_take_hard_reset_request(PolicyEngine* pe, PolicyEngineRxEvent* event);
uint32_t policy_engine_role_send_power_role_swap(
    PolicyEngine* pe,
    PolicyEngineState wait_response_state);
bool policy_engine_role_run_shared_state(
    PolicyEngine* pe,
    PolicyEngineRxEvent* event,
    uint32_t* next_call_ms);

ProtocolLayerTransmitResult policy_engine_send_control_message(
    PolicyEngine* pe,
    UsbPdControlMessage message_type,
    PolicyEngineState next_state,
    bool first_message_of_ams);
ProtocolLayerTransmitResult policy_engine_send_data_message(
    PolicyEngine* pe,
    UsbPdDataMessage message_type,
    const uint32_t* data_objects,
    uint32_t data_object_count,
    PolicyEngineState next_state,
    bool first_message_of_ams);
ProtocolLayerTransmitResult policy_engine_send_message(
    PolicyEngine* pe,
    uint8_t message_type,
    uint16_t size,
    PolicyEngineState next_state,
    bool first_message_of_ams);
ProtocolLayerTransmitResult policy_engine_send_extended_message(
    PolicyEngine* pe,
    UsbPdExtendedMessage message_type,
    uint16_t size,
    PolicyEngineState next_state,
    bool first_message_of_ams);
void policy_engine_dispatch_message(PolicyEngine* pe);
void policy_engine_dispatch_control_message(PolicyEngine* pe);
void policy_engine_dispatch_data_message(PolicyEngine* pe);
void policy_engine_consume_message(
    PolicyEngine* pe,
    PolicyEngineRxEvent* event,
    PolicyEngineState next_state);
bool policy_engine_is_data_message(PolicyEngine* pe, UsbPdDataMessage message_type);
uint16_t policy_engine_get_control_message(PolicyEngine* pe);

void policy_engine_source_handle_pending_requests(PolicyEngine* pe, PolicyEngineRxEvent* event);
uint32_t policy_engine_source_run_state(PolicyEngine* pe, PolicyEngineRxEvent* event);
ProtocolLayerTransmitResult
    policy_engine_source_send_capabilities(PolicyEngine* pe, bool first_message_of_ams);
void policy_engine_sink_handle_pending_requests(PolicyEngine* pe, PolicyEngineRxEvent* event);
uint32_t policy_engine_sink_run_state(PolicyEngine* pe, PolicyEngineRxEvent* event);
uint32_t policy_engine_sink_ready(PolicyEngine* pe, PolicyEngineRxEvent* event);
void policy_engine_sink_accept_source_capabilities(PolicyEngine* pe, PolicyEngineRxEvent* event);

uint32_t vconn_swap_run(PolicyEngine* pe, PolicyEngineRxEvent* event);

#endif
