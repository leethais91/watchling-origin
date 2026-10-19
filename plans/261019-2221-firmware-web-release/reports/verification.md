# Verification — private firmware release

Date: 2026-10-19
Status: source/docs/automated checks complete; release-candidate validation pending

## Verified

- Repository visibility returned `PRIVATE`.
- Offline ESP Web Tools 10.4.0 installer build passed.
- 13 focused tests passed: 7 Python packager tests and 6 Node loopback-server tests.
- `actionlint` 1.7.12 passed for the private tag/draft-release workflow.
- Python/Node syntax checks and scoped diff checks passed.
- Independent review completed with no blockers.
- Repository secret scan completed with zero findings.
- Source implements all generated flash parts/offsets, SHA-256 verification, notices,
  offline assets, Node 22 loopback serving, private-visibility guard, and draft release creation.

## Pending

- Build and inspect an actual versioned ZIP from real ESP-IDF `build/` metadata.
- Open the packaged installer in a supported browser and exercise Web Serial.
- Flash an ESP-VoCat through browser and CLI paths; verify boot version, USB/BLE behavior,
  settings persistence, erase behavior, and packaged model/storage assets.
- Build/output reads are still blocked by the privacy hook until user approval is received.

No commit, push, tag, release, deployment, or publication was created. Existing unrelated
firmware edits were preserved.

## Unresolved questions

- None beyond approval/access needed for the pending build and hardware validation.
