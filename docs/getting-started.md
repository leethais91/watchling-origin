# Getting started

Build the firmware for ESP-VoCat V1.2, connect it to a Mac, and enable the agent integrations you need. Commands start in the repository root unless a step changes directory.

## Prerequisites

- [ESP-VoCat V1.2](hardware.md) and a USB-C cable that carries data.
- macOS, Node.js 22 or newer, npm, and Apple's Command Line Tools (`xcode-select --install`, needed when npm compiles native serial/BLE modules). The bridge is not supported on Linux or Windows.
- Claude Code terminal sessions or Codex CLI. Codex permission integration requires a version with `PermissionRequest` hooks (the project targets 0.162+); the integration depends on the agent's hook and session formats.
- **Only if building firmware from source (Option B):**
  - An installed ESP-IDF **5.5** environment with the ESP32-S3 toolchain. Follow [Espressif's macOS setup instructions](https://docs.espressif.com/projects/esp-idf/en/v5.5/esp32s3/get-started/linux-macos-setup.html), including `./install.sh esp32s3`; the helper below loads an existing installation. Keep the SDK and project paths free of spaces, as required by the ESP-IDF build system.
  - [uv](https://docs.astral.sh/uv/) with its managed Python 3.12: `uv python install 3.12`. ESP-IDF supports Python 3.9–3.13, and newer Homebrew Pythons break its setup, so `scripts/idf-env.sh` always runs ESP-IDF on that uv-managed interpreter and never on a system Python. Set `WATCHLING_IDF_PYTHON` to pick another supported version. ESP-IDF keeps one Python environment per version: create it with the same interpreter by running `install.sh esp32s3` (or `python3 "$IDF_PATH/tools/idf_tools.py" install-python-env`) in a shell that sourced the helper.

No audio service API keys are needed for setup or everyday use.

## 1. Install firmware (Option A: Prebuilt release bundle)

If you have a prebuilt firmware release archive (`watchling-<version>-esp-vocat-v1.2.zip` and its matching `.zip.sha256` file from GitHub Releases or `./scripts/build-firmware-release.sh`), verify the archive before extracting it:

```sh
shasum -a 256 -c watchling-v1.2.3-esp-vocat-v1.2.zip.sha256
unzip watchling-v1.2.3-esp-vocat-v1.2.zip
cd watchling-v1.2.3-esp-vocat-v1.2
```

The extracted installer is self-contained. It needs Node.js 22 or newer, but no network connection or ESP-IDF installation. Start it with `node serve-installer.mjs`, run `sh start.command`, or double-click `start.command` in Finder. If macOS refuses to open the launcher, run `chmod +x start.command` first. The launcher serves only `http://127.0.0.1:8765`; open that address in desktop Chrome or Edge. The macOS launcher may open Chrome automatically.

Before selecting the device, stop the bridge and any serial monitor from `pc-bridge/`:

```sh
npm run bridge:stop
```

Use **Connect & install**, select the ESP-VoCat and confirm. Leave **Erase device** unchecked to preserve Settings and Bluetooth bonds. Erasing removes device data, including those settings and bonds. The installer flashes firmware only; the Mac bridge remains a separate installation. When finished, press Ctrl-C in the installer terminal and proceed to [2. Build the bridge](#2-build-the-bridge).

## 1b. Build and flash firmware from source (Option B: ESP-IDF)

Skip this section if you already flashed a prebuilt release bundle in step 1.

Set `IDF_PATH` if ESP-IDF is outside `~/esp/esp-idf`; otherwise the helper uses that directory. Run in a shell where your ESP-IDF installation is supported:

```sh
export IDF_PATH="$HOME/esp/esp-idf"  # replace if you installed the SDK elsewhere
. scripts/idf-env.sh
python -m pip install 'idf-component-manager==2.5.2'
cd echoear-firmware
idf.py build
```

Pinning `idf-component-manager==2.5.2` avoids an ESP-IDF 5.5 Kconfig dependency error (`MissingKconfigError: LV_USE_FREETYPE`) on clean builds. The checked-in defaults select ESP32-S3 and the ESP-VoCat memory/display configuration. The build resolves component dependencies and compiles the bundled firmware audio and images.

To find your serial port (`*` marks Espressif devices), or after building the bridge in step 2, flash with the exact `/dev/cu.usbmodem…` port:

```sh
idf.py -p /dev/cu.usbmodem123456 flash
```

Replace the example port with your own. If no port appears, hold **BOOT** while connecting USB-C. Flashing replaces the installed firmware. Settings and BLE bonds use NVS; an ordinary flash is different from a full flash erase.

For firmware logs, `idf.py -p /dev/cu.usbmodem123456 monitor` opens the serial monitor. Exit it with Ctrl-] before starting the bridge: both use the same port.
Return to the repository root with `cd ..`.

## 2. Build the bridge

From the repository root:

```sh
cd pc-bridge
nvm use
npm ci
npm run build
npm run list-ports
```

If you do not use nvm, activate Node 22 or newer with your preferred version manager and skip `nvm use`. Port listing marks Espressif devices with `*`. Then return to the root with `cd ..`.

## 3. Try a foreground connection

Return to the repository root, then:

```sh
cd pc-bridge
WATCHLING_BLE=off npm start
```

The log should report a USB connection and device readiness; the display receives time and usage state. Unknown usage remains unknown until the corresponding agent supplies it. Ctrl-C stops this foreground process. See [Bluetooth setup](hardware.md#bluetooth-le) after USB works.

## Agent integrations

Run the commands below from `pc-bridge/` after building. They are opt-in and change files in your home directory.

| Command | Changes |
| --- | --- |
| `npm run hooks:install` | Adds Claude hooks in `~/.claude/settings.json`; wraps `statusLine` to forward usage and run your original command |
| `npm run hooks:install:codex` | Adds session-start and permission hooks in `~/.codex/hooks.json`; usage and busy/done tracking already come from session files |
| Either installer | Registers `~/Library/LaunchAgents/com.watchling.bridge.plist`, pinned to the current Node executable and repository path |

Either agent's configuration file is created if absent; existing files must contain valid JSON. Existing files are backed up beside themselves as `.watchling-backup-<timestamp>` on install and uninstall. Hooks belonging to other tools are retained. The original Claude statusline command is stored in `~/.watchling/config.json`.

Install one or both:

```sh
npm run hooks:install
npm run hooks:install:codex
```

Start a new agent session afterward. Codex may ask you to trust the new hooks. A session-start hook starts the bridge through launchd; it remains running after the session ends. It does not start automatically at login or restart immediately after `bridge:stop`. Logs go to `~/.watchling/bridge.log`.

Rebuild after updating source. Re-run the relevant installer after changing the Node installation. Before moving the repository, uninstall both installed integrations from the old location: hooks contain absolute paths, and the installer only recognizes paths belonging to its current checkout. After moving, rebuild and reinstall the integrations. If the old location is already gone, repair stale hook and statusline entries manually before reinstalling; see [recovery steps](troubleshooting.md#recovering-agent-settings).

## Permissions and fallback

The touchscreen shows one request at a time. Touch Approve or Deny returns that decision to the asking agent. The terminal resumes its normal permission flow if the bridge/device is unavailable, the connection is lost or the request expires (120 seconds by default). Claude tools such as `AskUserQuestion` and `ExitPlanMode` stay in the terminal.

Permission summaries can be shortened and the risk label is a heuristic. Use the terminal to inspect a request when more context is needed. See [configuration and privacy](configuration.md) for the information sent to the device.

## Stop, uninstall and restore

```sh
npm run bridge:stop
npm run hooks:uninstall          # remove Claude integration
npm run hooks:uninstall:codex    # remove Codex integration
```

Uninstall removes the selected integration. The shared launch agent is retained while the other integration remains installed, and removed when neither needs it. Claude uninstall restores the stored original statusline command if Watchling's wrapper is still active. Uninstall retains local state and backups for recovery.

For manual recovery, compare the printed backup with the current agent settings, then restore the needed entries. Replacing the whole file would also replace unrelated settings changed since that backup. See [troubleshooting](troubleshooting.md) before deleting local state.
