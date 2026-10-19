# Third-party and generated assets

This file records provenance. It does not grant rights to third-party trademarks, product
photography or provider-generated media beyond the applicable permissions. Watchling's own source
code is covered by the [MIT License](LICENSE); the items below keep their own terms.

| Files / component | Source | Notice / status |
| --- | --- | --- |
| `echoear-firmware/main/assets/font_dash_bold_*.c` | Montserrat converted for LVGL | SIL OFL 1.1; [copyright and full notice](echoear-firmware/main/assets/Montserrat-OFL.txt) |
| `echoear-firmware/main/assets/font_mono_15.c` | JetBrains Mono Medium converted for LVGL | SIL OFL 1.1; [copyright and full notice](echoear-firmware/main/assets/JetBrainsMono-OFL.txt) |
| `logo_claude.png`, `logo_codex.png`, corresponding `img_logo_*.c` | Claude/OpenAI SVG marks from `@lobehub/icons-static-svg` 1.95.1, tinted and rasterized; conversion recorded in `dashboard_assets.h` | LobeHub MIT notice below. Brand marks identify integrations; no affiliation or endorsement is implied. |
| `echoear-firmware/main/assets/robot_head.png`, `img_robot_head.c` | Codex image generation, converted for LVGL | Project-generated artwork; [generation prompt](plans/261017-2138-animated-robot-face/robot-head-prompt.md) |
| `echoear-firmware/main/assets/sounds/*.wav`, `sound_effects.c`, `pc-bridge/assets/sfx/*.wav` | ElevenLabs Sound Effects API | Project-generated audio. Owner must confirm the generating account's redistribution permissions before publication. |
| `pc-bridge/assets/chatter/*.wav` | Gemini TTS | Project-generated English voice clips. Model/voice identity and text hashes are recorded in [manifest](pc-bridge/assets/manifest.json). Owner must confirm applicable redistribution permissions before publication. |
| `docs/brand/assets/*.png` | Built-in image generation using the supplied ESP-VoCat photo and project UI mockups | AI illustrations; [prompts and identity guide](docs/brand/README.md). Not photographs of firmware running on a real device. |
| `docs/brand/references/esp-vocat.avif` | ESP-VoCat product photo supplied by the repository owner | Original photographer/source and redistribution permission have not yet been recorded. Owner confirmation required before publishing this photo. |
| `docs/images/dashboard.png`, `docs/images/settings.png` | Host SDL/LVGL simulator rendering of the firmware UI | Actual simulator captures using synthetic sample values, not device photographs or live agent data. Included fonts and marks retain the notices above. |
| `plans/261018-1240-settings-screen/mockups/*.png` | Project UI design mockups | Design references, not screenshots of shipped firmware. The pull mockup contains unrelated energy placeholders that are not Watchling features. |
| ESP-IDF, ESP-VoCat BSP, LVGL and managed components | Dependencies resolved from `idf_component.yml` / `dependencies.lock` | Retain upstream license files when redistributing built firmware/dependencies; see each upstream component. |
| npm dependencies | `pc-bridge/package-lock.json` | Dependency licenses remain with their upstream packages. `node_modules/` is not committed. |

Inter and JetBrains Mono are named as recommended brand fonts. No Inter font file is bundled
by the brand kit. Included firmware font conversions are covered by their OFL notices above.

## LobeHub icon source — MIT License

Source: [LobeHub Icons license](https://github.com/lobehub/lobe-icons/blob/master/LICENSE).

Copyright (c) 2023 LobeHub

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
