#include <furi.h>

#include <applications/services/input/remote_decoders/remote_decoder.h>

#include <applications/services/input/remote_decoders/apple_remote_decoder.h>
#include <applications/services/input/remote_decoders/ctia_remote_decoder.h>

const RemoteDecoder* const REMOTE_DECODERS[] = {
    &ctia_remote_decoder,
    &apple_remote_decoder,
};

const size_t REMOTE_DECODERS_COUNT = COUNT_OF(REMOTE_DECODERS);
