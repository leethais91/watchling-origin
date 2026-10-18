# Brainstorm: review chatter spec v3 trước khi triển khai

Date: 2026-10-18 · Spec: `docs/chatter-design-v3.md` · Supersedes chatter part of `plans/261018-1103-gemini-voice-clips/plan.md`

## Problem
Spec v3 (50 câu, Gemini TTS pre-generated, ElevenLabs SFX, biểu cảm LVGL) viết độc lập với code hiện tại → nhiều chỗ lệch protocol/firmware và over-engineering.

## Codebase facts (verified)
- Chatter v1: `pc-bridge/src/chatter/chatter.ts` + `chatter-lines.ts`, 21 câu, `say -v Linh`, 5–10 min, ≤3/spell.
- Speech protocol: `say{id,bytes}` → PCM `0x02` → `say_end{id}`; 16 kHz mono s16le, ≤15 s; no cancel; device plays clip after queued chime (never overlap).
- Firmware moods: IDLE/BUSY/SLEEP/HAPPY/ATTENTION/ERROR/SURPRISED + look CENTER/LEFT/RIGHT, `robot_face_react(mood, look, hold_ms)`.
- No Gemini Live anywhere.
- `ELEVENLABS_API_KEY` set; `GEMINI_API_KEY` empty (voice gen still blocked).

## Spec defects found
| # | Issue | Fix |
|---|---|---|
| 1 | 24 kHz | 16 kHz direct from Gemini (`sample_rate:16000`), no resample |
| 2 | Gemini Live eligibility | Remove (feature doesn't exist) |
| 3 | CARE_04 == CTX_04 text | Replace CTX_04 |
| 4 | CTX_08 requires "real" cost, `costToday` is estimate | Condition = `costToday` known & ≥ $20 |
| 5 | Wink/side-look/asymmetric eyes not in firmware | Map to existing moods/looks |
| 6 | 6 new CHATTER_* events | Optional `mood`/`look` on `say` |
| 7 | Preemption/cancel | Drop; clip 2–4 s, recheck busy right before play |
| 8 | History persisted across restart | Drop, in-memory |
| 9 | 14 env vars | Keep `ECHOEAR_CHATTER`, `ECHOEAR_CHATTER_DELAY_S`; rest constants |
| 10 | 2–3 variations/line | 1/line, regenerate rejects by id |
| 11 | Heavy manifest | `{voiceId, textHash per id}` + test |

## Decisions (user)
- Scope: chatter only; mood clips (done/attention/error/tap/doze) deferred, reuse same voice ID + generator later.
- Protocol: optional `mood`, `look` fields on `say`; firmware reacts at playback start, back to base after. No new messages, no cancel.
- SFX: keep ElevenLabs (key available). 5 SFX, 2–3 takes each to choose. Runtime concat SFX PCM + 150 ms silence + line PCM, 25% of turns. Source files kept separate.
- Missing clip: line excluded from pool. No `say -v Linh` for chatter.

## Final design
- Catalog `{id, category, text, emotion, condition?}` in `chatter-lines.ts` (single source; generator imports it).
- Emotion → face: happy/proud/encouraging/playful→HAPPY; curious→IDLE+LEFT/RIGHT; confused/surprised→SURPRISED; sleepy→SLEEP.
- Selection: weights Self 60 / Dev 25 / Care 15; contextual 40% when any valid; reweight on empty category; history 8.
- Cooldowns: Care 2h (whole category); time lines 2h each; quota re-arm when h5 < 80; cost once/day.
- Timing: first 12–20 min, next 20–35 min, ≤2/spell, quiet hours 22–08, 25% silence (counts as a wait, not a line). Not-played (link down/voice off) not counted.
- Generator npm scripts: `voice:design`, `voice:audition`, `voice:generate [--ids]`, `sfx:generate`; HTML listening page (gitignored candidates); assets committed in `pc-bridge/assets/`.

## Implementation order
1. ElevenLabs SFX generation + pick.
2. Runtime: catalog, selector, cooldowns, timing, `playClip`, SFX concat + tests.
3. `say.mood/look` in protocol doc, bridge, firmware (+ simulator).
4. Gemini voice design → audition → generate 50 → listen → commit (needs key).
5. Docs: replace `docs/chatter-design.md` content with v3 behavior; README voice/SFX workflow.

## Risks
- Gemini key missing → runtime ships with zero voice clips = chatter silent until step 4. Acceptable; tests use fixture PCM.
- Southern accent/English words (Claude, Codex, quota, bug) may mispronounce → per-line regenerate.
- Repo size ~5 MB WAV committed — acceptable.

## Success criteria
- Unit tests: selection weights, history, cooldowns, quiet hours, silence, busy recheck, missing clip, manifest/hash match.
- Device: chatter shows mapped face during clip, SFX sometimes precedes, permission still appears immediately.

## Unresolved
- ElevenLabs SFX output format → convert to 16 kHz mono via ffmpeg (available).
- Valid Gemini voice-design `gender` value for female (docs only showed "male").
