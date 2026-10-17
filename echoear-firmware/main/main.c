/*
 * EchoEar (ESP-VoCat V1.2) agent display: shows Claude Code / Codex usage and lets
 * the user answer Claude Code permission requests by touch. Messages arrive from
 * pc-bridge over USB; see docs/device-protocol.md.
 */
#include <stdio.h>
#include <string.h>
#include <sys/time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "cJSON.h"
#include "esp_app_desc.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "bsp/esp-bsp.h"

#include "audio.h"
#include "battery.h"
#include "host_link.h"
#include "ui.h"

static const char *TAG = "app";

#define BATTERY_POLL_US  (30 * 1000 * 1000)

/* The bridge pings every 2 s; consider it gone after a few missed pings. */
#define HOST_TIMEOUT_US  (5 * 1000 * 1000)

static volatile int64_t s_last_host_us;

/* esp_timer task; one short I2C transaction pair every 30 s. */
static void battery_poll(void *arg)
{
    battery_state_t state;
    if (battery_read(&state) == ESP_OK) {
        ui_set_battery(state.percent, state.charging);
    } else {
        ui_set_battery(-1, false);
    }
}

static void battery_start(void)
{
    esp_err_t err = battery_init();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Fuel gauge not found (%s); hiding battery", esp_err_to_name(err));
        return;
    }
    battery_state_t state;
    if (battery_read(&state) == ESP_OK) {
        ESP_LOGI(TAG, "Fuel gauge: %d%%%s", state.percent, state.charging ? ", charging" : "");
    }
    battery_poll(NULL);
    const esp_timer_create_args_t args = {.callback = battery_poll, .name = "battery"};
    esp_timer_handle_t timer;
    if (esp_timer_create(&args, &timer) == ESP_OK) {
        esp_timer_start_periodic(timer, BATTERY_POLL_US);
    }
}

static bool host_is_linked(void)
{
    return s_last_host_us != 0 && esp_timer_get_time() - s_last_host_us < HOST_TIMEOUT_US;
}

static void send_hello(void)
{
    char json[96];
    snprintf(json, sizeof(json), "{\"t\":\"hello\",\"fw\":\"%s\"}", esp_app_get_description()->version);
    host_link_send_json(json);
}

static void on_decision(uint32_t id, bool approve)
{
    char json[96];
    snprintf(json, sizeof(json), "{\"t\":\"confirm_res\",\"id\":%lu,\"action\":\"%s\",\"src\":\"touch\"}",
             (unsigned long)id, approve ? "approve" : "deny");
    if (!host_link_send_json(json)) {
        ESP_LOGW(TAG, "Decision for #%lu not sent", (unsigned long)id);
    }
}

static int json_int(const cJSON *obj, const char *key, int fallback)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, key);
    return cJSON_IsNumber(item) ? item->valueint : fallback;
}

static const char *json_str(const cJSON *obj, const char *key)
{
    const char *value = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(obj, key));
    return value ? value : "";
}

static void handle_usage(const cJSON *msg)
{
    const cJSON *claude = cJSON_GetObjectItemCaseSensitive(msg, "claude");
    const cJSON *codex = cJSON_GetObjectItemCaseSensitive(msg, "codex");
    const cJSON *cost = cJSON_GetObjectItemCaseSensitive(claude, "costToday");
    ui_usage_t usage = {
        .claude_h5 = json_int(claude, "h5", -1),
        .claude_d7 = json_int(claude, "d7", -1),
        .codex_h5 = json_int(codex, "h5", -1),
        .codex_d7 = json_int(codex, "d7", -1),
        .cost_today = cJSON_IsNumber(cost) ? (float)cost->valuedouble : -1.0f,
    };
    ui_set_usage(&usage);
}

/* The bridge sends UTC seconds plus its local offset; the RTC keeps counting between syncs. */
static void handle_time(const cJSON *msg)
{
    const cJSON *epoch = cJSON_GetObjectItemCaseSensitive(msg, "epoch");
    if (!cJSON_IsNumber(epoch) || epoch->valuedouble <= 0) {
        return;
    }
    struct timeval tv = {.tv_sec = (time_t)epoch->valuedouble};
    settimeofday(&tv, NULL);
    ui_set_clock_offset(json_int(msg, "tz", 0));
}

static void handle_event(const cJSON *msg)
{
    const char *kind = json_str(msg, "kind");
    ui_event_kind_t event_kind = strcmp(kind, "error") == 0       ? UI_EVENT_ERROR
                                 : strcmp(kind, "attention") == 0 ? UI_EVENT_ATTENTION
                                                                  : UI_EVENT_DONE;
    static const audio_chime_t chimes[] = {
        [UI_EVENT_DONE] = AUDIO_CHIME_DONE,
        [UI_EVENT_ATTENTION] = AUDIO_CHIME_ATTENTION,
        [UI_EVENT_ERROR] = AUDIO_CHIME_ERROR,
    };
    ui_show_event(json_str(msg, "src"), event_kind, json_str(msg, "text"));
    audio_play_chime(chimes[event_kind]);
}

static void handle_confirm_request(const cJSON *msg)
{
    const char *risk = json_str(msg, "risk");
    ui_risk_t ui_risk = strcmp(risk, "high") == 0 ? UI_RISK_HIGH
                        : strcmp(risk, "low") == 0 ? UI_RISK_LOW
                                                   : UI_RISK_MEDIUM;
    ui_show_confirm((uint32_t)json_int(msg, "id", 0), json_str(msg, "tool"), json_str(msg, "summary"),
                    ui_risk, json_str(msg, "project"));
    audio_play_chime(AUDIO_CHIME_CONFIRM);
}

/* Runs on the host link RX task, like the speech begin/end messages. */
static void on_host_audio(const uint8_t *payload, size_t len)
{
    if (len <= 6) {
        return;
    }
    uint16_t id = (uint16_t)(payload[0] | payload[1] << 8);
    uint32_t offset = (uint32_t)payload[2] | (uint32_t)payload[3] << 8 | (uint32_t)payload[4] << 16 |
                      (uint32_t)payload[5] << 24;
    audio_speech_data(id, offset, payload + 6, len - 6);
}

/* Runs on the host link RX task. */
static void on_host_message(const char *json, size_t len)
{
    cJSON *msg = cJSON_ParseWithLength(json, len);
    if (msg == NULL) {
        ESP_LOGW(TAG, "Malformed JSON from host");
        return;
    }
    s_last_host_us = esp_timer_get_time();

    const char *type = json_str(msg, "t");
    if (strcmp(type, "ping") == 0) {
        host_link_send_json("{\"t\":\"pong\"}");
    } else if (strcmp(type, "hello") == 0) {
        send_hello();
    } else if (strcmp(type, "usage") == 0) {
        handle_usage(msg);
    } else if (strcmp(type, "time") == 0) {
        handle_time(msg);
    } else if (strcmp(type, "say") == 0) {
        audio_speech_begin((uint16_t)json_int(msg, "id", 0), (size_t)json_int(msg, "bytes", 0));
    } else if (strcmp(type, "say_end") == 0) {
        audio_speech_end((uint16_t)json_int(msg, "id", 0));
    } else if (strcmp(type, "event") == 0) {
        handle_event(msg);
    } else if (strcmp(type, "confirm_req") == 0) {
        handle_confirm_request(msg);
    } else if (strcmp(type, "confirm_cancel") == 0) {
        ui_cancel_confirm((uint32_t)json_int(msg, "id", 0));
    }
    cJSON_Delete(msg);
}

void app_main(void)
{
    /*
     * Power the ES8311 now: the BSP only enables it right before the first I2C access,
     * which the codec NAKs while still powering up. Display init gives it time to settle.
     */
    ESP_ERROR_CHECK(bsp_feature_enable(BSP_FEATURE_SPEAKER, true));

    /* Display first: it also brings up the shared I2C bus used by touch and the codecs. */
    if (bsp_display_start() == NULL) {
        ESP_LOGE(TAG, "Display init failed");
        return;
    }
    bsp_display_backlight_on();
    ui_init(on_decision, host_is_linked);
    battery_start();

    if (audio_init() != ESP_OK) {
        ESP_LOGE(TAG, "Speaker unavailable; continuing without sound");
    }

    ESP_ERROR_CHECK(host_link_start(on_host_message, on_host_audio));
    /* Announce ourselves in case the bridge is already connected (e.g. after a reset). */
    send_hello();
}
