/*
 * Hardware bring-up for ESP-VoCat (EchoEar) V1.2.
 *
 * Verifies each board subsystem on its own before the real application is built:
 *  - Display: ST77916 360x360 round LCD renders the two usage arcs at the screen edge.
 *  - Touch:   CST816S taps move a dot to the reported point (checks axis mapping).
 *  - PSRAM:   octal PSRAM size is shown on screen.
 *  - Speaker: ES8311 + PA plays a short beep at boot and on every tap.
 */
#include <inttypes.h>
#include <math.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_codec_dev.h"
#include "bsp/esp-bsp.h"
#include "lvgl.h"

static const char *TAG = "bringup";

#define COLOR_BG          0x0B0F19
#define COLOR_ARC_TRACK   0x1F2937
#define COLOR_CLAUDE      0xD97757
#define COLOR_CODEX       0x10A37F

#define ARC_WIDTH_PX      10
#define TOUCH_DOT_PX      16

#define BEEP_SAMPLE_RATE  16000
#define BEEP_FREQ_HZ      1000
#define BEEP_DURATION_MS  150
#define BEEP_AMPLITUDE    8000
#define SPEAKER_VOLUME    60

static TaskHandle_t s_beep_task;
static lv_obj_t *s_touch_label;
static lv_obj_t *s_touch_dot;
static uint32_t s_touch_count;

/* Plays a precomputed sine burst each time the task is notified. */
static void beep_task(void *arg)
{
    esp_codec_dev_handle_t speaker = arg;

    /* I2S is configured as stereo by the BSP, so write interleaved L/R frames. */
    const size_t frames = BEEP_SAMPLE_RATE * BEEP_DURATION_MS / 1000;
    const size_t bytes = frames * 2 * sizeof(int16_t);
    int16_t *pcm = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM);
    if (pcm == NULL) {
        ESP_LOGE(TAG, "No memory for beep buffer");
        vTaskDelete(NULL);
        return;
    }
    for (size_t i = 0; i < frames; i++) {
        int16_t s = (int16_t)(BEEP_AMPLITUDE * sinf(2.0f * (float)M_PI * BEEP_FREQ_HZ * i / BEEP_SAMPLE_RATE));
        pcm[2 * i] = s;
        pcm[2 * i + 1] = s;
    }

    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        if (esp_codec_dev_write(speaker, pcm, bytes) != ESP_CODEC_DEV_OK) {
            ESP_LOGW(TAG, "Speaker write failed");
        }
    }
}

static esp_codec_dev_handle_t speaker_open(void)
{
    esp_codec_dev_handle_t speaker = bsp_audio_codec_speaker_init();
    if (speaker == NULL) {
        return NULL;
    }
    esp_codec_dev_sample_info_t fs = {
        .sample_rate = BEEP_SAMPLE_RATE,
        .channel = 2,
        .bits_per_sample = 16,
    };
    if (esp_codec_dev_open(speaker, &fs) != ESP_CODEC_DEV_OK) {
        return NULL;
    }
    esp_codec_dev_set_out_vol(speaker, SPEAKER_VOLUME);
    return speaker;
}

/*
 * Usage arc hugging the round screen edge. LVGL angles start at 3 o'clock and grow
 * clockwise, so 135..225 is the left edge and 315..45 the right edge.
 */
static lv_obj_t *usage_arc_create(lv_obj_t *parent, uint16_t start_deg, uint16_t end_deg,
                                  uint32_t color, lv_arc_mode_t mode)
{
    lv_obj_t *arc = lv_arc_create(parent);
    lv_obj_set_size(arc, BSP_LCD_H_RES, BSP_LCD_V_RES);
    lv_obj_center(arc);
    lv_arc_set_bg_angles(arc, start_deg, end_deg);
    lv_arc_set_mode(arc, mode);
    lv_arc_set_range(arc, 0, 100);
    lv_obj_remove_style(arc, NULL, LV_PART_KNOB);
    lv_obj_set_clickable(arc, false);
    lv_obj_set_style_arc_width(arc, ARC_WIDTH_PX, LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc, ARC_WIDTH_PX, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(arc, lv_color_hex(COLOR_ARC_TRACK), LV_PART_MAIN);
    lv_obj_set_style_arc_color(arc, lv_color_hex(color), LV_PART_INDICATOR);
    return arc;
}

static void screen_pressed_cb(lv_event_t *e)
{
    lv_point_t p;
    lv_indev_get_point(lv_indev_active(), &p);

    s_touch_count++;
    lv_label_set_text_fmt(s_touch_label, "Touch #%" PRIu32 "  (%d, %d)",
                          s_touch_count, (int)p.x, (int)p.y);
    lv_obj_set_pos(s_touch_dot, p.x - TOUCH_DOT_PX / 2, p.y - TOUCH_DOT_PX / 2);
    lv_obj_set_hidden(s_touch_dot, false);

    if (s_beep_task) {
        xTaskNotifyGive(s_beep_task);
    }
}

/* Must be called with the LVGL lock held. */
static void bringup_screen_create(void)
{
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_hex(COLOR_BG), 0);
    lv_obj_set_style_text_color(scr, lv_color_white(), 0);
    lv_obj_set_clickable(scr, true);
    lv_obj_add_event_cb(scr, screen_pressed_cb, LV_EVENT_PRESSED, NULL);

    /* Static test values: left fills bottom-up, right fills bottom-up via reverse mode. */
    lv_arc_set_value(usage_arc_create(scr, 135, 225, COLOR_CLAUDE, LV_ARC_MODE_NORMAL), 72);
    lv_arc_set_value(usage_arc_create(scr, 315, 45, COLOR_CODEX, LV_ARC_MODE_REVERSE), 38);

    lv_obj_t *title = lv_label_create(scr);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_label_set_text(title, "ESP-VoCat bring-up");
    lv_obj_align(title, LV_ALIGN_CENTER, 0, -40);

    lv_obj_t *mem = lv_label_create(scr);
    lv_label_set_text_fmt(mem, "PSRAM %u KB  /  SRAM %u KB free",
                          (unsigned)(heap_caps_get_total_size(MALLOC_CAP_SPIRAM) / 1024),
                          (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024));
    lv_obj_align(mem, LV_ALIGN_CENTER, 0, 0);

    s_touch_label = lv_label_create(scr);
    lv_label_set_text(s_touch_label, "Tap anywhere");
    lv_obj_align(s_touch_label, LV_ALIGN_CENTER, 0, 30);

    s_touch_dot = lv_obj_create(scr);
    lv_obj_set_size(s_touch_dot, TOUCH_DOT_PX, TOUCH_DOT_PX);
    lv_obj_set_style_radius(s_touch_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s_touch_dot, lv_color_white(), 0);
    lv_obj_set_style_border_width(s_touch_dot, 0, 0);
    lv_obj_set_clickable(s_touch_dot, false);
    lv_obj_set_hidden(s_touch_dot, true);
}

void app_main(void)
{
    ESP_LOGI(TAG, "PSRAM total: %u KB", (unsigned)(heap_caps_get_total_size(MALLOC_CAP_SPIRAM) / 1024));

    /*
     * Power the ES8311 now: the BSP only enables it right before the first I2C access,
     * which the codec NAKs while still powering up. Display init gives it time to settle.
     */
    ESP_ERROR_CHECK(bsp_feature_enable(BSP_FEATURE_SPEAKER, true));

    /* Display first: it also brings up the shared I2C bus used by touch and the codecs. */
    lv_display_t *disp = bsp_display_start();
    if (disp == NULL) {
        ESP_LOGE(TAG, "Display init failed");
        return;
    }
    bsp_display_backlight_on();

    bsp_display_lock(0);
    bringup_screen_create();
    bsp_display_unlock();

    esp_codec_dev_handle_t speaker = speaker_open();
    if (speaker == NULL) {
        ESP_LOGE(TAG, "Speaker init failed; display/touch test continues without sound");
        return;
    }
    xTaskCreate(beep_task, "beep", 4096, speaker, 5, &s_beep_task);
    xTaskNotifyGive(s_beep_task);
}
