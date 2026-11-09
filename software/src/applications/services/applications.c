#include <applications/services/applications.h>

#include <applications/services/audio/audio.h>
#include <applications/services/input/input.h>
#include <applications/services/media_keys/media_keys.h>
#include <applications/services/rpc/rpc.h>
#include <applications/services/usbc/usbc.h>

const ServiceApplication SERVICE_APPLICATIONS[] = {
    {.app = usbc_srv, .name = "UsbcSrv", .stack_size = 1024},
    {.app = input_srv, .name = "InputSrv", .stack_size = 512},
    {.app = audio_srv, .name = "AudioSrv", .stack_size = 1024},
    {.app = media_keys_srv, .name = "MediaKeysSrv", .stack_size = 512},
    {.app = rpc_srv, .name = "RpcSrv", .stack_size = 1024},
};

const size_t SERVICE_APPLICATIONS_COUNT = COUNT_OF(SERVICE_APPLICATIONS);
