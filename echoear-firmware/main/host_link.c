#include "host_link.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "driver/usb_serial_jtag.h"
#include "driver/usb_serial_jtag_vfs.h"
#include "esp_check.h"
#include "esp_log.h"
#include "frame_codec.h"

static const char *TAG = "host_link";

/* Matches MAX_FRAME_BYTES in pc-bridge/src/protocol/frame-codec.ts. */
#define FRAME_MAX_BYTES     4096
#define LINK_TX_BUFFER      8192
#define LINK_RX_BUFFER      4096
#define LINK_TX_TIMEOUT_MS  100
#define RX_TASK_STACK       6144

static host_link_rx_cb_t s_on_message;
static SemaphoreHandle_t s_tx_lock;

/* RX frame assembly (RX task only). */
static uint8_t s_rx_raw[FRAME_COBS_MAX_ENCODED(FRAME_MAX_BYTES)];
static uint8_t s_rx_body[FRAME_COBS_MAX_ENCODED(FRAME_MAX_BYTES) + 1];

/* TX scratch (guarded by s_tx_lock). */
static uint8_t s_tx_body[FRAME_MAX_BYTES];
static uint8_t s_tx_frame[FRAME_COBS_MAX_ENCODED(FRAME_MAX_BYTES) + 2];

static void handle_raw_frame(size_t raw_len)
{
    size_t body_len = frame_cobs_decode(s_rx_raw, raw_len, s_rx_body);
    if (body_len < 3) {
        return;
    }
    uint16_t crc = (uint16_t)(s_rx_body[body_len - 2] << 8) | s_rx_body[body_len - 1];
    if (frame_crc16(s_rx_body, body_len - 2) != crc) {
        ESP_LOGW(TAG, "Dropped frame with bad CRC");
        return;
    }
    if (s_rx_body[0] != FRAME_TYPE_JSON) {
        return;
    }
    size_t json_len = body_len - 3;
    char *json = (char *)&s_rx_body[1];
    json[json_len] = '\0'; /* overwrites the CRC, already checked */
    s_on_message(json, json_len);
}

static void rx_task(void *arg)
{
    uint8_t chunk[256];
    size_t raw_len = 0;
    bool overflow = false;

    for (;;) {
        int n = usb_serial_jtag_read_bytes(chunk, sizeof(chunk), portMAX_DELAY);
        for (int i = 0; i < n; i++) {
            if (chunk[i] == 0) {
                if (raw_len > 0 && !overflow) {
                    handle_raw_frame(raw_len);
                }
                raw_len = 0;
                overflow = false;
            } else if (raw_len < sizeof(s_rx_raw)) {
                s_rx_raw[raw_len++] = chunk[i];
            } else {
                overflow = true; /* drop until the next delimiter */
            }
        }
    }
}

esp_err_t host_link_start(host_link_rx_cb_t on_message)
{
    s_on_message = on_message;
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

    if (xTaskCreate(rx_task, "host_link_rx", RX_TASK_STACK, NULL, 6, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

bool host_link_send_json(const char *json)
{
    size_t json_len = strlen(json);
    if (json_len + 3 > FRAME_MAX_BYTES || !usb_serial_jtag_is_connected()) {
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
    int written = usb_serial_jtag_write_bytes(s_tx_frame, total, pdMS_TO_TICKS(LINK_TX_TIMEOUT_MS));
    xSemaphoreGive(s_tx_lock);

    return written == (int)total;
}
