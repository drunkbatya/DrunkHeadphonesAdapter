#include <applications/services/audio/audio_capture.h>

#include <furi.h>
#include <furi_hal_i2s.h>

#include <string.h>

#define AUDIO_CAPTURE_I2S_CHANNELS          2
#define AUDIO_CAPTURE_I2S_LEFT_CHANNEL      0
#define AUDIO_CAPTURE_SAMPLE_RATE_MAX       48000
#define AUDIO_CAPTURE_HALVES_PER_SECOND     1000
#define AUDIO_CAPTURE_HALF_COUNT            2
#define AUDIO_CAPTURE_USB_16BIT_SAMPLE_SIZE 2
#define AUDIO_CAPTURE_USB_24BIT_SAMPLE_SIZE 3
#define AUDIO_CAPTURE_I2S_16BIT_HALFWORDS   1
#define AUDIO_CAPTURE_I2S_24BIT_HALFWORDS   2
#define AUDIO_CAPTURE_USB_SAMPLE_SIZE_MAX   AUDIO_CAPTURE_USB_24BIT_SAMPLE_SIZE
#define AUDIO_CAPTURE_I2S_HALFWORDS_MAX     AUDIO_CAPTURE_I2S_24BIT_HALFWORDS
#define AUDIO_CAPTURE_USB_16BIT_LOW_BYTE    0
#define AUDIO_CAPTURE_USB_16BIT_HIGH_BYTE   1
#define AUDIO_CAPTURE_USB_24BIT_LOW_BYTE    0
#define AUDIO_CAPTURE_USB_24BIT_MIDDLE_BYTE 1
#define AUDIO_CAPTURE_USB_24BIT_HIGH_BYTE   2
#define AUDIO_CAPTURE_BITS_PER_BYTE         8
#define AUDIO_CAPTURE_FRAMES_PER_HALF(rate) \
    (((rate) + AUDIO_CAPTURE_HALVES_PER_SECOND - 1) / AUDIO_CAPTURE_HALVES_PER_SECOND)
#define AUDIO_CAPTURE_FRAMES_PER_HALF_MAX \
    AUDIO_CAPTURE_FRAMES_PER_HALF(AUDIO_CAPTURE_SAMPLE_RATE_MAX)
#define AUDIO_CAPTURE_USB_HALF_SIZE_MAX \
    (AUDIO_CAPTURE_FRAMES_PER_HALF_MAX * AUDIO_CAPTURE_USB_SAMPLE_SIZE_MAX)
#define AUDIO_CAPTURE_DMA_HALF_LENGTH_MAX                             \
    (AUDIO_CAPTURE_FRAMES_PER_HALF_MAX * AUDIO_CAPTURE_I2S_CHANNELS * \
     AUDIO_CAPTURE_I2S_HALFWORDS_MAX)

typedef struct {
    size_t usb_sample_size;
    size_t i2s_halfwords_per_sample;
    FuriHalI2sDataFormat i2s_data_format;
} AudioCaptureSampleLayout;

static const AudioCaptureSampleLayout audio_capture_sample_layouts[] = {
    [FuriHalUsbAudioSampleFormat16Bit] =
        {
            .usb_sample_size = AUDIO_CAPTURE_USB_16BIT_SAMPLE_SIZE,
            .i2s_halfwords_per_sample = AUDIO_CAPTURE_I2S_16BIT_HALFWORDS,
            .i2s_data_format = FuriHalI2sDataFormat16BitIn32BitFrame,
        },
    [FuriHalUsbAudioSampleFormat24Bit] =
        {
            .usb_sample_size = AUDIO_CAPTURE_USB_24BIT_SAMPLE_SIZE,
            .i2s_halfwords_per_sample = AUDIO_CAPTURE_I2S_24BIT_HALFWORDS,
            .i2s_data_format = FuriHalI2sDataFormat24BitIn32BitFrame,
        },
};

struct AudioCapture {
    AudioCaptureErrorCallback error_callback;
    void* error_context;
    bool running;
    uint32_t sample_rate;
    FuriHalUsbAudioSampleFormat sample_format;
    size_t frames_per_half;
    size_t usb_half_size;
    size_t dma_half_length;
    size_t dma_halfwords_per_frame;
    uint8_t usb_samples[AUDIO_CAPTURE_USB_HALF_SIZE_MAX];
    uint16_t dma_samples[AUDIO_CAPTURE_HALF_COUNT * AUDIO_CAPTURE_DMA_HALF_LENGTH_MAX];
};

AudioCapture* audio_capture_alloc(AudioCaptureErrorCallback error_callback, void* context) {
    furi_check(error_callback);

    AudioCapture* capture = malloc(sizeof(AudioCapture));
    capture->error_callback = error_callback;
    capture->error_context = context;
    capture->running = false;

    return capture;
}

void audio_capture_free(AudioCapture* capture) {
    furi_check(capture);
    furi_check(!capture->running);

    free(capture);
}

static void audio_capture_repack_16bit(AudioCapture* capture, const uint16_t* dma_half) {
    for(size_t i = 0; i < capture->frames_per_half; i++) {
        const uint16_t sample =
            dma_half[i * capture->dma_halfwords_per_frame + AUDIO_CAPTURE_I2S_LEFT_CHANNEL];
        uint8_t* usb_sample = &capture->usb_samples[i * AUDIO_CAPTURE_USB_16BIT_SAMPLE_SIZE];
        usb_sample[AUDIO_CAPTURE_USB_16BIT_LOW_BYTE] = (uint8_t)sample;
        usb_sample[AUDIO_CAPTURE_USB_16BIT_HIGH_BYTE] =
            (uint8_t)(sample >> AUDIO_CAPTURE_BITS_PER_BYTE);
    }
}

static void audio_capture_repack_24bit(AudioCapture* capture, const uint16_t* dma_half) {
    for(size_t i = 0; i < capture->frames_per_half; i++) {
        const uint16_t* i2s_sample =
            &dma_half
                [i * capture->dma_halfwords_per_frame +
                 AUDIO_CAPTURE_I2S_LEFT_CHANNEL * AUDIO_CAPTURE_I2S_24BIT_HALFWORDS];
        uint8_t* usb_sample = &capture->usb_samples[i * AUDIO_CAPTURE_USB_24BIT_SAMPLE_SIZE];
        usb_sample[AUDIO_CAPTURE_USB_24BIT_LOW_BYTE] =
            (uint8_t)(i2s_sample[1] >> AUDIO_CAPTURE_BITS_PER_BYTE);
        usb_sample[AUDIO_CAPTURE_USB_24BIT_MIDDLE_BYTE] = (uint8_t)i2s_sample[0];
        usb_sample[AUDIO_CAPTURE_USB_24BIT_HIGH_BYTE] =
            (uint8_t)(i2s_sample[0] >> AUDIO_CAPTURE_BITS_PER_BYTE);
    }
}

static void audio_capture_drain(AudioCapture* capture, const uint16_t* dma_half) {
    if(capture->sample_format == FuriHalUsbAudioSampleFormat16Bit) {
        audio_capture_repack_16bit(capture, dma_half);
    } else {
        audio_capture_repack_24bit(capture, dma_half);
    }
    furi_hal_usb_audio_mic_write(capture->usb_samples, capture->usb_half_size);
}

static void audio_capture_i2s_callback(FuriHalI2sEvent event, void* context) {
    AudioCapture* capture = context;

    switch(event) {
    case FuriHalI2sEventHalfTransfer:
        audio_capture_drain(capture, capture->dma_samples);
        break;
    case FuriHalI2sEventTransferComplete:
        audio_capture_drain(capture, &capture->dma_samples[capture->dma_half_length]);
        break;
    default:
        capture->error_callback(event, capture->error_context);
        break;
    }
}

bool audio_capture_start(
    AudioCapture* capture,
    uint32_t sample_rate,
    FuriHalUsbAudioSampleFormat sample_format) {
    furi_check(capture);
    furi_check(!capture->running);
    furi_check(sample_rate <= AUDIO_CAPTURE_SAMPLE_RATE_MAX);
    furi_check(sample_format < COUNT_OF(audio_capture_sample_layouts));

    const AudioCaptureSampleLayout* layout = &audio_capture_sample_layouts[sample_format];

    capture->sample_rate = sample_rate;
    capture->sample_format = sample_format;
    capture->frames_per_half = AUDIO_CAPTURE_FRAMES_PER_HALF(sample_rate);
    capture->usb_half_size = capture->frames_per_half * layout->usb_sample_size;
    capture->dma_halfwords_per_frame =
        AUDIO_CAPTURE_I2S_CHANNELS * layout->i2s_halfwords_per_sample;
    capture->dma_half_length = capture->frames_per_half * capture->dma_halfwords_per_frame;
    memset(capture->dma_samples, 0, sizeof(capture->dma_samples));

    if(!furi_hal_i2s_start(
           FuriHalI2sIdMic,
           layout->i2s_data_format,
           capture->dma_samples,
           AUDIO_CAPTURE_HALF_COUNT * capture->dma_half_length,
           audio_capture_i2s_callback,
           capture)) {
        return false;
    }
    capture->running = true;

    const FuriHalUsbAudioFormat format = {
        .sample_rate = sample_rate,
        .sample_format = sample_format,
    };
    furi_hal_usb_audio_mic_attach(&format);
    return true;
}

void audio_capture_stop(AudioCapture* capture) {
    furi_check(capture);
    furi_check(capture->running);

    furi_hal_usb_audio_mic_detach();
    furi_hal_i2s_stop(FuriHalI2sIdMic);
    capture->running = false;
}

bool audio_capture_restart(AudioCapture* capture) {
    furi_check(capture);

    const uint32_t sample_rate = capture->sample_rate;
    const FuriHalUsbAudioSampleFormat sample_format = capture->sample_format;

    audio_capture_stop(capture);
    return audio_capture_start(capture, sample_rate, sample_format);
}

bool audio_capture_is_running(const AudioCapture* capture) {
    furi_check(capture);
    return capture->running;
}
