/*
 * EchoEar (ESP-VoCat V1.2) agent display: shows Claude Code / Codex usage and lets
 * the user answer Claude Code permission requests by touch. Messages arrive from
 * pc-bridge over USB; see docs/device-protocol.md.
 */
#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "cJSON.h"
#include "esp_app_desc.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "bsp/esp-bsp.h"

#include "beep.h"
#include "host_link.h"
#include "ui.h"

static const char *TAG = "app";

/* The bridge pings every 2 s; consider it gone after a few missed pings. */
#define HOST_TIMEOUT_US  (5 * 1000 * 1000)

static volatile int64_t s_last_host_us;

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

static void handle_event(const cJSON *msg)
{
    const char *kind = json_str(msg, "kind");
    ui_event_kind_t event_kind = strcmp(kind, "error") == 0       ? UI_EVENT_ERROR
                                 : strcmp(kind, "attention") == 0 ? UI_EVENT_ATTENTION
                                                                  : UI_EVENT_DONE;
    ui_show_event(json_str(msg, "src"), event_kind, json_str(msg, "text"));
    beep_play();
}

static void handle_confirm_request(const cJSON *msg)
{
    const char *risk = json_str(msg, "risk");
    ui_risk_t ui_risk = strcmp(risk, "high") == 0 ? UI_RISK_HIGH
                        : strcmp(risk, "low") == 0 ? UI_RISK_LOW
                                                   : UI_RISK_MEDIUM;
    ui_show_confirm((uint32_t)json_int(msg, "id", 0), json_str(msg, "tool"), json_str(msg, "summary"),
                    ui_risk, json_str(msg, "project"));
    beep_play();
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

    if (beep_init() != ESP_OK) {
        ESP_LOGE(TAG, "Speaker unavailable; continuing without sound");
    }

    ESP_ERROR_CHECK(host_link_start(on_host_message));
    /* Announce ourselves in case the bridge is already connected (e.g. after a reset). */
    send_hello();
}
