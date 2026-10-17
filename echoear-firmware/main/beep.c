#include "beep.h"

#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_codec_dev.h"
#include "bsp/esp-bsp.h"

static const char *TAG = "beep";

#define BEEP_SAMPLE_RATE  16000
#define BEEP_FREQ_HZ      1000
#define BEEP_DURATION_MS  150
#define BEEP_AMPLITUDE    8000
#define SPEAKER_VOLUME    60

static TaskHandle_t s_task;
static esp_codec_dev_handle_t s_speaker;
static int16_t *s_pcm;
static size_t s_pcm_bytes;

static void beep_task(void *arg)
{
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        if (esp_codec_dev_write(s_speaker, s_pcm, s_pcm_bytes) != ESP_CODEC_DEV_OK) {
            ESP_LOGW(TAG, "Speaker write failed");
        }
    }
}

esp_err_t beep_init(void)
{
    s_speaker = bsp_audio_codec_speaker_init();
    ESP_RETURN_ON_FALSE(s_speaker, ESP_FAIL, TAG, "speaker init failed");

    /* I2S is configured as stereo by the BSP, so play interleaved L/R frames. */
    esp_codec_dev_sample_info_t fs = {
        .sample_rate = BEEP_SAMPLE_RATE,
        .channel = 2,
        .bits_per_sample = 16,
    };
    ESP_RETURN_ON_FALSE(esp_codec_dev_open(s_speaker, &fs) == ESP_CODEC_DEV_OK, ESP_FAIL, TAG, "codec open failed");
    esp_codec_dev_set_out_vol(s_speaker, SPEAKER_VOLUME);

    const size_t frames = BEEP_SAMPLE_RATE * BEEP_DURATION_MS / 1000;
    s_pcm_bytes = frames * 2 * sizeof(int16_t);
    s_pcm = heap_caps_malloc(s_pcm_bytes, MALLOC_CAP_SPIRAM);
    ESP_RETURN_ON_FALSE(s_pcm, ESP_ERR_NO_MEM, TAG, "no memory for tone");
    for (size_t i = 0; i < frames; i++) {
        int16_t s = (int16_t)(BEEP_AMPLITUDE * sinf(2.0f * (float)M_PI * BEEP_FREQ_HZ * i / BEEP_SAMPLE_RATE));
        s_pcm[2 * i] = s;
        s_pcm[2 * i + 1] = s;
    }

    ESP_RETURN_ON_FALSE(xTaskCreate(beep_task, "beep", 4096, NULL, 5, &s_task) == pdPASS,
                        ESP_ERR_NO_MEM, TAG, "task create failed");
    return ESP_OK;
}

void beep_play(void)
{
    if (s_task) {
        xTaskNotifyGive(s_task);
    }
}
