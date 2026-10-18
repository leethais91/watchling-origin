# Hardware

Watchling targets the **ESP-VoCat V1.2**, formerly named EchoEar. The existing `echoear-firmware/` directory retains the earlier product name.

## Supported board

| Part | Firmware configuration |
| --- | --- |
| MCU/module | ESP32-S3-WROOM-1-N16R16V |
| Flash / PSRAM | 16 MB QIO flash, 16 MB octal PSRAM |
| Display / touch | 360 × 360 round display; ST77916 LCD, CST816S touch |
| Audio | ESP-VoCat BSP codecs (ES8311 / ES7210) and built-in speaker |
| Battery status | BQ27220 fuel gauge, hidden if unavailable |
| Board support | `espressif/esp_vocat`, configured in `main/idf_component.yml` |

The physical enclosure is nearly round with two small cat ears. The white robot head shown inside the dashboard is screen artwork. The [brand reference](brand/references/esp-vocat.avif) and generated banner are visual references, not evidence of hardware testing. Other revisions and generic ESP32-S3 boards are not established targets of this configuration.

## USB

Use a USB-C data cable and the ESP32-S3's built-in USB-Serial-JTAG connection. Flashing, firmware logs and bridge data share this port. Only one process should open it at a time.

On macOS, the bridge auto-selects the first device with Espressif vendor ID `303a`. Use `npm run list-ports` in `pc-bridge/` to inspect ports, and `WATCHLING_PORT` to select a particular device when several are attached. Hold BOOT while plugging in the cable if the device does not enumerate for flashing.

## Bluetooth LE

The device advertises as **Watchling** using Nordic UART Service. Enable Bluetooth on the Mac and allow Bluetooth access when macOS requests it. The bridge scans automatically unless `WATCHLING_BLE=off`.

On first connection, the screen shows a six-digit passkey. Enter it when macOS requests pairing. Firmware requires an encrypted authenticated connection and stores bonds in NVS. Subsequent connections reuse the bond. A flash erase can remove pairing state; re-pair if host and device state no longer agree.

USB is preferred while responsive; BLE carries usage, events and permission decisions when USB is unavailable. Chimes still work over BLE. Raw speech audio is **USB-only**. Wi-Fi is not implemented.

## Touch and sound

Pull down from the top edge and continue roughly halfway across the display to open Settings; an incomplete pull springs back. Adjust brightness and volume with the arcs. Releasing volume plays a sample chime. Voice controls spoken announcements and chatter, leaving chimes enabled. NVS stores these settings.

Swipe up, tap the closing hint or wait 15 seconds to close Settings. Permission decisions use the on-screen buttons; protocol fields for other decision sources do not imply an implemented voice-recognition feature.

For setup see [getting started](getting-started.md); for message details see [device protocol](device-protocol.md).
