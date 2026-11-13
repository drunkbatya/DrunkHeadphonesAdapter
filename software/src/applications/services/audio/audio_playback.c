#include <applications/services/audio/audio_playback.h>

#include <furi.h>
#include <furi_hal_i2s.h>

#include <string.h>

#define AUDIO_PLAYBACK_CHANNELS              2
#define AUDIO_PLAYBACK_SAMPLE_RATE_MAX       96000
#define AUDIO_PLAYBACK_HALVES_PER_SECOND     1000
#define AUDIO_PLAYBACK_HALF_COUNT            2
#define AUDIO_PLAYBACK_PRIME_HALVES          2
#define AUDIO_PLAYBACK_USB_16BIT_SAMPLE_SIZE 2
#define AUDIO_PLAYBACK_USB_24BIT_SAMPLE_SIZE 3
#define AUDIO_PLAYBACK_I2S_16BIT_HALFWORDS   1
#define AUDIO_PLAYBACK_I2S_24BIT_HALFWORDS   2
#define AUDIO_PLAYBACK_USB_SAMPLE_SIZE_MAX   AUDIO_PLAYBACK_USB_24BIT_SAMPLE_SIZE
#define AUDIO_PLAYBACK_I2S_HALFWORDS_MAX     AUDIO_PLAYBACK_I2S_24BIT_HALFWORDS
#define AUDIO_PLAYBACK_USB_24BIT_LOW_BYTE    0
#define AUDIO_PLAYBACK_USB_24BIT_MIDDLE_BYTE 1
#define AUDIO_PLAYBACK_USB_24BIT_HIGH_BYTE   2
#define AUDIO_PLAYBACK_BITS_PER_BYTE         8
#define AUDIO_PLAYBACK_FRAMES_PER_HALF(rate) \
    (((rate) + AUDIO_PLAYBACK_HALVES_PER_SECOND - 1) / AUDIO_PLAYBACK_HALVES_PER_SECOND)
#define AUDIO_PLAYBACK_FRAMES_PER_HALF_MAX \
    AUDIO_PLAYBACK_FRAMES_PER_HALF(AUDIO_PLAYBACK_SAMPLE_RATE_MAX)
#define AUDIO_PLAYBACK_SAMPLES_PER_HALF_MAX \
    (AUDIO_PLAYBACK_FRAMES_PER_HALF_MAX * AUDIO_PLAYBACK_CHANNELS)
#define AUDIO_PLAYBACK_USB_HALF_SIZE_MAX \
    (AUDIO_PLAYBACK_SAMPLES_PER_HALF_MAX * AUDIO_PLAYBACK_USB_SAMPLE_SIZE_MAX)
#define AUDIO_PLAYBACK_DMA_HALF_LENGTH_MAX \
    (AUDIO_PLAYBACK_SAMPLES_PER_HALF_MAX * AUDIO_PLAYBACK_I2S_HALFWORDS_MAX)

typedef struct {
    size_t usb_sample_size;
    size_t i2s_halfwords_per_sample;
    FuriHalI2sDataFormat i2s_data_format;
} AudioPlaybackSampleLayout;

static const AudioPlaybackSampleLayout audio_playback_sample_layouts[] = {
    [FuriHalUsbAudioSampleFormat16Bit] =
        {
            .usb_sample_size = AUDIO_PLAYBACK_USB_16BIT_SAMPLE_SIZE,
            .i2s_halfwords_per_sample = AUDIO_PLAYBACK_I2S_16BIT_HALFWORDS,
            .i2s_data_format = FuriHalI2sDataFormat16BitIn32BitFrame,
        },
    [FuriHalUsbAudioSampleFormat24Bit] =
        {
            .usb_sample_size = AUDIO_PLAYBACK_USB_24BIT_SAMPLE_SIZE,
            .i2s_halfwords_per_sample = AUDIO_PLAYBACK_I2S_24BIT_HALFWORDS,
            .i2s_data_format = FuriHalI2sDataFormat24BitIn32BitFrame,
        },
};

struct AudioPlayback {
    AudioPlaybackErrorCallback error_callback;
    void* error_context;
    bool running;
    uint32_t sample_rate;
    FuriHalUsbAudioSampleFormat sample_format;
    size_t samples_per_half;
    size_t usb_half_size;
    size_t dma_half_length;
    size_t dma_halfwords_per_frame;
    size_t dma_position;
    size_t played_halfwords;
    uint32_t played_frames;
    bool priming;
    uint8_t usb_samples[AUDIO_PLAYBACK_USB_HALF_SIZE_MAX];
    uint16_t dma_samples[AUDIO_PLAYBACK_HALF_COUNT * AUDIO_PLAYBACK_DMA_HALF_LENGTH_MAX];
};

AudioPlayback* audio_playback_alloc(AudioPlaybackErrorCallback error_callback, void* context) {
    furi_check(error_callback);

    AudioPlayback* playback = malloc(sizeof(AudioPlayback));
    playback->error_callback = error_callback;
    playback->error_context = context;
    playback->running = false;

    return playback;
}

void audio_playback_free(AudioPlayback* playback) {
    furi_check(playback);
    furi_check(!playback->running);

    free(playback);
}

static void audio_playback_repack_24bit(const AudioPlayback* playback, uint16_t* dma_half) {
    for(size_t i = 0; i < playback->samples_per_half; i++) {
        const uint8_t* sample = &playback->usb_samples[i * AUDIO_PLAYBACK_USB_24BIT_SAMPLE_SIZE];
        dma_half[i * AUDIO_PLAYBACK_I2S_24BIT_HALFWORDS] =
            (uint16_t)((sample[AUDIO_PLAYBACK_USB_24BIT_HIGH_BYTE]
                        << AUDIO_PLAYBACK_BITS_PER_BYTE) |
                       sample[AUDIO_PLAYBACK_USB_24BIT_MIDDLE_BYTE]);
        dma_half[i * AUDIO_PLAYBACK_I2S_24BIT_HALFWORDS + 1] =
            (uint16_t)(sample[AUDIO_PLAYBACK_USB_24BIT_LOW_BYTE] << AUDIO_PLAYBACK_BITS_PER_BYTE);
    }
}

static void audio_playback_fill(AudioPlayback* playback, uint16_t* dma_half) {
    const size_t available = furi_hal_usb_audio_speaker_get_available();

    if(playback->priming && available >= AUDIO_PLAYBACK_PRIME_HALVES * playback->usb_half_size) {
        playback->priming = false;
    }

    if(!playback->priming && available < playback->usb_half_size) {
        playback->priming = true;
    }

    if(playback->priming) {
        memset(dma_half, 0, playback->dma_half_length * sizeof(uint16_t));
        return;
    }

    uint8_t* usb_half = playback->sample_format == FuriHalUsbAudioSampleFormat16Bit ?
                            (uint8_t*)dma_half :
                            playback->usb_samples;
    const size_t read_size = furi_hal_usb_audio_speaker_read(usb_half, playback->usb_half_size);
    memset(&usb_half[read_size], 0, playback->usb_half_size - read_size);

    if(playback->sample_format == FuriHalUsbAudioSampleFormat24Bit) {
        audio_playback_repack_24bit(playback, dma_half);
    }
}

static uint32_t audio_playback_get_played_frames(void* context) {
    AudioPlayback* playback = context;

    const size_t dma_length = AUDIO_PLAYBACK_HALF_COUNT * playback->dma_half_length;
    const size_t dma_position = furi_hal_i2s_get_position(FuriHalI2sIdHeadphones);
    playback->played_halfwords +=
        (dma_position + dma_length - playback->dma_position) % dma_length;
    playback->dma_position = dma_position;

    const size_t frames = playback->played_halfwords / playback->dma_halfwords_per_frame;
    playback->played_halfwords -= frames * playback->dma_halfwords_per_frame;
    playback->played_frames += frames;

    return playback->played_frames;
}

static void audio_playback_i2s_callback(FuriHalI2sEvent event, void* context) {
    AudioPlayback* playback = context;

    switch(event) {
    case FuriHalI2sEventHalfTransfer:
        audio_playback_fill(playback, playback->dma_samples);
        break;
    case FuriHalI2sEventTransferComplete:
        audio_playback_fill(playback, &playback->dma_samples[playback->dma_half_length]);
        break;
    default:
        playback->error_callback(event, playback->error_context);
        break;
    }
}

bool audio_playback_start(
    AudioPlayback* playback,
    uint32_t sample_rate,
    FuriHalUsbAudioSampleFormat sample_format) {
    furi_check(playback);
    furi_check(!playback->running);
    furi_check(sample_rate <= AUDIO_PLAYBACK_SAMPLE_RATE_MAX);
    furi_check(sample_format < COUNT_OF(audio_playback_sample_layouts));

    const AudioPlaybackSampleLayout* layout = &audio_playback_sample_layouts[sample_format];

    playback->sample_rate = sample_rate;
    playback->sample_format = sample_format;
    playback->samples_per_half =
        AUDIO_PLAYBACK_FRAMES_PER_HALF(sample_rate) * AUDIO_PLAYBACK_CHANNELS;
    playback->usb_half_size = playback->samples_per_half * layout->usb_sample_size;
    playback->dma_half_length = playback->samples_per_half * layout->i2s_halfwords_per_sample;
    playback->dma_halfwords_per_frame = AUDIO_PLAYBACK_CHANNELS * layout->i2s_halfwords_per_sample;
    playback->dma_position = 0;
    playback->played_halfwords = 0;
    playback->played_frames = 0;
    playback->priming = true;
    memset(playback->dma_samples, 0, sizeof(playback->dma_samples));

    if(!furi_hal_i2s_start(
           FuriHalI2sIdHeadphones,
           layout->i2s_data_format,
           playback->dma_samples,
           AUDIO_PLAYBACK_HALF_COUNT * playback->dma_half_length,
           audio_playback_i2s_callback,
           playback)) {
        return false;
    }
    playback->running = true;

    const FuriHalUsbAudioFormat format = {
        .sample_rate = sample_rate,
        .sample_format = sample_format,
    };
    furi_hal_usb_audio_speaker_attach(&format, audio_playback_get_played_frames, playback);
    return true;
}

void audio_playback_stop(AudioPlayback* playback) {
    furi_check(playback);
    furi_check(playback->running);

    furi_hal_usb_audio_speaker_detach();
    furi_hal_i2s_stop(FuriHalI2sIdHeadphones);
    playback->running = false;
}

bool audio_playback_restart(AudioPlayback* playback) {
    furi_check(playback);

    const uint32_t sample_rate = playback->sample_rate;
    const FuriHalUsbAudioSampleFormat sample_format = playback->sample_format;

    audio_playback_stop(playback);
    return audio_playback_start(playback, sample_rate, sample_format);
}

bool audio_playback_is_running(const AudioPlayback* playback) {
    furi_check(playback);
    return playback->running;
}
