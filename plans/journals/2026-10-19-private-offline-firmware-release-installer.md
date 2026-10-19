---
title: Private offline firmware release installer
date: 2026-10-19
summary: Add private draft-release workflow and an offline localhost browser installer.
---

# Private offline firmware release installer

## Delivered source
Added a tag-triggered workflow that verifies private repository visibility twice, builds versioned ESP32-S3 firmware, and creates private draft releases. Packaging uses generated flash_files offsets, validates paths/chip/version/ranges, preserves sparse gaps, and includes checksums and notices. ESP Web Tools 10.4.0 is bundled offline. Node 22 serves only loopback and verifies file hashes before serving. Installation and release docs updated.

## Verification
13 focused tests passed (7 Python, 6 Node). Installer bundle builds; npm audit reports zero vulnerabilities. actionlint 1.7.12, syntax and diff checks pass. Secret pattern scan: zero findings. Independent code review found no blockers.

## Pending
Hook denies direct access to build/ and dist/; explicit user approval requested, still pending. Real firmware archive inspection, browser end-to-end and physical flash not performed. No commit, push, tag or GitHub release created. Repository remains private. Existing unrelated firmware edits from another session preserved. AgentWiki publish skipped.

> Historical work record — not durable authority. Prefer docs/specs/ADRs for current decisions.
