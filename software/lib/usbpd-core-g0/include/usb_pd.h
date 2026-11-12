#ifndef USB_PD_H_
#define USB_PD_H_

#include "usb_pd_types.h"

typedef struct {
    uint16_t vendor_id;
    uint16_t product_id;
    uint32_t xid;
    uint8_t firmware_version;
    uint8_t hardware_version;
} UsbPdIdentity;

typedef struct {
    UsbPdPortMode mode;
    UsbPdRpCurrent rp_current;
    UsbPdSpecRevision spec_revision;
    bool power_role_swap;
    bool data_role_swap;
    bool vconn_swap;
    bool fast_role_swap;
    UsbPdIdentity identity;
} UsbPdPortConfig;

typedef enum {
    UsbPdPowerSourceNewContract,
    UsbPdPowerSourceOff,
    UsbPdPowerSourceOn,
    UsbPdPowerSinkStandby,
    UsbPdPowerSinkOff,
    UsbPdPowerSinkNewContract,
    UsbPdPowerSinkHardReset,
} UsbPdPower;

typedef enum {
    UsbPdRequestPowerRoleSwap,
    UsbPdRequestDataRoleSwap,
    UsbPdRequestVconnSwap,
    UsbPdRequestSourceCapabilities,
    UsbPdRequestSinkCapabilities,
    UsbPdRequestPpsStatus,
    UsbPdRequestSoftReset,
    UsbPdRequestPower,
} UsbPdRequest;

typedef enum {
    UsbPdRequestResultAccepted,
    UsbPdRequestResultRejected,
    UsbPdRequestResultWait,
    UsbPdRequestResultNotSupported,
    UsbPdRequestResultNoResponse,
    UsbPdRequestResultDiscarded,
} UsbPdRequestResult;

typedef struct {
    uint16_t output_voltage_20mv;
    uint8_t output_current_50ma;
    uint8_t real_time_flags;
} UsbPdPpsStatus;

typedef enum {
    UsbPdEventTypeAttached,
    UsbPdEventTypeDetached,
    UsbPdEventTypeSourceCapabilities,
    UsbPdEventTypeSinkCapabilities,
    UsbPdEventTypeContract,
    UsbPdEventTypeRequestAnswered,
    UsbPdEventTypePowerRoleSwapped,
    UsbPdEventTypeDataRoleSwapped,
    UsbPdEventTypeVconnSwapped,
    UsbPdEventTypeFastRoleSwapped,
    UsbPdEventTypeHardReset,
    UsbPdEventTypeSoftReset,
    UsbPdEventTypePpsStatus,
    UsbPdEventTypePartnerNotPd,
} UsbPdEventType;

typedef struct {
    UsbPdPowerRole power_role;
    UsbPdCcPin pin;
    UsbPdRpCurrent partner_rp_current;
} UsbPdAttach;

typedef struct {
    uint32_t pdo;
    uint32_t rdo;
} UsbPdContract;

typedef struct {
    UsbPdRequest request;
    UsbPdRequestResult result;
} UsbPdRequestAnswer;

typedef struct {
    UsbPdEventType type;
    union {
        UsbPdAttach attach;
        const UsbPdCapabilities* capabilities;
        UsbPdContract contract;
        UsbPdRequestAnswer request_answer;
        UsbPdPowerRole power_role;
        UsbPdDataRole data_role;
        bool vconn_source;
        bool hard_reset_received;
        UsbPdPpsStatus pps_status;
    };
} UsbPdEvent;

typedef struct {
    void (*on_event)(void* context, uint8_t port, const UsbPdEvent* event);
    void (*get_source_capabilities)(void* context, uint8_t port, UsbPdCapabilities* capabilities);
    void (*get_sink_capabilities)(void* context, uint8_t port, UsbPdCapabilities* capabilities);
    bool (*get_pps_status)(void* context, uint8_t port, UsbPdPpsStatus* status);
    UsbPdAnswer (*evaluate_request)(void* context, uint8_t port, uint32_t rdo);
    uint32_t (*select_capability)(
        void* context,
        uint8_t port,
        const UsbPdCapabilities* source_capabilities);
    UsbPdAnswer (*evaluate_power_role_swap)(void* context, uint8_t port);
    UsbPdAnswer (*evaluate_data_role_swap)(void* context, uint8_t port);
    UsbPdAnswer (*evaluate_vconn_swap)(void* context, uint8_t port);
    void (*set_power)(void* context, uint8_t port, UsbPdPower power);
    void (*set_vconn)(void* context, uint8_t port, bool enabled);
    void (*wakeup)(void* context);
} UsbPdCallbacks;

typedef struct {
    bool attached;
    bool explicit_contract;
    UsbPdPowerRole power_role;
    UsbPdDataRole data_role;
    bool vconn_source;
    UsbPdSpecRevision spec_revision;
    UsbPdCcPin pin;
    UsbPdContract contract;
} UsbPdPortStatus;

void usb_pd_init(const UsbPdCallbacks* callbacks, void* context);
bool usb_pd_port_init(uint8_t port, const UsbPdPortConfig* config);

uint32_t usb_pd_process(void);

void usb_pd_set_mode(uint8_t port, UsbPdPortMode mode);
void usb_pd_set_rp_current(uint8_t port, UsbPdRpCurrent current);
void usb_pd_enter_error_recovery(uint8_t port);

void usb_pd_power_ready(uint8_t port);

bool usb_pd_request(uint8_t port, UsbPdRequest request);
bool usb_pd_request_power(uint8_t port, uint32_t rdo);
bool usb_pd_send_source_capabilities(uint8_t port);
bool usb_pd_send_hard_reset(uint8_t port);

bool usb_pd_signal_fast_role_swap(uint8_t port);

void usb_pd_get_port_status(uint8_t port, UsbPdPortStatus* status);

#endif
