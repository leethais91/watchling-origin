---
phase: 5
title: "Docs"
status: pending
priority: P3
dependencies: [2, 3, 4]
---

# Phase 5: Docs

## Overview
Make the docs describe what was built and how to regenerate audio.

## Requirements
- `docs/archive/chatter-design.md` becomes the maintained page for v3 behaviour (character, timing, catalog
  table with ids, selection, cooldowns, SFX, moods, trying it out, writing/regenerating lines).
- `docs/archive/chatter-design-v3.md`: keep as the design proposal, mark "implemented — see chatter-design.md",
  or delete if the user prefers a single page (ask).
- `docs/device-protocol.md`: `say.mood/look` (done in phase 3, verify).
- README: SFX/voice generation workflow (keys needed only for generation).
- `plans/261018-1103-gemini-voice-clips/plan.md`: status note that chatter moved to this plan.

## Related Code Files
- Modify: `docs/archive/chatter-design.md`, `docs/archive/chatter-design-v3.md`, `README.md`,
  `plans/261018-1103-gemini-voice-clips/plan.md`

## Implementation Steps
1. Rewrite `chatter-design.md` from code, not from the proposal.
2. Update README and the old plan status.
3. Verify every env var, script name and path mentioned exists.

## Success Criteria
- [ ] No doc mentions `ECHOEAR_CHATTER_VOICE`, Linh for chatter, or 24 kHz.
- [ ] Commands in docs run as written.

## Risk Assessment
Low.
