# Private firmware release checklist

Use this checklist to review the private firmware draft before publishing it. CI packaging and
generated artifacts do not replace testing on an ESP-VoCat.

## Repository

- [x] Owner selects the source license (MIT); add `LICENSE` and update package metadata and documentation.
- [ ] Confirm redistribution rights for generated voice/SFX, product photo and third-party marks; record the result in [asset notices](../THIRD_PARTY_NOTICES.md).
- [ ] Run `python3 scripts/check-secrets.py --history`; review any findings without posting secret values. Consider a dedicated credential scanner as well.
- [ ] Run the documented quick start from a fresh clone with Node 22 and ESP-IDF 5.5.
- [ ] Run bridge typecheck, tests and build, plus firmware build; confirm CI on GitHub passes.
- [ ] Keep the repository private; this release flow does not use GitHub Pages or public hosting.
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

- [ ] Pick and push a semantic version tag such as `v1.2.3` to the [firmware release workflow](../.github/workflows/firmware-release.yml); it builds that exact `PROJECT_VER` with ESP-IDF 5.5 and `idf-component-manager==2.5.2`.
- [ ] Confirm the workflow created or updated a **private draft** release for owner/writer/admin review. The owner publishes it manually; the workflow never publishes it automatically.
- [ ] Record the board revision, ESP-IDF version, bridge commit and agent versions tested.
- [ ] Review the ZIP and `.sha256` attachment while authenticated, verify with `shasum -a 256 -c`, then inspect the extracted bundle and required third-party notices before publishing.
- [ ] After publication, confirm read-access collaborators can authenticate and download the private release.
- [ ] Confirm the bundle contains every flash part and its generated offset from `flasher_args.json` as collected by the [package script](../scripts/package-firmware-release.py); do not hand-edit offsets or attach local backups or logs.
- [ ] Test the offline installer from the extracted bundle. It serves on `127.0.0.1:8765`; no Pages site is involved.
- [ ] Review the final repository and release contents before publishing. Do not describe the artifact as hardware-tested or published until those checks are complete.

## Validation record

See [the preparation report](../plans/261018-public-repository/reports/verification.md) for
checks performed during repository preparation and remaining limitations.
