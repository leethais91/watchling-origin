---
phase: 1
title: SFX generation
status: completed
priority: P2
dependencies: []
---

# Phase 1: SFX generation

## Overview
Generate the 5 chatter sound effects with ElevenLabs Sound Effects, let the user pick one take per
effect, store them in the device format.

## Requirements
- 5 effects from spec §10.2 prompts: curious, playful, sleepy, surprised, happy; 0.3–0.8 s.
- 3 takes each; user picks by ear on a local HTML page.
- Output 16 kHz mono s16le WAV, trimmed silence, peak-normalized to sit below the speech level.
- Key from `ELEVENLABS_API_KEY` env only; never logged or committed.

## Architecture
**As built:** extended the existing `echoear-firmware/tools/sound_effects.py` (style `chatter`, `CHATTER_EFFECTS` levels at 0.30–0.35 peak, `bridge` command → `pc-bridge/assets/sfx/`) instead of a new TS generator: same API path (`pcm_16000` verified), same trim/level code. User picked variant 1 of each (2026-10-18).

Original design:
`pc-bridge/src/voice/generate-sfx.ts` (tsx, run by hand):
- `POST https://api.elevenlabs.io/v1/sound-generation?output_format=pcm_16000`, header `xi-api-key`,
  body `{ text, duration_seconds, prompt_influence }`. Verify `pcm_16000` is accepted on the first
  call; if not, request mp3 and convert with `ffmpeg -ar 16000 -ac 1 -f s16le`.
- `sfx:generate` writes `voice-candidates/sfx/<name>-<n>.wav` + `index.html` (audio tags).
- `sfx:pick <name> <n>` trims leading/trailing silence (ffmpeg `silenceremove`) and copies to
  `assets/sfx/<name>.wav`.
- Prompts live in `src/voice/sfx-catalog.ts` (name → prompt, duration).
- WAV write via existing `src/tts/wav.ts` if it has an encoder; otherwise add a small `encodeWav`.

## Related Code Files
- Create: `pc-bridge/src/voice/sfx-catalog.ts`, `pc-bridge/src/voice/generate-sfx.ts`,
  `pc-bridge/assets/sfx/*.wav`
- Modify: `pc-bridge/package.json` (scripts `sfx:generate`, `sfx:pick`), `.gitignore`
  (`pc-bridge/voice-candidates/`), possibly `pc-bridge/src/tts/wav.ts`

## Implementation Steps
1. Catalog with the 5 prompts from spec §10.2.
2. Generator: sequential requests, retry on 429 with backoff, fail loudly on 401/4xx with body.
3. Build candidate page; open it for the user.
4. User picks; run `sfx:pick` per effect; listen once more on device speaker (phase 3 test).

## Success Criteria
- [x] 5 files in `assets/sfx/`, 16 kHz mono, each ≤ 1 s, no leading silence > 30 ms.
- [x] User approved each by ear.
- [x] No key in repo or logs.

## Risk Assessment
- Output format param differs → ffmpeg fallback.
- SFX too loud vs speech → normalize peak to about −6 dBFS, adjust after device listen.
