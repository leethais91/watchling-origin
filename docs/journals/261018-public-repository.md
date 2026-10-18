# Public repository preparation

**Date**: 2026-10-18
**Severity**: Medium
**Component**: Installation, documentation, release preparation
**Status**: Ongoing

## What Happened

Public release preparation added documentation, CI, a portable ESP-IDF helper, asset provenance, a history pattern scan, and a fix for uninstalling a shared launch agent.

## Release readiness

Release preparation includes reproducible installation, accurate documentation, verified build results and recorded asset permissions. The readiness checklist tracks these separately so a new contributor can see the supported setup and remaining checks.

## Technical Details

The installer checks passed: 14 tests. Full-suite and firmware validation results were still pending when this entry was written. Passing installer checks must not be presented as proof that firmware builds or hardware behavior works.

## What We Tried

The proposed cleanup recognized stale script paths. Review identified an ownership collision: that matching could claim an installation belonging to another checkout. The speculative matching was removed.

## Root Cause Analysis

Path similarity was being treated as ownership evidence. The decision is to recognize the exact checkout and document uninstalling before moving it. Broader cleanup was rejected because uncertain ownership makes removal unsafe.

## Lessons Learned

Installation cleanup needs explicit ownership. Convenience heuristics become destructive when multiple checkouts share service names.

## Next Steps

Before publication, the maintainer must choose a license and review validation results, asset provenance, and history scan findings. No pushes or publication have been performed as part of this preparation. Local commits are authorized by the owner.
