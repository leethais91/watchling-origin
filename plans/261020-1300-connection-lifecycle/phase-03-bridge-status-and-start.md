---
phase: 3
title: Bridge status and start
status: completed
priority: P2
dependencies:
  - 2
effort: 0.5 d
---

# Phase 3: Bridge status and start

## Overview
`npm run bridge:status` and `npm run bridge:start`, matching the existing `bridge:stop` script pattern.

## Requirements
- `bridge:status`: not running → `bridge is not running`, exit 0. Running → active link (`USB`/`BLE`/none), USB alive, BLE alive (or `disabled by WATCHLING_BLE=off`), device Bluetooth setting (`on`/`off`/`unknown`), firmware version, pending permission count.
- `bridge:start`: `launchctl kickstart` of `com.watchling.bridge`; if the launch agent is not installed, print the hooks install hint and exit 1; wait until the IPC socket answers (≤5 s).
- Non-functional: Node built-ins only in the CLI scripts (same as `ipc-client.ts`).

## Architecture
- `ipc-server.ts`: `GET /status` (or `POST /status` to reuse `postToBridge`) → handler supplied by `bridge.ts` returning a JSON snapshot.
- `bridge.ts`: snapshot from `transport.active`, each link's `alive`, `deviceBle`, last `fw`, `broker` pending count (add a `pendingCount` getter if missing).
- `bridge-process.ts`: export `kickstartLaunchAgent()` if not already present (hooks already start it; reuse that code path).
- New scripts `src/bridge-status.ts`, `src/start-bridge.ts`; `package.json` scripts.

## Related Code Files
- Create: `pc-bridge/src/bridge-status.ts`, `pc-bridge/src/start-bridge.ts`
- Modify: `pc-bridge/src/ipc/ipc-server.ts`, `pc-bridge/src/bridge.ts`, `pc-bridge/src/ipc/bridge-process.ts`, `pc-bridge/src/permission/permission-broker.ts`, `pc-bridge/package.json`

## Implementation Steps
1. Add status route + handler type.
2. Snapshot in `bridge.ts`; track `fw` from `hello`.
3. `bridge-status.ts` printing a short aligned table.
4. `start-bridge.ts` reusing the hook's start logic.
5. Test the status snapshot formatter (pure function) in a unit test.

## Success Criteria
- [ ] `bridge:status` correct when stopped, on USB, on BLE, and with BLE disabled.
- [ ] `bridge:start` after `bridge:stop` brings the bridge back; status shows running.
- [ ] `npm test`/`npm run build` pass.

## Risk Assessment
- Status socket open to any local user process: same trust boundary as existing IPC socket (user-owned `~/.watchling`); no secrets in the payload.
