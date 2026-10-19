/*
 * Watchling agent display on the EchoEar (ESP-VoCat V1.2) board: shows Claude Code / Codex usage and lets
 * the user answer Claude Code / Codex permission requests by touch. Messages arrive from
 * pc-bridge over USB or BLE; see docs/device-protocol.md.
 */
#include <stdio.h>
#include <string.h>
#include <sys/time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "cJSON.h"
#include "esp_app_desc.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "bsp/esp-bsp.h"

#include "audio.h"
#include "battery.h"
#include "ble_link.h"
#include "host_link.h"
#include "imu.h"
#include "settings_store.h"
#include "status_led.h"
#include "ui.h"

static const char *TAG = "app";

#define BATTERY_POLL_US  (30 * 1000 * 1000)
/* A turn often starts right after another; one "working" sound per spell is enough. */
#define WORKING_CHIME_COOLDOWN_US (30 * 1000 * 1000)

/* Written in the LVGL task; the RX task only reads `voice` for the hello reply. */
static settings_values_t s_settings;

/*
 * Quiet mode: no chimes and no speech while the robot lies on its face. Written on the
 * esp_timer task (IMU poll); the RX and LVGL tasks only read it.
 */
static volatile bool s_quiet;

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

static ui_link_state_t link_state(void)
{
    return (ui_link_state_t){.usb = host_link_alive(HOST_LINK_USB), .ble = host_link_alive(HOST_LINK_BLE)};
}

/* Bonds and settings live in NVS; a full or older-format partition is wiped. */
static esp_err_t nvs_start(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    return err;
}

/* Whether the bridge should synthesize speech at all: the Voice setting, unless quiet. */
static bool voice_wanted(void)
{
    return s_settings.voice && !s_quiet;
}

static void send_voice(void)
{
    host_link_send_json(voice_wanted() ? "{\"t\":\"settings\",\"voice\":true}"
                                       : "{\"t\":\"settings\",\"voice\":false}");
}

static void send_hello(void)
{
    char json[128];
    snprintf(json, sizeof(json), "{\"t\":\"hello\",\"fw\":\"%s\",\"voice\":%s}",
             esp_app_get_description()->version, voice_wanted() ? "true" : "false");
    host_link_send_json(json);
}

/* esp_timer task. The bridge hears "voice off", so it stops sending speech meanwhile. */
static void quiet_set(bool quiet)
{
    if (quiet == s_quiet) {
        return;
    }
    s_quiet = quiet;
    audio_set_muted(quiet);
    send_voice();
    ui_set_quiet(quiet);
    ESP_LOGI(TAG, "Quiet mode %s", quiet ? "on" : "off");
}

/* esp_timer task (IMU poll). */
static void on_imu(imu_event_t event)
{
    static const char *const NAMES[] = {"face down", "face up", "moved",  "tilt left",
                                        "tilt right", "level", "shaken", "on back", "off back"};
    ESP_LOGI(TAG, "Body: %s", NAMES[event]);
    switch (event) {
    case IMU_EVENT_FACE_DOWN:
    case IMU_EVENT_FACE_UP:
        quiet_set(event == IMU_EVENT_FACE_DOWN);
        break;
    case IMU_EVENT_MOVED:
        ui_body_event(UI_BODY_MOVED);
        break;
    case IMU_EVENT_TILT_LEFT:
        ui_body_event(UI_BODY_TILT_LEFT);
        break;
    case IMU_EVENT_TILT_RIGHT:
        ui_body_event(UI_BODY_TILT_RIGHT);
        break;
    case IMU_EVENT_LEVEL:
        ui_body_event(UI_BODY_LEVEL);
        break;
    case IMU_EVENT_SHAKEN:
        ui_body_event(UI_BODY_SHAKEN);
        break;
    case IMU_EVENT_ON_BACK:
        ui_body_event(UI_BODY_ON_BACK);
        break;
    case IMU_EVENT_OFF_BACK:
        ui_body_event(UI_BODY_OFF_BACK);
        break;
    }
}

/* The LED and the IMU are extras: each one missing only costs its own feature. */
static void body_sensors_start(void)
{
    if (status_led_init() == ESP_OK) {
        ui_set_needs_user_cb(status_led_set_attention);
    } else {
        ESP_LOGW(TAG, "Status LED unavailable");
    }
    esp_err_t err = imu_start(on_imu);
    if (err == ESP_OK) {
        ui_set_gravity_probe(imu_gravity);
    } else {
        ESP_LOGW(TAG, "Accelerometer unavailable (%s)", esp_err_to_name(err));
    }
}

/* LVGL task. Brightness follows the finger; volume changes once it lifts, with a sample chime. */
static void on_settings(settings_field_t field, const settings_values_t *values, bool final)
{
    s_settings = *values;
    switch (field) {
    case SETTINGS_FIELD_BRIGHTNESS:
        bsp_display_brightness_set(values->brightness);
        break;
    case SETTINGS_FIELD_VOLUME:
        if (final) {
            audio_set_volume(values->volume);
            audio_play_chime(AUDIO_CHIME_DONE);
        }
        break;
    case SETTINGS_FIELD_VOICE:
        audio_set_speech_enabled(values->voice);
        send_voice();
        break;
    }
    if (final) {
        settings_store_save(values);
    }
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

/*
 * Faces a speech clip may ask for (say.mood / say.look), packed into the clip's audio tag:
 * mood index + 1 in the low byte (0 = no face change), look index in the next byte.
 */
static const char *const SPEECH_MOODS[] = {"happy", "surprised", "sleep", "idle"};
static const char *const SPEECH_LOOKS[] = {"center", "left", "right"};

static int name_index(const char *const *names, size_t count, const char *name)
{
    for (size_t i = 0; name != NULL && i < count; i++) {
        if (strcmp(names[i], name) == 0) {
            return (int)i;
        }
    }
    return -1;
}

static uint32_t speech_face_tag(const cJSON *msg)
{
    int mood = name_index(SPEECH_MOODS, sizeof(SPEECH_MOODS) / sizeof(SPEECH_MOODS[0]), json_str(msg, "mood"));
    int look = name_index(SPEECH_LOOKS, sizeof(SPEECH_LOOKS) / sizeof(SPEECH_LOOKS[0]), json_str(msg, "look"));
    return mood < 0 ? 0 : (uint32_t)(mood + 1) | (uint32_t)(look < 0 ? 0 : look) << 8;
}

/* Playback task: the clip's face shows for as long as the clip plays. */
static void on_speech_start(uint32_t tag, uint32_t duration_ms)
{
    ui_show_speech_face(SPEECH_MOODS[(tag & 0xff) - 1], SPEECH_LOOKS[(tag >> 8) & 0xff], duration_ms);
}

static void handle_confirm_request(const cJSON *msg)
{
    const char *risk = json_str(msg, "risk");
    ui_risk_t ui_risk = strcmp(risk, "high") == 0 ? UI_RISK_HIGH
                        : strcmp(risk, "low") == 0 ? UI_RISK_LOW
                                                   : UI_RISK_MEDIUM;
    ui_show_confirm((uint32_t)json_int(msg, "id", 0), json_str(msg, "src"), json_str(msg, "tool"),
                    json_str(msg, "summary"), ui_risk);
    audio_play_chime(AUDIO_CHIME_ATTENTION);
}

static void handle_activity(const cJSON *msg)
{
    static int64_t last_chime_us;
    bool busy = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(msg, "busy"));
    ui_set_activity(json_str(msg, "src"), busy);
    int64_t now = esp_timer_get_time();
    if (busy && (last_chime_us == 0 || now - last_chime_us >= WORKING_CHIME_COOLDOWN_US)) {
        last_chime_us = now;
        audio_play_chime(AUDIO_CHIME_WORKING);
    }
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
        audio_speech_begin((uint16_t)json_int(msg, "id", 0), (size_t)json_int(msg, "bytes", 0), speech_face_tag(msg));
    } else if (strcmp(type, "say_end") == 0) {
        audio_speech_end((uint16_t)json_int(msg, "id", 0));
    } else if (strcmp(type, "activity") == 0) {
        handle_activity(msg);
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

    esp_err_t nvs_err = nvs_start();
    settings_store_load(&s_settings);

    /* Display first: it also brings up the shared I2C bus used by touch and the codecs. */
    if (bsp_display_start() == NULL) {
        ESP_LOGE(TAG, "Display init failed");
        return;
    }
    bsp_display_brightness_set(s_settings.brightness);
    ui_init(on_decision, link_state, &s_settings, on_settings);
    battery_start();

    audio_set_volume(s_settings.volume);
    audio_set_speech_enabled(s_settings.voice);
    audio_set_speech_start_cb(on_speech_start);
    if (audio_init() == ESP_OK) {
        audio_play_chime(AUDIO_CHIME_BOOT);
    } else {
        ESP_LOGE(TAG, "Speaker unavailable; continuing without sound");
    }
    body_sensors_start();

    ESP_ERROR_CHECK(host_link_start(on_host_message, on_host_audio));
    if (nvs_err != ESP_OK || ble_link_start(host_link_ble_rx, ui_show_pairing) != ESP_OK) {
        ESP_LOGE(TAG, "Bluetooth unavailable; USB only");
    }
    ESP_LOGI(TAG, "Free internal RAM: %u KB (largest block %u KB)",
             (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024),
             (unsigned)(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) / 1024));
    /* Announce ourselves in case the bridge is already connected (e.g. after a reset). */
    send_hello();
}
