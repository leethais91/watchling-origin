---
title: Public repository preparation
status: in-progress
priority: P1
effort: medium
created: 2026-10-18
---

# Public repository preparation

## Scope and acceptance

Prepare a reviewable repository for public GitHub use: English README and installation/configuration/hardware/architecture/troubleshooting/contribution guides, truthful asset provenance, portable ESP-IDF environment helper, independent Claude/Codex uninstall lifecycle, and CI. Keep ESP-VoCat hardware, Node 22, protocol and existing brand assets. No publishing, real user hook changes, invented device photos or hardware test claims. Local commits are authorized by the owner. License choice remains with the owner.

Review: scope follows the owner's accepted readiness checklist. Preserve unrelated work and existing runtime contracts; use isolated fixtures for installer checks. Validate commands/links, bridge typecheck/tests/build, firmware build if tooling is available, and secret patterns in the working tree and all reachable Git commits without printing secret values.

## Tasks

- [x] Update public user documentation and archive obsolete design specs.
- [x] Make ESP-IDF helper portable and fix shared bridge uninstall lifecycle.
- [x] Add CI, community files, release checklist and asset provenance.
- [x] Scan tracked files and reachable history for credential patterns and private paths.
- [ ] Run validation and independent code review; resolve findings.
- [x] Record the pending owner license choice; add LICENSE once selected.
- [ ] Sync final status and record verification limits.

## Dependencies

License preference; ESP-IDF/toolchain and network availability for clean dependency/build checks; physical ESP-VoCat for end-to-end verification.

## Handoff

Implementation, documentation, targeted installer tests, typecheck/build, source/history
pattern scan and independent review are complete. Full bridge test and firmware build
requests outside the sandbox await permission/completion. License remains owner-selected.
See [verification](reports/verification.md) for exact results and remaining release checks.
