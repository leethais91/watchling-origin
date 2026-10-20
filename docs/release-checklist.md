# Firmware release checklist

Use this checklist to review a locally packaged firmware release before publishing it. Automated
checks and generated artifacts do not replace testing on an ESP-VoCat.

## Repository

- [x] Owner selects the source license (MIT); add `LICENSE` and update package metadata and documentation.
- [x] Record asset provenance and third-party terms in [asset notices](../THIRD_PARTY_NOTICES.md).
- [ ] Run `python3 scripts/check-secrets.py --history`; review any findings without posting secret values. Consider a dedicated credential scanner as well.
- [ ] Run the documented quick start from a fresh clone with Node 22 and ESP-IDF 5.5.
- [ ] Run bridge typecheck, tests and build, plus firmware build; confirm CI on GitHub passes.
- [ ] Set the GitHub description, topics and social preview; enable private vulnerability reporting in repository settings.
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

- [ ] Tag the release commit (for example `v1.2.3`) and run `./scripts/build-firmware-release.sh v1.2.3` locally with ESP-IDF 5.5; it builds that exact `PROJECT_VER` with `idf-component-manager==2.5.2` and verifies the `.zip.sha256` checksum.
- [ ] Optionally run `./scripts/build-firmware-release.sh v1.2.3 --upload-draft` to upload the verified ZIP and `.sha256` file to a GitHub draft release for review before publishing manually.
- [ ] Record the board revision, ESP-IDF version, bridge commit and agent versions tested.
- [ ] Verify the archive with `shasum -a 256 -c`, then inspect the extracted bundle and included third-party notices before publishing.
- [ ] Confirm the bundle contains every flash part and its generated offset from `flasher_args.json` as collected by the [package script](../scripts/package-firmware-release.py); do not hand-edit offsets or attach local backups or logs.
- [ ] Test the offline installer from the extracted bundle (`node serve-installer.mjs` or `start.command`). It serves on `127.0.0.1:8765`.
- [ ] Review the final repository and release contents before publishing. Do not describe the artifact as hardware-tested or published until those checks are complete.
