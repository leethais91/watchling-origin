# Idle chatter

When the agents are quiet, Watchling occasionally plays a short English line in a playful robot voice. Clips are bundled in `pc-bridge/assets/chatter/`; there are no runtime generation calls or API keys.

## Timing and controls

| Rule | Current behavior |
| --- | --- |
| First attempt | After 10 minutes without agent activity |
| Further attempts | Randomly every 10–20 minutes |
| Quiet-spell limit | Four played lines; activity resets the counter |
| Quiet hours | 22:00–08:00 in the bridge host's local time |
| Working agent | No chatter; a stale busy flag expires after 15 minutes |
| Link | USB only; missing connectivity or disabled Voice does not count as a played line |
| Disable | `WATCHLING_CHATTER=off`, `WATCHLING_TTS=off`, or device Voice off |
| Development delay | `WATCHLING_CHATTER_DELAY_S` sets a positive fixed delay before every attempt |

Agent activity includes turn start, permission requests and done/attention/error events. Usage and clock updates do not reset the quiet spell. A skipped attempt during quiet hours can postpone playback until the next scheduled attempt after 08:00.

`WATCHLING_TTS_VOICE` affects synthesized announcements, not these prerecorded clips. The former `WATCHLING_CHATTER_VOICE` setting is not used.

## Line selection

The 50-line catalog in `pc-bridge/src/chatter/chatter-lines.ts` covers self commentary, development jokes, gentle care and context. Only IDs with usable clips can be picked. Missing clips are excluded; there is no `say` fallback.

- The last eight played lines are avoided; history resets when the bridge restarts.
- When context fits, it has a 40% selection chance. Otherwise category weights are self 60, development 25 and care 15, adjusted for eligible lines.
- Care lines share a two-hour cooldown. Each time-of-day line also has a two-hour cooldown.
- Time windows are morning 08:00–10:00, lunch 11:00–13:00 and evening 17:00–19:00.
- Quota remarks trigger at 80% five-hour usage and rearm when usage is observed below that threshold.
- The spend remark requires at least USD 20 estimated Claude spend and plays at most once per local day.

Lines can change face mood or gaze while playing. Eligible emotions have a 25% chance to prepend their mood sound, followed by 150 ms silence. Encouraging lines have no mood sound. Short clips are not interrupted mid-playback.

## Maintaining clips

Edit the catalog, generate the corresponding WAV files, and update `pc-bridge/assets/manifest.json`. The manifest records the voice ID and text hashes. Do not reuse a line ID for different text without regenerating its clip. See [development](development.md#audio-generation) for tool locations and prerequisites.

Timing: `pc-bridge/src/chatter/chatter.ts`. Selection: `pc-bridge/src/chatter/chatter-lines.ts`. Loading and emotion mapping: `pc-bridge/src/chatter/chatter-clips.ts`. Earlier design proposals are preserved in [archive](archive/README.md); this page documents the implementation.
