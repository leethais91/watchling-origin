---
title: 'Chatter v3: Gemini voice clips, SFX and expressions'
description: >-
  50 pre-generated Gemini chatter clips, ElevenLabs SFX, weighted/cooldown
  selection, face mood on say
status: in-progress
priority: P2
branch: main
tags:
  - chatter
  - tts
  - sfx
  - firmware
blockedBy: []
blocks: []
created: '2026-10-18T07:48:20.752Z'
createdBy: 'ck:plan'
source: skill
---

# Chatter v3: Gemini voice clips, SFX and expressions

## Overview
Replace live `say -v Linh` chatter with 50 pre-generated clips in one designed Southern female voice,
optional ElevenLabs SFX before 25% of lines, v3 selection rules, and a face mood shown while the clip
plays. Spec: `docs/archive/chatter-design-v3.md` (§0 overrides the rest). Decisions/rationale:
`plans/reports/brainstorm-261018-1405-chatter-v3-spec-review-report.md`.

## Phases

| Phase | Name | Status |
|-------|------|--------|
| 1 | [SFX generation](./phase-01-sfx-generation.md) | Completed |
| 2 | [Chatter runtime](./phase-02-chatter-runtime.md) | Completed |
| 3 | [Mood on say (protocol + firmware)](./phase-03-mood-on-say-protocol-firmware.md) | Completed |
| 4 | [Gemini voice clips](./phase-04-gemini-voice-clips.md) | Completed |
| 5 | [Docs](./phase-05-docs.md) | Pending |

Order: 1 → 2 → 3 can run now (1 needs `ELEVENLABS_API_KEY`, set). 4 needs `GEMINI_API_KEY`
(empty) and the user's voice pick. 2 depends on 1's asset layout only, not on the clips. 3 depends on 2
(`Speaker.playClip` carries the mood). 5 last.

## Asset layout (shared contract)
```
pc-bridge/assets/
  chatter/<LINE_ID>.wav   16 kHz mono s16le, ≤ 15 s
  sfx/<name>.wav          curious|playful|sleepy|surprised|happy, 16 kHz mono
  manifest.json           { voiceId, lines: { <LINE_ID>: { textHash } } }
```
Committed to git (≈5 MB). Candidate/audition audio lives in `pc-bridge/voice-candidates/` (gitignored).

## Acceptance
- `npm test` and `npm run typecheck` pass in `pc-bridge`; firmware builds (`idf.py build`) and simulator builds.
- Chatter only plays lines with a clip; never runs `say` for chatter.
- Device shows the line's mood while a chatter clip plays, then returns to base.
- After phase 4: 50 clips approved by the user by ear, manifest hashes match catalog.

## Dependencies
- Supersedes the chatter part of `plans/261018-1103-gemini-voice-clips/plan.md`; its mood-clip part
  stays there, deferred, and should reuse this plan's generator and voice ID.
