# Release checklist

Use this checklist before changing repository visibility or publishing a release. CI builds
and generated artwork do not replace testing on an ESP-VoCat.

## Repository

- [x] Owner selects the source license (MIT); add `LICENSE` and update package metadata and documentation.
- [ ] Confirm redistribution rights for generated voice/SFX, product photo and third-party marks; record the result in [asset notices](../THIRD_PARTY_NOTICES.md).
- [ ] Run `python3 scripts/check-secrets.py --history`; review any findings without posting secret values. Consider a dedicated credential scanner as well.
- [ ] Run the documented quick start from a fresh clone with Node 22 and ESP-IDF 5.5.
- [ ] Run bridge typecheck, tests and build, plus firmware build; confirm CI on GitHub passes.
- [ ] Set the GitHub description, topics and social preview; enable private vulnerability reporting if available to this repository.
- [ ] Add a real-device photo or short demo when available. Label AI illustrations, design mockups and simulator captures accurately.

## Device acceptance

- [ ] USB: device detection, reconnect and normal usage updates.
- [ ] BLE: pairing, reconnect and USB/BLE handover; speech remains USB-only.
- [ ] Claude and Codex: working/done events, usage and permission prompts with the supported CLI versions.
- [ ] Approval, denial, timeout and disconnect fallback; check multiple queued requests.
- [ ] Brightness, volume, voice toggle and persisted Settings values after reboot.
- [ ] Install both integrations; uninstall either first, then the last; verify existing hooks/statusline are preserved.
- [ ] Silent hours, chatter preemption checks and normal notifications.

## Release artifact

- [ ] Pick and tag a version; move relevant entries out of `Unreleased` in the changelog.
- [ ] Record the board revision, ESP-IDF version, bridge commit and agent versions tested.
- [ ] If distributing firmware, build from that tag and include flash offsets/instructions,
  checksums and required third-party notices. Do not attach local backups or logs.
- [ ] Review the final repository and release contents before publishing.

## Validation record

See [the preparation report](../plans/261018-public-repository/reports/verification.md) for
checks performed during repository preparation and remaining limitations.
