# Animated robot face — Plan

Status: implemented; on device: blink verified 2026-10-17, busy/happy/attention/error not yet seen live · Created: 2026-10-17

## Decisions (user, 2026-10-17)
- Approach A: robot head is one image with an empty visor; eyes are drawn by LVGL so they
  can blink, glance and change expression.
- New robot art, head only (no body), generated with Codex `image_generation`.
- Robot gets bigger; usage percentage labels shrink to make room.
- Add a "working" state: needs a new protocol message and bridge hooks.

## Moods
| Mood | Trigger | Eyes |
|---|---|---|
| idle | base, link up, no agent busy | blink at random 2–6 s, sometimes a double blink, occasional glance |
| busy | base, any agent working | narrower eyes sweeping left↔right |
| sleep | base, no bridge link | flat lines, dim |
| happy | `event done`, 4 s | `^ ^` arcs, green, look toward source side |
| attention | `event attention`, until next change (max 2 min) | bigger, amber, pulse |
| error | `event error`, 6 s | `× ×`, red, short shake |
Head bobs ±2 px (slow sine) in every mood except sleep. Claude = left side, Codex = right side.

## Protocol
- New bridge → device message `activity`: `src: claude|codex`, `busy: bool`.
- Device clears `busy` for a source on any `event` from it; busy also expires after 15 min
  (Claude Code fires no hook when a turn is interrupted).

## Bridge
- Claude: `UserPromptSubmit` hook → `activity busy:true`. `Stop`/`StopFailure` already send events.
- Codex: rollout `task_started` → busy true, `turn_aborted` → busy false; `task_complete` → done event (exists).
- IPC route `POST /activity`.

## Firmware
- New `main/robot_face.c/.h` (pure LVGL, used by the dashboard and the simulator): head image,
  eye objects, mood state machine. API: `robot_face_create`, `robot_face_set_base`,
  `robot_face_react`.
- `agent_dashboard.c`: replace avatar + lids with robot_face; new layout (bigger head,
  smaller percent font, cost moved down).
- `ui.c`: track busy per source + link, compute the base mood, map events to reactions.
- `main.c`: parse `activity`.
- Simulator: `--mood <name>` for screenshots; the live window cycles through moods.

## Assets
- `robot-head-prompt.md` (this plan dir) keeps the Codex prompt; `robot-head-source.png` is the chosen 1024 px candidate. The source PNG lives in `main/assets/`,
  converted with LVGLImage.py to RGB565A8.

## Validation
- `npm test` + typecheck in `pc-bridge`.
- `idf.py build`; simulator screenshots for every mood plus `--stress`.
- On device: user checks the animation (not verifiable here).

## Risks
- AI art may not leave a clean empty visor → retry prompt / clean up with ImageMagick.
- Full head redraw at bob frames: only ~5 distinct y positions, cheap. Eye anims redraw small areas.
