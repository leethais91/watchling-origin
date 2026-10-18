#include "audio.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_codec_dev.h"
#include "bsp/esp-bsp.h"
#include "assets/sound_effects.h"

static const char *TAG = "audio";

#define SAMPLE_RATE        16000
/* 15 s of speech. Keep in sync with MAX_SPEECH_BYTES in pc-bridge/src/speech/speaker.ts. */
#define SPEECH_MAX_BYTES   (15 * SAMPLE_RATE * 2)
#define PLAY_QUEUE_LEN     8
#define PLAY_CHUNK_FRAMES  256

typedef struct {
    const int16_t *pcm; /* mono */
    size_t samples;
    bool owned;         /* heap clip to free after playing */
} clip_t;

static const sound_effect_t *const CHIME_SOUNDS[AUDIO_CHIME_COUNT] = {
    [AUDIO_CHIME_CONFIRM] = &SFX_CONFIRM,
    [AUDIO_CHIME_DONE] = &SFX_DONE,
    [AUDIO_CHIME_ATTENTION] = &SFX_ATTENTION,
    [AUDIO_CHIME_ERROR] = &SFX_ERROR,
};

static esp_codec_dev_handle_t s_speaker;
static QueueHandle_t s_queue;
static int s_volume = 60;
/* Written from the LVGL task, read on the host link RX task. */
static volatile bool s_speech_enabled = true;

/* Speech clip being received (host link RX task only). */
static int16_t *s_rx_pcm;
static size_t s_rx_bytes;
static uint16_t s_rx_id;

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
    esp_codec_dev_set_out_vol(s_speaker, s_volume);

    s_queue = xQueueCreate(PLAY_QUEUE_LEN, sizeof(clip_t));
    ESP_RETURN_ON_FALSE(s_queue, ESP_ERR_NO_MEM, TAG, "queue create failed");
    ESP_RETURN_ON_FALSE(xTaskCreate(playback_task, "audio_play", 4096, NULL, 5, NULL) == pdPASS,
                        ESP_ERR_NO_MEM, TAG, "task create failed");
    return ESP_OK;
}

void audio_set_volume(int percent)
{
    s_volume = percent;
    if (s_speaker != NULL) {
        esp_codec_dev_set_out_vol(s_speaker, percent);
    }
}

void audio_set_speech_enabled(bool enabled)
{
    s_speech_enabled = enabled;
}

void audio_play_chime(audio_chime_t chime)
{
    if (chime < AUDIO_CHIME_COUNT) {
        const sound_effect_t *sfx = CHIME_SOUNDS[chime];
        clip_t clip = {.pcm = sfx->pcm, .samples = sfx->samples, .owned = false};
        enqueue(&clip);
    }
}

void audio_speech_begin(uint16_t id, size_t bytes)
{
    heap_caps_free(s_rx_pcm);
    s_rx_pcm = NULL;
    if (!s_speech_enabled) {
        return; /* voice turned off on the device; the bridge stops sending once it hears */
    }
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
