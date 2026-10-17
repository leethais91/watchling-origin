/*
 * Framed JSON link to pc-bridge over the built-in USB-Serial-JTAG port. ESP log
 * output keeps flowing on the same port; the bridge separates it from frames.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

/* Called from the link RX task with a NUL-terminated JSON payload. */
typedef void (*host_link_rx_cb_t)(const char *json, size_t len);
/* Called from the link RX task with the payload of a FRAME_TYPE_AUDIO frame. */
typedef void (*host_link_audio_cb_t)(const uint8_t *payload, size_t len);

esp_err_t host_link_start(host_link_rx_cb_t on_message, host_link_audio_cb_t on_audio);

/* Sends one JSON frame; returns false if no host is attached or the TX buffer stayed full. Thread-safe. */
bool host_link_send_json(const char *json);
