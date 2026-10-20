---
phase: 5
title: "Docs and release notes"
status: pending
priority: P3
dependencies: [1, 2, 3, 4]
effort: "0.5 d"
---

# Phase 5: Docs and release notes

## Overview
Update user docs for opt-in Bluetooth, new commands, grace behavior and battery screen-off.

## Requirements
- README: "Connects over USB, or Bluetooth LE once enabled in Settings"; daily use mentions `bridge:status`/`bridge:start`.
- `docs/hardware.md`: Bluetooth section → enable in Settings, default off, standby while USB plugged; touch section adds the switch and screen-off.
- `docs/troubleshooting.md`: BLE not connecting → check Settings switch first; status command; silent USB port recovery.
- `docs/configuration.md`: `WATCHLING_BLE=off` still disables bridge scanning; new timeouts (8 s stale port, 10 s grace) if listed there.
- `docs/device-protocol.md`: `ble` field on `hello`/`settings`; permission release after 10 s link loss.
- `docs/architecture.md`: transport + permission paragraph.
- `CHANGELOG.md`: behavior change — Bluetooth now off by default.
- Simulator screenshot `docs/images/settings.png` regenerated.

## Related Code Files
- Modify: `README.md`, `CHANGELOG.md`, `docs/{hardware,troubleshooting,configuration,device-protocol,architecture}.md`, `docs/images/settings.png`

## Implementation Steps
1. Read each doc, update only affected sections.
2. Regenerate settings screenshot with `simulator/render-screenshot.sh`.
3. Check links and claims match shipped behavior.

## Success Criteria
- [ ] No doc still says BLE is automatic/always on.
- [ ] Commands in docs run as written.

## Risk Assessment
Low.
