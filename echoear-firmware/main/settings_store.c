#include "settings_store.h"

#include "esp_log.h"
#include "nvs.h"

static const char *TAG = "settings";

#define NAMESPACE          "settings"
#define DEFAULT_BRIGHTNESS 100
#define DEFAULT_VOLUME     60
#define DEFAULT_VOICE      true
/* Bluetooth is opt-in: nothing advertises until the user switches it on. */
#define DEFAULT_BLE        false

static int clamp(int value, int min, int max)
{
    return value < min ? min : value > max ? max : value;
}

void settings_store_load(settings_values_t *out)
{
    *out = (settings_values_t){
        .brightness = DEFAULT_BRIGHTNESS,
        .volume = DEFAULT_VOLUME,
        .voice = DEFAULT_VOICE,
        .ble = DEFAULT_BLE,
    };
    nvs_handle_t nvs;
    if (nvs_open(NAMESPACE, NVS_READONLY, &nvs) != ESP_OK) {
        return; /* nothing saved yet */
    }
    uint8_t value;
    if (nvs_get_u8(nvs, "brightness", &value) == ESP_OK) {
        out->brightness = clamp(value, SETTINGS_BRIGHTNESS_MIN, 100);
    }
    if (nvs_get_u8(nvs, "volume", &value) == ESP_OK) {
        out->volume = clamp(value, 0, 100);
    }
    if (nvs_get_u8(nvs, "voice", &value) == ESP_OK) {
        out->voice = value != 0;
    }
    if (nvs_get_u8(nvs, "ble", &value) == ESP_OK) {
        out->ble = value != 0;
    }
    nvs_close(nvs);
}

void settings_store_save(const settings_values_t *values)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(NAMESPACE, NVS_READWRITE, &nvs);
    if (err == ESP_OK) {
        nvs_set_u8(nvs, "brightness", (uint8_t)values->brightness);
        nvs_set_u8(nvs, "volume", (uint8_t)values->volume);
        nvs_set_u8(nvs, "voice", values->voice);
        nvs_set_u8(nvs, "ble", values->ble);
        err = nvs_commit(nvs);
        nvs_close(nvs);
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Settings not saved: %s", esp_err_to_name(err));
    }
}
