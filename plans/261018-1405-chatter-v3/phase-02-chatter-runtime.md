---
phase: 2
title: Chatter runtime
status: completed
priority: P1
dependencies:
  - 1
---

# Phase 2: Chatter runtime

## Overview
Rewrite the chatter catalog, selection and timing per spec v3 and play pre-generated clips (with
optional SFX) through the existing `Speaker` queue. No `say` for chatter.

## Requirements
- Catalog: 50 lines from spec §6 (with the replaced CTX_04), `{ id, category, text, emotion, when? }`.
  `category: self|dev|care|context`; `emotion` one of the 8 in §4.
- Timing constants (§5.1): first line after 12–20 min quiet, next after 20–35 min, ≤ 2 lines per quiet
  spell, quiet hours 22:00–08:00 local, 25% of due turns stay silent (counts as a wait, not a line).
- Eligibility at play time (not only at timer): no agent busy (existing 15-min expiry), link alive
  and voice on (`speechLink.alive`), not quiet hours, spell limit not reached. Pending permission
  already marks the agent busy (`onAgent(src, true)` on confirm) — keep that.
- Selection (§7): context lines that fit → 40% contextual, else regular; regular weights self 60 /
  dev 25 / care 15, renormalized over categories with candidates. Exclude last 8 played ids, lines
  without a clip, and cooldowns. No candidate → skip turn.
- Cooldowns: care category 2 h; each time line 2 h; quota line armed again only after its h5 drops
  below 80 (or becomes null after a reset); cost line once per local day, needs `costToday ≥ 20`.
- A line counts (history, spell count, cooldown) only when its clip was actually sent.
- SFX: 25% of played lines, mapped by emotion (§4: happy/proud→happy, playful→playful,
  curious/confused→curious, sleepy→sleepy, surprised→surprised, encouraging→none), concatenated
  as `sfx + 150 ms silence + line` into one clip.
- Mood per emotion (§12.1) passed with the clip (used in phase 3; ignored by old firmware).
- Missing asset dir or file → line excluded, logged once at start (`chatter: N/50 clips`).
- Env: keep `ECHOEAR_CHATTER`, `ECHOEAR_CHATTER_DELAY_S`; drop `ECHOEAR_CHATTER_VOICE`.

## Architecture
- `chatter-lines.ts`: `CHATTER_LINES` array + pure `pickLine(ctx, state, random)` returning a line
  or null. `ctx = { now: Date, claudeH5, codexH5, costToday, available: Set<id> }`; `state` holds
  history, cooldown timestamps, quota armed flags, cost day. Pure → easy tests.
- `chatter.ts`: scheduler (spell count, quiet hours, silence roll, first/next delay) and state
  updates after a successful play. `say` option replaced by `play(line) => boolean` (sent or not).
- `chatter-clips.ts` (new): loads `assets/chatter/*.wav` + `assets/sfx/*.wav` at start into PCM
  buffers (decode via `src/tts/wav.ts`), builds the clip (sfx concat) for a line. ~50 × 100 KB in
  memory is fine.
- `Speaker.playClip(pcm, face?)`: same queue/id path as `say`, factor the send loop into one
  private `send(pcm, face?)` so `say` and `playClip` share it. `face = { mood, look }`.
- `bridge.ts`: wire `Chatter` with `play` → `speaker.playClip`, log `chatter: <id> <text>` or
  `(not played: …)`.

## Related Code Files
- Modify: `pc-bridge/src/chatter/chatter-lines.ts`, `pc-bridge/src/chatter/chatter.ts`,
  `pc-bridge/src/chatter/chatter.test.ts`, `pc-bridge/src/speech/speaker.ts`,
  `pc-bridge/src/speech/speaker.test.ts`, `pc-bridge/src/bridge.ts`,
  `pc-bridge/src/protocol/messages.ts` (optional `mood`, `look` on `SayMessage`)
- Create: `pc-bridge/src/chatter/chatter-clips.ts` (+ test)

## Implementation Steps
1. Catalog + `pickLine` with tests (weights with seeded random, history 8, cooldowns, quota re-arm,
   cost per day, unavailable clips excluded, empty → null, duplicate text check).
2. Scheduler rewrite with fake timers/clock: quiet hours, first vs next delay, ≤ 2 per spell,
   silence roll, busy recheck at fire time, not-played not counted, activity resets spell.
3. `chatter-clips.ts`: load + concat; tests with tiny fixture PCM.
4. `Speaker.playClip` + `SayMessage` fields; speaker test asserts `say` carries mood.
5. Bridge wiring + log lines; remove `chatterVoice`.
6. `npm test`, `npm run typecheck`; run with `ECHOEAR_CHATTER_DELAY_S=30` and a couple of
   placeholder clips (SFX only, or temp Linh-rendered files not committed) to verify end to end.

## Success Criteria
- [ ] All new rules covered by unit tests; suite + typecheck green.
- [ ] Bridge log shows picked id, mood, sfx yes/no; nothing played in quiet hours or while busy.
- [ ] With zero chatter clips present the bridge runs and stays silent (`chatter: 0/50 clips`).

## Risk Assessment
- Behaviour change: chatter is far rarer (≈16 min first line, max 2) — intended by spec.
- Clips missing until phase 4 → chatter silent in production meanwhile; acceptable, logged.
