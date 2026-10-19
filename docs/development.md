# Development

The bridge is TypeScript on Node 22 or newer; firmware is ESP-IDF C with LVGL. Follow [getting started](getting-started.md) for the initial toolchain and device setup. See [CONTRIBUTING.md](../CONTRIBUTING.md) before sending changes.

## Bridge checks

From `pc-bridge/` with Node 22 or newer active:

```sh
npm ci
npm run typecheck
npm test
npm run build
```

`npm run dev` starts the TypeScript bridge directly; `npm start` runs `dist/bridge.js`. Installed agent hooks always point to built JavaScript, so rebuild after editing source. Keep `pc-bridge/assets/` next to the built bridge: chatter assets are loaded relative to the package directory.

The GitHub [CI workflow](../.github/workflows/ci.yml) runs these bridge checks on macOS with
Node 22, builds firmware for ESP32-S3 in ESP-IDF 5.5, and checks credential patterns across
reachable Git history. It never flashes hardware or installs hooks in a real agent profile.
The firmware job follows [Espressif's CI action](https://github.com/espressif/esp-idf-ci-action);
Node setup and npm caching use [GitHub's setup-node action](https://github.com/actions/setup-node).

## Private firmware release packaging

The [tag workflow](../.github/workflows/firmware-release.yml) accepts semantic version tags such
as `v1.2.3`, requires the GitHub repository to remain private, builds with ESP-IDF 5.5 and
`idf-component-manager==2.5.2`, and creates or updates a private draft release. The owner reviews
and publishes the draft manually. It does not publish automatically or use GitHub Pages. Packaging
is artifact preparation; it does not prove USB, Bluetooth, audio or touchscreen behavior on
hardware.

To reproduce a release bundle locally, use Node 22 or newer and a sourced ESP-IDF environment:

```sh
VERSION=v1.2.3
COMMIT=$(git rev-parse HEAD)
. scripts/idf-env.sh
python3 -m pip install idf-component-manager==2.5.2
npm --prefix web-installer ci
npm --prefix web-installer test
npm --prefix web-installer run build
cd echoear-firmware
idf.py -DPROJECT_VER="$VERSION" build
python3 ../scripts/package-firmware-release.py \
  --build-dir build \
  --web-dir ../web-installer/dist \
  --output-dir ../release-output \
  --source-root .. \
  --idf-path "$IDF_PATH" \
  --version "$VERSION" \
  --commit "$COMMIT"
```

The [package script](../scripts/package-firmware-release.py) reads ESP-IDF's generated
`build/flasher_args.json` and copies every flash part at its generated offset. Do not supply
hand-written offsets. It writes the ZIP and matching `.zip.sha256` file under `release-output/`;
verify the ZIP before sharing it with an authenticated repository user.

From the root, run the local repository check with:

```sh
python3 scripts/check-secrets.py --history
```

It prints paths and rule names, never credential values. It scans tracked and non-ignored
untracked text files plus all reachable history blobs. Ignored files, unreachable objects
and credential formats outside its rules are not covered. A clean result is not a full
security audit. Real credentials found in history need revocation and a separate decision
about history cleanup before sharing the repository.

## Firmware and UI simulator

From the root, source `scripts/idf-env.sh`; then build in `echoear-firmware/` with `idf.py build`. Stop the bridge and exit serial monitors before flashing.

The SDL simulator compiles dashboard and robot UI code on the host against the firmware's managed LVGL component. Run `idf.py reconfigure` in the firmware directory if `managed_components/` has not been created. It needs CMake, pkg-config, SDL2 and ImageMagick. On a Mac using Homebrew:

```sh
brew install cmake pkg-config sdl2 imagemagick
cd echoear-firmware/simulator
./render-screenshot.sh /tmp/watchling-dashboard.png
./render-screenshot.sh /tmp/watchling-settings.png --settings
./render-screenshot.sh /tmp/watchling-face.png --face --mood happy
./cmake-out/dashboard_sim --ui
```

The screenshot helper builds the simulator and applies the round screen mask. Supported options include `--stress`, `--unknown`, `--confirm`, `--pull N`, `--gravity LAT,DOWN` (mg, for the rolling eyes) and `--after-ms N`; moods include `idle`, `busy`, `sleep`, `happy`, `attention`, `error`, `surprised`, `lean`, `dizzy`, `stargaze`. Simulator output is a host rendering, not a photograph or a hardware integration test.

## Audio generation

Everyday users should use the checked-in clips. These tools send requests to external services, need credentials and can incur provider charges. Keep credentials out of tracked files and logs. Provider availability and quotas can change; the generator source is the authoritative API implementation.

| Tool | Inputs and output |
| --- | --- |
| `pc-bridge/src/voice/generate-voice.ts` | `GEMINI_API_KEY`; generates chatter WAV files and manifest, using ffmpeg for conversion |
| `echoear-firmware/tools/sound_effects.py` | `ELEVENLABS_API_KEY`; generates sound candidates and embeds selected firmware sounds, or generates bridge chatter SFX |

Inspect each tool's command handling before regenerating assets. The bridge npm tasks `voice:design`, `voice:audition` and `voice:generate` invoke the voice generator; generation requires an actual provider voice ID. Candidate listening pages are local development output. Keep voice ID, model, text hashes and bundled clips in sync when accepting new audio.

Firmware sounds live in `echoear-firmware/main/assets/sounds/`; bridge mood sounds live in `pc-bridge/assets/sfx/`. They serve different playback paths. See [chatter](chatter.md), [device protocol](device-protocol.md) and [asset notices](../THIRD_PARTY_NOTICES.md).
