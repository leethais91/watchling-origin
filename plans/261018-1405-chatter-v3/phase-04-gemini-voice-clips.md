---
phase: 4
title: Gemini voice clips
status: in-progress
priority: P2
dependencies:
  - 2
---

# Phase 4: Gemini voice clips

## Overview
Design the robot voice with Gemini 3.8 Flash TTS, let the user pick it by ear, generate the 50 lines,
approve them by ear, commit them with a manifest.

**As built:** Southern, then Northern em/anh Vietnamese were tried; the user chose English with the
`english-robot` voice (`voice_0tetlqv7unme`). 39/50 clips done (CARE_02–05 added 2026-10-19, awaiting approval by ear); the Tier 1 limit is 100
TTS requests per day, so the remaining 11 (CARE_06–08, CTX_01–08) are generated on 2026-10-20. Key comes from zshrc
(`zsh -ic`).

## Requirements
- API details from `plans/261018-1103-gemini-voice-clips/plan.md` §Findings (interactions endpoint,
  `speech_metadata.style`, voice design `POST /v1beta/voices`, `sample_rate:16000` WAV/L16).
- Voice identity prompt: spec §3.3 condensed to 1–2 sentences (Google guidance); `language_code: vi-VN`;
  confirm valid `gender` value for female on first call.
- Per-line style from the emotion table §8.3, plus the custom styles for SELF_03, SELF_22, CTX_06.
- One take per line; regenerate selected ids on request.
- Manifest `{ voiceId, lines: { id: { textHash } } }` written by the generator.

## Architecture
`pc-bridge/src/voice/generate-voice.ts` (tsx), reads `CHATTER_LINES` from `chatter-lines.ts`:
- `voice:design` → 3 candidates (variations of the §3.3 prompt), saves IDs + sample audio to
  `voice-candidates/voices.json`.
- `voice:audition --voices a,b,c` → same 6 lines (one per main emotion + an English-word line) per
  voice into `voice-candidates/<voice>/` + `index.html`.
- `voice:generate --voice <id> [--ids SELF_03,DEV_07]` → `assets/chatter/<id>.wav`, sequential,
  retry on 429, trims edge silence, updates manifest; `chatter-index.html` lists every clip with text.
- Test: every `CHATTER_LINES` id has a wav and `sha256(text)` matches manifest (skipped with a clear
  message when the manifest doesn't exist yet so phase 2 CI stays green).

## Related Code Files
- Create: `pc-bridge/src/voice/generate-voice.ts`, `pc-bridge/src/voice/voice-assets.test.ts`,
  `pc-bridge/assets/chatter/*.wav`, `pc-bridge/assets/manifest.json`
- Modify: `pc-bridge/package.json` (scripts), `pc-bridge/src/voice/` shared HTTP/WAV helpers with
  phase 1 if duplication appears

## Implementation Steps
1. Get key from user (`launchctl setenv` or shell export).
2. Design + audition; user picks a voice. Record voice ID in manifest.
3. Generate all 50; user listens on the index page; regenerate rejects (watch Claude, Codex, quota,
   code, bug, deadline).
4. Commit clips + manifest; run test.
5. On device: listen to a sample of lines through the speaker; adjust loudness normalization if needed.

## Success Criteria
- [ ] User approved the voice and all 50 clips.
- [ ] Every clip ≤ 15 s, 16 kHz mono; manifest test green.
- [ ] Bridge logs `chatter: 50/50 clips`.

## Risk Assessment
- Accent/English word pronunciation → per-line regenerate, optionally tweak style text; don't respell
  automatically (spec §14.2).
- Voice TTL 1 year → regenerating new lines later needs a new design if expired; manifest keeps the ID.
