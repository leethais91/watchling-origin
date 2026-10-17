#include "host_link.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "driver/usb_serial_jtag.h"
#include "driver/usb_serial_jtag_vfs.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "ble_link.h"
#include "frame_codec.h"

static const char *TAG = "host_link";

/* Matches MAX_FRAME_BYTES in pc-bridge/src/protocol/frame-codec.ts. */
#define FRAME_MAX_BYTES     4096
#define LINK_TX_BUFFER      8192
#define LINK_RX_BUFFER      4096
#define LINK_TX_TIMEOUT_MS  100
#define RX_TASK_STACK       6144
/* The bridge pings every 2 s; consider a link gone after a few missed pings. */
#define LINK_TIMEOUT_US     (5 * 1000 * 1000)

/* Frame assembly state for one link; only touched by that link's RX task. */
typedef struct {
    host_link_t link;
    uint8_t raw[FRAME_COBS_MAX_ENCODED(FRAME_MAX_BYTES)];
    uint8_t body[FRAME_COBS_MAX_ENCODED(FRAME_MAX_BYTES) + 1];
    size_t raw_len;
    bool overflow;
} frame_rx_t;

static host_link_rx_cb_t s_on_message;
static host_link_audio_cb_t s_on_audio;
static SemaphoreHandle_t s_tx_lock;
static frame_rx_t s_rx[HOST_LINK_COUNT] = {
    [HOST_LINK_USB] = {.link = HOST_LINK_USB},
    [HOST_LINK_BLE] = {.link = HOST_LINK_BLE},
};
static volatile int64_t s_last_rx_us[HOST_LINK_COUNT];

/* TX scratch (guarded by s_tx_lock). */
static uint8_t s_tx_body[FRAME_MAX_BYTES];
static uint8_t s_tx_frame[FRAME_COBS_MAX_ENCODED(FRAME_MAX_BYTES) + 2];

static void handle_raw_frame(frame_rx_t *rx)
{
    size_t body_len = frame_cobs_decode(rx->raw, rx->raw_len, rx->body);
    if (body_len < 3) {
        return;
    }
    uint16_t crc = (uint16_t)(rx->body[body_len - 2] << 8) | rx->body[body_len - 1];
    if (frame_crc16(rx->body, body_len - 2) != crc) {
        ESP_LOGW(TAG, "Dropped frame with bad CRC");
        return;
    }
    s_last_rx_us[rx->link] = esp_timer_get_time();
    if (rx->body[0] == FRAME_TYPE_AUDIO) {
        s_on_audio(&rx->body[1], body_len - 3);
        return;
    }
    if (rx->body[0] != FRAME_TYPE_JSON) {
        return;
    }
    size_t json_len = body_len - 3;
    char *json = (char *)&rx->body[1];
    json[json_len] = '\0'; /* overwrites the CRC, already checked */
    s_on_message(json, json_len);
}

/* Frames are delimited by 0x00 on both links. */
static void frame_rx_feed(frame_rx_t *rx, const uint8_t *data, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        if (data[i] == 0) {
            if (rx->raw_len > 0 && !rx->overflow) {
                handle_raw_frame(rx);
            }
            rx->raw_len = 0;
            rx->overflow = false;
        } else if (rx->raw_len < sizeof(rx->raw)) {
            rx->raw[rx->raw_len++] = data[i];
        } else {
            rx->overflow = true; /* drop until the next delimiter */
        }
    }
}

static void usb_rx_task(void *arg)
{
    uint8_t chunk[256];
    for (;;) {
        int n = usb_serial_jtag_read_bytes(chunk, sizeof(chunk), portMAX_DELAY);
        if (n > 0) {
            frame_rx_feed(&s_rx[HOST_LINK_USB], chunk, (size_t)n);
        }
    }
}

esp_err_t host_link_start(host_link_rx_cb_t on_message, host_link_audio_cb_t on_audio)
{
    s_on_message = on_message;
    s_on_audio = on_audio;
    s_tx_lock = xSemaphoreCreateMutex();
    if (s_tx_lock == NULL) {
        return ESP_ERR_NO_MEM;
    }

    usb_serial_jtag_driver_config_t config = {
        .tx_buffer_size = LINK_TX_BUFFER,
        .rx_buffer_size = LINK_RX_BUFFER,
    };
    ESP_RETURN_ON_ERROR(usb_serial_jtag_driver_install(&config), TAG, "driver install failed");
    /* Route console/log output through the same driver so it cannot race our frame writes in the FIFO. */
    usb_serial_jtag_vfs_use_driver();

    if (xTaskCreate(usb_rx_task, "host_link_rx", RX_TASK_STACK, NULL, 6, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

void host_link_ble_rx(const uint8_t *data, size_t len)
{
    frame_rx_feed(&s_rx[HOST_LINK_BLE], data, len);
}

bool host_link_alive(host_link_t link)
{
    int64_t last = s_last_rx_us[link];
    return last != 0 && esp_timer_get_time() - last < LINK_TIMEOUT_US;
}

bool host_link_send_json(const char *json)
{
    size_t json_len = strlen(json);
    if (json_len + 3 > FRAME_MAX_BYTES) {
        return false;
    }

    xSemaphoreTake(s_tx_lock, portMAX_DELAY);
    s_tx_body[0] = FRAME_TYPE_JSON;
    memcpy(&s_tx_body[1], json, json_len);
    uint16_t crc = frame_crc16(s_tx_body, json_len + 1);
    s_tx_body[json_len + 1] = crc >> 8;
    s_tx_body[json_len + 2] = crc & 0xFF;

    s_tx_frame[0] = 0;
    size_t encoded = frame_cobs_encode(s_tx_body, json_len + 3, &s_tx_frame[1]);
    s_tx_frame[encoded + 1] = 0;
    size_t total = encoded + 2;

    bool sent = false;
    if (usb_serial_jtag_is_connected()) {
        sent |= usb_serial_jtag_write_bytes(s_tx_frame, total, pdMS_TO_TICKS(LINK_TX_TIMEOUT_MS)) == (int)total;
    }
    sent |= ble_link_send(s_tx_frame, total);
    xSemaphoreGive(s_tx_lock);
    return sent;
}
