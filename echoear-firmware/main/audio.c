#include "audio.h"

#include <math.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_codec_dev.h"
#include "bsp/esp-bsp.h"

static const char *TAG = "audio";

#define SAMPLE_RATE        16000
#define SPEAKER_VOLUME     60
/* 15 s of speech. Keep in sync with MAX_SPEECH_BYTES in pc-bridge/src/speech/speaker.ts. */
#define SPEECH_MAX_BYTES   (15 * SAMPLE_RATE * 2)
#define PLAY_QUEUE_LEN     8
#define PLAY_CHUNK_FRAMES  256
#define CHIME_AMPLITUDE    7000.0f
#define CHIME_MAX_NOTES    3

typedef struct {
    const int16_t *pcm; /* mono */
    size_t samples;
    bool owned;         /* heap clip to free after playing */
} clip_t;

typedef struct {
    float freq_hz;
    uint16_t start_ms;
    uint16_t length_ms;
} note_t;

static const note_t CHIME_NOTES[AUDIO_CHIME_COUNT][CHIME_MAX_NOTES] = {
    [AUDIO_CHIME_CONFIRM] = {{880.0f, 0, 260}, {1174.7f, 120, 320}},
    [AUDIO_CHIME_DONE] = {{523.3f, 0, 320}, {659.3f, 90, 320}, {784.0f, 180, 420}},
    [AUDIO_CHIME_ATTENTION] = {{784.0f, 0, 180}, {784.0f, 170, 220}},
    [AUDIO_CHIME_ERROR] = {{440.0f, 0, 260}, {329.6f, 170, 380}},
};

static esp_codec_dev_handle_t s_speaker;
static QueueHandle_t s_queue;
static clip_t s_chimes[AUDIO_CHIME_COUNT];

/* Speech clip being received (host link RX task only). */
static int16_t *s_rx_pcm;
static size_t s_rx_bytes;
static uint16_t s_rx_id;

/* Bell-like notes: quick attack, exponential decay, a little second harmonic. */
static esp_err_t chime_render(audio_chime_t chime)
{
    size_t total_ms = 0;
    for (int n = 0; n < CHIME_MAX_NOTES; n++) {
        const note_t *note = &CHIME_NOTES[chime][n];
        if (note->freq_hz > 0 && note->start_ms + note->length_ms > total_ms) {
            total_ms = note->start_ms + note->length_ms;
        }
    }
    size_t samples = SAMPLE_RATE * total_ms / 1000;
    int16_t *pcm = heap_caps_calloc(samples, sizeof(int16_t), MALLOC_CAP_SPIRAM);
    ESP_RETURN_ON_FALSE(pcm, ESP_ERR_NO_MEM, TAG, "no memory for chime");

    for (int n = 0; n < CHIME_MAX_NOTES; n++) {
        const note_t *note = &CHIME_NOTES[chime][n];
        if (note->freq_hz <= 0) {
            continue;
        }
        size_t start = SAMPLE_RATE * note->start_ms / 1000;
        size_t length = SAMPLE_RATE * note->length_ms / 1000;
        for (size_t i = 0; i < length && start + i < samples; i++) {
            float t = (float)i / SAMPLE_RATE;
            float attack = fminf(1.0f, t / 0.005f);
            float envelope = attack * expf(-6.0f * t / (note->length_ms / 1000.0f));
            float phase = 2.0f * (float)M_PI * note->freq_hz * t;
            float value = (sinf(phase) + 0.25f * sinf(2.0f * phase)) * envelope * CHIME_AMPLITUDE;
            int32_t mixed = pcm[start + i] + (int32_t)value;
            pcm[start + i] = (int16_t)(mixed > INT16_MAX ? INT16_MAX : mixed < INT16_MIN ? INT16_MIN : mixed);
        }
    }
    s_chimes[chime] = (clip_t){.pcm = pcm, .samples = samples, .owned = false};
    return ESP_OK;
}

/* The BSP configures I2S as stereo, so mono clips go out as duplicated L/R frames. */
static void clip_play(const clip_t *clip)
{
    int16_t frames[PLAY_CHUNK_FRAMES * 2];
    for (size_t done = 0; done < clip->samples;) {
        size_t n = clip->samples - done < PLAY_CHUNK_FRAMES ? clip->samples - done : PLAY_CHUNK_FRAMES;
        for (size_t i = 0; i < n; i++) {
            frames[2 * i] = frames[2 * i + 1] = clip->pcm[done + i];
        }
        if (esp_codec_dev_write(s_speaker, frames, n * 2 * sizeof(int16_t)) != ESP_CODEC_DEV_OK) {
            ESP_LOGW(TAG, "Speaker write failed");
            return;
        }
        done += n;
    }
}

static void playback_task(void *arg)
{
    clip_t clip;
    for (;;) {
        xQueueReceive(s_queue, &clip, portMAX_DELAY);
        clip_play(&clip);
        if (clip.owned) {
            heap_caps_free((void *)clip.pcm);
        }
    }
}

static void enqueue(const clip_t *clip)
{
    if (s_queue == NULL || xQueueSend(s_queue, clip, 0) != pdTRUE) {
        ESP_LOGW(TAG, "Playback queue full; dropping clip");
        if (clip->owned) {
            heap_caps_free((void *)clip->pcm);
        }
    }
}

esp_err_t audio_init(void)
{
    s_speaker = bsp_audio_codec_speaker_init();
    ESP_RETURN_ON_FALSE(s_speaker, ESP_FAIL, TAG, "speaker init failed");

    esp_codec_dev_sample_info_t fs = {
        .sample_rate = SAMPLE_RATE,
        .channel = 2,
        .bits_per_sample = 16,
    };
    ESP_RETURN_ON_FALSE(esp_codec_dev_open(s_speaker, &fs) == ESP_CODEC_DEV_OK, ESP_FAIL, TAG, "codec open failed");
    esp_codec_dev_set_out_vol(s_speaker, SPEAKER_VOLUME);

    for (int c = 0; c < AUDIO_CHIME_COUNT; c++) {
        ESP_RETURN_ON_ERROR(chime_render((audio_chime_t)c), TAG, "chime render failed");
    }

    s_queue = xQueueCreate(PLAY_QUEUE_LEN, sizeof(clip_t));
    ESP_RETURN_ON_FALSE(s_queue, ESP_ERR_NO_MEM, TAG, "queue create failed");
    ESP_RETURN_ON_FALSE(xTaskCreate(playback_task, "audio_play", 4096, NULL, 5, NULL) == pdPASS,
                        ESP_ERR_NO_MEM, TAG, "task create failed");
    return ESP_OK;
}

void audio_play_chime(audio_chime_t chime)
{
    if (chime < AUDIO_CHIME_COUNT && s_chimes[chime].pcm != NULL) {
        enqueue(&s_chimes[chime]);
    }
}

void audio_speech_begin(uint16_t id, size_t bytes)
{
    heap_caps_free(s_rx_pcm);
    s_rx_pcm = NULL;
    if (bytes == 0 || bytes > SPEECH_MAX_BYTES || (bytes & 1)) {
        ESP_LOGW(TAG, "Rejected speech clip of %u bytes", (unsigned)bytes);
        return;
    }
    /* Zeroed, so a chunk lost on the wire plays as a short silence instead of noise. */
    s_rx_pcm = heap_caps_calloc(1, bytes, MALLOC_CAP_SPIRAM);
    if (s_rx_pcm == NULL) {
        ESP_LOGW(TAG, "No memory for %u-byte speech clip", (unsigned)bytes);
        return;
    }
    s_rx_bytes = bytes;
    s_rx_id = id;
}

void audio_speech_data(uint16_t id, uint32_t offset, const uint8_t *pcm, size_t len)
{
    if (s_rx_pcm == NULL || id != s_rx_id || offset > s_rx_bytes || len > s_rx_bytes - offset) {
        return;
    }
    memcpy((uint8_t *)s_rx_pcm + offset, pcm, len);
}

void audio_speech_end(uint16_t id)
{
    if (s_rx_pcm == NULL || id != s_rx_id) {
        return;
    }
    clip_t clip = {.pcm = s_rx_pcm, .samples = s_rx_bytes / sizeof(int16_t), .owned = true};
    s_rx_pcm = NULL;
    enqueue(&clip);
}
