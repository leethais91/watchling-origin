# Device protocol (pc-bridge ⇄ EchoEar firmware)

Transport: ESP32-S3 built-in USB-Serial-JTAG (`/dev/cu.usbmodem*`, VID `303a`).
ESP log text shares the port with protocol frames.

## Framing

```
0x00 | COBS( type:u8 | payload | crc16:u16 big-endian ) | 0x00
```

- CRC-16/CCITT-FALSE (poly `0x1021`, init `0xFFFF`) over `type | payload`; check value for `"123456789"` is `0x29B1`.
- Leading `0x00` separates a frame from preceding log text. Chunks that fail COBS/CRC are treated as log text.
- Max decoded frame: 4096 bytes.
- `type 0x01` = UTF-8 JSON.
- `type 0x02` = speech PCM chunk: `id:u16 LE | offset:u32 LE | samples` (16 kHz mono s16le, ≤ 4000 sample bytes per frame).
- Other values are reserved.

Implementations: `pc-bridge/src/protocol/frame-codec.ts`, `echoear-firmware/main/frame_codec.c`.
Both are cross-checked byte-for-byte.

## Messages

Field `t` is the message type. Percentages are integers 0–100 or `null` when unknown.

### Bridge → device

| `t` | Fields | Meaning |
|---|---|---|
| `hello` | – | Port opened; device replies `hello` |
| `ping` | – | Every 2 s; device replies `pong`. Device shows "no bridge" after 5 s silence |
| `usage` | `claude:{h5,d7,costToday}`, `codex:{h5,d7}` | 5-hour / 7-day window usage; `costToday` USD (estimated, list price) |
| `time` | `epoch`, `tz` | Unix seconds (UTC) and host offset in minutes east of UTC (Asia/Saigon = `420`). Sent after `hello` and every 10 min; the device sets its clock and keeps ticking between syncs |
| `event` | `src: claude\|codex`, `kind: done\|attention\|error`, `text` | Turn finished / waiting for input / API error. Device plays a chime per kind |
| `activity` | `src: claude\|codex`, `busy: bool` | Agent started working (Claude `UserPromptSubmit`, Codex `task_started`) or was interrupted (Codex `turn_aborted`). Any `event` from that `src` also ends busy; the device drops it after 15 min without one |
| `say` | `id`, `bytes` | Start of a speech clip (`bytes` ≤ 480000 = 15 s, even); PCM follows in `0x02` frames |
| `say_end` | `id` | Clip complete; device plays it after any queued chime. Missing chunks play as silence |
| `confirm_req` | `id`, `tool`, `summary`, `risk: low\|medium\|high`, `project` | Show permission modal |
| `confirm_cancel` | `id` | Request resolved elsewhere (timeout, terminal, hook exited); hide modal if it shows `id` |

### Device → bridge

| `t` | Fields | Meaning |
|---|---|---|
| `hello` | `fw` | Firmware version (git describe) |
| `pong` | – | Liveness |
| `confirm_res` | `id`, `action: approve\|deny`, `src: touch\|voice\|tap` | User decision |

## Permission rules (enforced by the bridge)

- One request on screen at a time; others queue. Only the active `id` can be answered.
- `touch` may approve or deny. `voice` may approve only `risk: low`, deny always. `tap` may only deny.
- Unanswered after `ECHOEAR_CONFIRM_TIMEOUT_S` (default 120 s), device unplugged, or hook process
  gone → request resolves with no decision and Claude Code shows its own terminal prompt.
- Risk: Bash matching destructive patterns (`rm`, `sudo`, `git push`, `curl | sh`, …) = high; other Bash,
  file edits and MCP tools = medium; Read/Glob/Grep/WebFetch/WebSearch = low.
