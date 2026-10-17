/*
 * Framed JSON link to pc-bridge over the built-in USB-Serial-JTAG port. ESP log
 * output keeps flowing on the same port; the bridge separates it from frames.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"

/* Called from the link RX task with a NUL-terminated JSON payload. */
typedef void (*host_link_rx_cb_t)(const char *json, size_t len);

esp_err_t host_link_start(host_link_rx_cb_t on_message);

/* Sends one JSON frame; returns false if no host is attached or the TX buffer stayed full. Thread-safe. */
bool host_link_send_json(const char *json);
