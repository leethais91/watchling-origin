---
title: Private firmware release and local web installer
status: in-progress
created: 2026-10-19
---

## Contract

Deliver a private release ZIP with firmware, offline browser flasher, checksums and
local launcher. Repository and assets stay private. No Pages, public hosting,
hardware flash, or visibility changes. Release version chosen by a pushed tag.

## Reviewed approach

Planner reviewed: package every generated flash part at its original offset;
validate paths, target, version and ranges. Bundle pinned ESP Web Tools locally.
Serve only on loopback with Node 22. Explicit erase choice preserves settings by
default. Tag workflow refuses public repositories and creates draft releases.

## Tasks

- [x] Implement safe firmware packaging, manifests, notices and checksums.
- [x] Implement offline installer bundle and loopback launcher.
- [x] Add private draft release workflow and regression tests.
- [x] Update installation and release documentation.
- [x] Pass automated package/server tests and static workflow checks.
- [ ] Build the ZIP from real firmware output; validate it in a browser and on hardware.

## Acceptance

All generated flash parts included without filling NVS gaps; invalid/overlapping
paths rejected; browser loads with no external requests; hashes verified before
serving; release guard fails closed. Existing bridge and firmware behavior remains
compatible. Record any unperformed hardware checks explicitly.

Verification: [source and automated checks](reports/verification.md). Actual firmware bundle,
browser install and device flash remain pending, so plan stays **in progress**.
