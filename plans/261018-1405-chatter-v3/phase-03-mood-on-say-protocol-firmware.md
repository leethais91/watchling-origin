---
phase: 3
title: Mood on say (protocol + firmware)
status: completed
priority: P2
dependencies:
  - 2
---

# Phase 3: Mood on say (protocol + firmware)

## Overview
Device shows the chatter line's mood while its clip plays, then returns to the base mood. Only an
optional field pair on the existing `say` message.

## Requirements
- `say` gains optional `mood: "happy"|"surprised"|"sleep"|"idle"` and `look: "center"|"left"|"right"`.
  Missing/unknown → no face change (back-compatible both ways).
- Face changes when playback of that clip starts (after any queued chime), held for the clip
  duration, then base mood resumes.
- Not when speech is disabled (clip dropped → no face change).

## Architecture
- `main.c` `say` handler parses `mood`/`look` and passes them to `audio_speech_begin(id, bytes, face)`
  (or a small `audio_speech_set_face(id, mood, look)`), stored with the pending clip.
- `audio.c` playback task: when it starts a speech clip with a face, calls a registered callback
  `on_speech_start(mood, look, duration_ms)` (set via `audio_set_speech_callback`). Keeps `audio.c`
  free of UI includes.
- `ui.c`: callback takes the display lock and calls `robot_face_react(mood, look, duration_ms)`;
  `hold_ms` returns to base on its own — no end message needed.
- Duration = bytes / 32 (16 kHz × 2 bytes per ms).
- Simulator: no audio path; nothing to change unless it compiles `main.c` handlers — check.

## Related Code Files
- Modify: `echoear-firmware/main/main.c`, `echoear-firmware/main/audio.c`,
  `echoear-firmware/main/audio.h`, `echoear-firmware/main/ui.c`, `echoear-firmware/main/ui.h`,
  `docs/device-protocol.md`
- Bridge side already done in phase 2 (`SayMessage.mood/look`).

## Implementation Steps
1. Protocol doc row for `say` fields.
2. Map strings → `robot_mood_t`/`robot_look_t` in `main.c`.
3. Store face with the clip in `audio.c`; invoke callback at playback start.
4. Register callback in `ui.c` (lock, react with hold).
5. `idf.py build`; flash; trigger chatter with `ECHOEAR_CHATTER_DELAY_S=30`; watch face + sound;
   trigger a permission request right after to confirm the modal appears immediately.

## Success Criteria
- [ ] Face reacts with the right mood for the clip's length, then returns to idle/busy base.
- [ ] Old bridge (no fields) → speech unchanged, no face change.
- [ ] Permission modal not delayed by a playing chatter clip.

## Risk Assessment
- Callback from audio task touching LVGL → must take display lock (same pattern as other UI calls).
- Reaction overriding an attention/error reaction: chatter only plays when idle, so low risk.
