---
title: Fix firmware CI Kconfig dependency resolution
date: 2026-10-19
summary: Pin IDF Component Manager 2.5.2 for clean ESP-IDF 5.5 firmware builds.
---

# Fix firmware CI Kconfig dependency resolution

## Cause and change
Firmware job 118110314365 failed with MissingKconfigError: LV_USE_FREETYPE before compilation. The bundled Component Manager evaluated LVGL optional dependencies before Kconfig existed when reconciling the lockfile's ESP-IDF 5.5.6 with CI 5.5.0. Pin manager 2.5.2 in .github/workflows/ci.yml; document recovery in docs/troubleshooting.md.

## Verification
Isolated manager 2.2.0 reproduces the missing-symbol exception; 2.5.2 returns False for the same optional dependency. A clean git archive firmware build using ESP-IDF v5.5, Python 3.12 and manager 2.5.2 passed all 1816 steps. Workflow YAML parses; action supports command input; git diff --check passes. Hardware was not flashed. Docs impact: minor.

## Remaining
Changes local; GitHub CI has not run on this patch. AgentWiki publish skipped.

> Historical work record — not durable authority. Prefer docs/specs/ADRs for current decisions.
