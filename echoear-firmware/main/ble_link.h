/*
 * BLE byte pipe to pc-bridge: Nordic UART Service (NUS) over NimBLE, LESC passkey pairing
 * with bonding. Carries the same framed stream as USB; host_link.c does the framing.
 *
 * Data only flows on an encrypted, MITM-authenticated link: the RX characteristic requires
 * it, and nothing is notified before it.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

/* Bytes written by the host to the RX characteristic; called from the NimBLE host task. */
typedef void (*ble_link_rx_cb_t)(const uint8_t *data, size_t len);
/* Pairing started (show `passkey` for the user to type on the host) or ended (active = false). */
typedef void (*ble_link_pairing_cb_t)(bool active, uint32_t passkey);

/* Starts NimBLE; advertises only while `enabled`. Requires nvs_flash_init() (bonds live in NVS). */
esp_err_t ble_link_start(ble_link_rx_cb_t on_rx, ble_link_pairing_cb_t on_pairing, bool enabled);

/*
 * The user's Bluetooth switch. Off stops advertising and drops a connected host; bonds are
 * kept, so switching back on lets a paired host reconnect without a new passkey.
 */
void ble_link_set_enabled(bool enabled);

/* True while an authenticated host is connected and subscribed to notifications. */
bool ble_link_ready(void);

/* Notifies `data` in MTU-sized chunks; false if not ready or the stack stayed out of buffers. */
bool ble_link_send(const uint8_t *data, size_t len);
