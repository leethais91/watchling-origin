---
date: 2026-10-18
status: awaiting-owner-decisions-and-build-permission
---

# Public repository preparation — verification

## Scope

Prepared the repository for review before public GitHub publication. No pushes,
visibility changes, firmware flashing, real hook installation or changes to the owner's
live agent settings were performed.

## Implemented

- English README plus setup, hardware, configuration, troubleshooting, architecture,
  current chatter and development guides; historical chatter specs moved to `docs/archive/`.
- Preserved the ESP-VoCat brand kit and added host simulator dashboard/settings captures.
- Portable ESP-IDF helper; no machine-specific Homebrew/Python path is imposed on new users.
- Shared launch-agent lifecycle for Claude/Codex uninstall, with isolated regression fixtures.
- Unrelated command/prompt/agent hooks and existing statusline are preserved; ambiguous script
  paths belonging to other repositories are not treated as Watchling.
- CI, contribution/issue/PR templates, security/data handling guidance, changelog, release
  checklist, asset provenance and LobeHub icon notice.

## Repository scan

Targeted scan after implementation: **197 working-tree files and 390 reachable Git history
blobs, 0 credential-pattern findings**. Report-only additions do not change source credentials.
Synthetic checks detected six representative credential patterns and ignored normal code /
explicit example placeholders without printing values.

This scanner covers selected private-key, provider-token, credential-URL and literal-assignment
patterns. It does not cover ignored files, unreachable Git objects, all possible credential
formats, dependency vulnerabilities or a full security audit.

The filename/history check did not find `.env`, PEM/key or credential-named files in reachable
commits. Two current generated font comments contained the owner's old absolute output path;
these are now relative. Old absolute paths remain in historical font blobs. Git history was
not rewritten; these are non-secret build paths, but the owner may prefer a separate history
cleanup before publication.

## Checks

- Shell syntax: `bash -n scripts/idf-env.sh` and `zsh -n scripts/idf-env.sh` passed.
- Installer tests: final targeted run passed 14/14 under Node 22.22.0 (temporary profiles and
  fake launchctl, no real user configuration).
- Bridge typecheck/build passed under Node 22.22.0. The full suite hit the sandbox's Unix
  socket restriction; a final run on the current source was requested outside the sandbox
  and is awaiting permission/completion. No full-suite passing total is claimed.
- Firmware configure hit the sandbox's `psutil/sysctl` restriction. An outside-sandbox build
  was requested; no successful firmware compilation is claimed yet.
- ESP-IDF helper fixtures: bash/zsh custom SDK, default SDK and missing SDK behavior passed.
- Simulator: captured `docs/images/dashboard.png` and `settings.png`; images visually inspected.
- Independent review found a medium ownership issue in migration matching; the speculative
  matching was removed and regression coverage added. Re-review passed with no unresolved
  high/medium findings; the reviewer independently ran all 14 installer tests successfully.
- Internal documentation links: 75 local targets across 23 Markdown documents checked after
  report creation, with no missing targets.
- CI YAML parsed successfully; `git diff --check` passed. GitHub execution has not been run.

See [test results](test-results.md) and [independent review](review.md) for detailed evidence.

## Remaining owner decisions and external checks

- Source license choice is pending; do not describe the repository as licensed open source
  until the owner chooses a license and the license file is added.
- Confirm redistribution permission/source for the supplied product photo and generated
  voice/SFX under the owner's provider accounts; notices make no unsupported rights claims.
- A clean-clone build on a new machine and an actual GitHub CI run are not yet established.
- Physical USB/BLE pairing, approvals, Settings persistence and agent interoperability need
  the device acceptance checks in [the release checklist](../../../docs/release-checklist.md).
- A real-device demo/photo and a public release remain owner publishing steps.

## Next action

Owner: choose the source license and allow the final bridge tests / firmware build requested
outside the sandbox. Resume the two pending verification commands, record their actual
results, then complete physical acceptance before publishing.
