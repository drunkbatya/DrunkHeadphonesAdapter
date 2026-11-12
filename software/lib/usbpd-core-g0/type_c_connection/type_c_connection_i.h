#ifndef TYPE_C_CONNECTION_I_H_
#define TYPE_C_CONNECTION_I_H_

#include "type_c_connection/type_c_connection.h"

#define TYPE_C_CONNECTION_CC_DEBOUNCE_MS     120u
#define TYPE_C_CONNECTION_PD_DEBOUNCE_MS     12u
#define TYPE_C_CONNECTION_SRC_DISCONNECT_MS  2u
#define TYPE_C_CONNECTION_ERROR_RECOVERY_MS  26u
#define TYPE_C_CONNECTION_DRP_SOURCE_MS      40u
#define TYPE_C_CONNECTION_DRP_SINK_MS        40u
#define TYPE_C_CONNECTION_DRP_TRY_MS         100u
#define TYPE_C_CONNECTION_TRY_CC_DEBOUNCE_MS 15u
#define TYPE_C_CONNECTION_POLL_MS            2u
#define TYPE_C_CONNECTION_WAIT_FOREVER       UINT32_MAX

typedef enum {
    TypeCConnectionStateDisabled,
    TypeCConnectionStateErrorRecovery,
    TypeCConnectionStateUnattachedSink,
    TypeCConnectionStateAttachWaitSink,
    TypeCConnectionStateAttachedSink,
    TypeCConnectionStateUnattachedSource,
    TypeCConnectionStateAttachWaitSource,
    TypeCConnectionStateAttachedSource,
    TypeCConnectionStateTrySink,
    TypeCConnectionStateTryWaitSource,
} TypeCConnectionState;

typedef enum {
    TypeCConnectionTerminationOpen,
    TypeCConnectionTerminationRd,
    TypeCConnectionTerminationRp,
} TypeCConnectionTermination;

typedef struct {
    UsbPdCcPin pin;
    UsbPdCcState state;
    bool vconn_required;
} TypeCConnectionPartner;

typedef struct {
    UsbPdPortMode mode;
    volatile UsbPdPortMode requested_mode;
    volatile bool mode_change_pending;
    volatile UsbPdRpCurrent requested_rp_current;
    volatile bool rp_current_change_pending;
    volatile bool error_recovery_pending;
    volatile bool swap_in_progress;
    bool vbus_disconnect_detection;
    bool attached;
    TypeCConnectionState state;
    uint32_t state_entered_ms;
    TypeCConnectionTermination termination;
    UsbPdRpCurrent rp_current;
    UsbPdRpCurrent presented_rp_current;
    UsbPdPowerRole power_role;
    UsbPdCcPin pin;
    bool partner_observed;
    TypeCConnectionPartner observed_partner;
    uint32_t partner_stable_since_ms;
    TypeCConnectionCallback callback;
    void* context;
} TypeCConnection;

#endif
