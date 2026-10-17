/*
 * Framed JSON link to pc-bridge over the built-in USB-Serial-JTAG port and BLE (ble_link.c).
 * Both carry the same COBS + CRC16 stream (docs/device-protocol.md). ESP log output keeps
 * flowing on the USB port; the bridge separates it from frames.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

typedef enum {
    HOST_LINK_USB,
    HOST_LINK_BLE,
    HOST_LINK_COUNT,
} host_link_t;

/* Called from a link RX task with a NUL-terminated JSON payload. */
typedef void (*host_link_rx_cb_t)(const char *json, size_t len);
/* Called from a link RX task with the payload of a FRAME_TYPE_AUDIO frame. */
typedef void (*host_link_audio_cb_t)(const uint8_t *payload, size_t len);

/* Starts the USB side; the BLE side starts with ble_link_start(host_link_ble_rx, ...). */
esp_err_t host_link_start(host_link_rx_cb_t on_message, host_link_audio_cb_t on_audio);

/* Feeds bytes received over BLE into that link's frame assembler. */
void host_link_ble_rx(const uint8_t *data, size_t len);

/* True if the bridge sent anything on `link` within the last few seconds (it pings every 2 s). */
bool host_link_alive(host_link_t link);

/*
 * Sends one JSON frame on every connected link; the bridge listens to one of them. Returns
 * false if no link took it. Thread-safe.
 */
bool host_link_send_json(const char *json);
