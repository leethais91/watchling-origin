# Idle chatter design

> Historical design proposal. Some details differ from the shipped code. See [the current chatter guide](../chatter.md).

When nobody needs it, the robot says something silly out loud in Vietnamese. This page covers
the character, when it speaks, the line catalog, how a line is picked, and how to add lines.

Code: `pc-bridge/src/chatter/chatter.ts` (timing) and `pc-bridge/src/chatter/chatter-lines.ts`
(lines and selection). Tests: `pc-bridge/src/chatter/chatter.test.ts`.

## Character

A small desk robot that keeps an eye on Claude and Codex for you and gets bored when there is
nothing to watch. It is friendly and a bit cheeky, never rude and never preachy.

- **Voice:** casual Southern Vietnamese. It calls itself *tui* and calls the user *bạn*, and
  uses particles like *nha*, *nè*, *đó*, *ta*, *hết trơn*.
- **Topics:** what it can see (the time, quota, today's spend, being idle) and small care
  (water, stretching, sleep, committing your work). It never comments on what you are working on.
- **Tone:** teasing, not nagging. A reminder comes as a joke ("Đói bụng chưa? Tui thì chạy bằng
  điện."), not an order.

## When it speaks

| Rule | Value | Why |
|---|---|---|
| Quiet time before a line | random 5–10 min with no agent activity | Rare enough to stay a surprise; random so it doesn't feel like a timer |
| Lines per quiet spell | at most 3 | So it doesn't talk to an empty room all night |
| While an agent works | silent | Speech would talk over permission prompts and "finished" announcements |
| Busy flag with no end | expires after 15 min | Claude Code fires no hook when a turn is interrupted (same rule as the device) |
| Link | USB only | Speech clips are raw PCM; over BLE the device only chimes |

Agent activity means: a prompt starting a turn, a permission request, a done/attention/error
event, or Codex starting or stopping. Any of them starts a new quiet spell (count back to 0).
Usage and clock updates don't count.

Chatter is off when `WATCHLING_CHATTER=off` or `WATCHLING_TTS=off`. Each line is logged as
`chatter: ...` in `~/.watchling/bridge.log`, with `(not spoken: device not on USB)` when it couldn't
be played.

## Line catalog

### Anytime (always in the pool)

| Line | Meaning |
|---|---|
| Rảnh quá, có ai giao việc cho tui không? | So bored, anyone got a job for me? |
| Tui đang canh Claude giùm bạn nè. | I'm keeping an eye on Claude for you. |
| Bíp bíp. Hệ thống vẫn ổn định. | Beep beep. All systems stable. |
| Uống miếng nước đi bạn ơi. | Have a sip of water. |
| Đứng dậy vươn vai chút đi. | Stand up and stretch a bit. |
| Hôm nay code vui không? | Having fun coding today? |
| Tui là robot, nhưng tui cũng biết chán đó nha. | I'm a robot, but I get bored too, you know. |
| Không ai chơi với tui hết trơn. | Nobody plays with me at all. |
| Commit chưa đó? Đừng để mất code nha. | Committed yet? Don't lose your code. |

### Time of day (local hour; windows don't overlap)

| Window | Lines | Meaning |
|---|---|---|
| 23:00–03:59 | Khuya rồi, đi ngủ đi bạn ơi. / Giờ này còn thức là cú đêm rồi đó. / Mai code tiếp cũng được mà. | It's late, go to bed. / Up at this hour, you're a night owl. / You can code tomorrow. |
| 06:00–09:59 | Chào buổi sáng! Cà phê chưa? / Sáng nay làm gì đây ta? | Good morning! Coffee yet? / What are we doing this morning? |
| 11:00–12:59 | Tới giờ ăn trưa rồi đó. / Đói bụng chưa? Tui thì chạy bằng điện. | Lunchtime. / Hungry? I run on electricity. |
| 17:00–18:59 | Chiều rồi, sắp tan làm chưa? | Evening already, done for the day soon? |

Other hours add no time line.

### Usage and spend (from the latest usage snapshot)

| Condition | Line | Meaning |
|---|---|---|
| Claude 5-hour usage ≥ 80% | Claude xài gần hết quota rồi đó nha. | Claude has almost used up its quota. |
| Codex 5-hour usage ≥ 80% | Codex cũng sắp hết quota rồi. | Codex is almost out of quota too. |
| Today's Claude cost ≥ $20 | Hôm nay đốt *N* đô rồi đó. | You've burned *N* dollars today. (*N* rounded) |

## How a line is picked

1. Collect the context lines that fit right now (time window, usage, spend).
2. Pool = all anytime lines + the context lines **twice**, so a fitting line is about twice as
   likely as a generic one.
3. Drop the line said last, so it never repeats back to back.
4. Pick one uniformly at random.

Example: at noon with normal usage the pool has 9 + 2×2 = 13 entries, so a lunch line comes up
about 31% of the time. Late at night with Claude above 80%: 9 + 2×4 = 17 entries, a context line
about 47% of the time.

## Writing new lines

- **Short:** one breath, about 12 words and 3 seconds of speech. Clips over 15 s are cut off.
- **Plain text only:** no emoji, markdown or symbols; `say` reads them out or skips them. Write
  numbers so they read naturally ("20 đô", not "$20").
- **In character:** *tui*/*bạn*, playful, no guilt-tripping, nothing about the user's code or
  private data.
- **Listen before committing:** `say -v Linh "your line"`. Pay attention to English words (code,
  commit, quota, Claude, Codex): respell them or swap in Vietnamese if Linh mangles them.
- **Where:** generic lines go in `ANYTIME`. For a new condition, add a branch to `contextLines()`
  so it is weighted. Add a test in `chatter.test.ts` for any new condition.

## Trying it out

```sh
launchctl setenv WATCHLING_CHATTER_DELAY_S 30        # wait 30 s instead of 5–10 min
launchctl kickstart -k gui/$(id -u)/com.watchling.bridge
tail -f ~/.watchling/bridge.log | grep chatter
# afterwards
launchctl unsetenv WATCHLING_CHATTER_DELAY_S
launchctl kickstart -k gui/$(id -u)/com.watchling.bridge
```

Pick another installed voice with `WATCHLING_CHATTER_VOICE` (`say -v '?'` lists them). The lines
are Vietnamese, so a non-Vietnamese voice will mispronounce them.

## Ideas not built

- Lines written by an LLM from the current context (costs tokens and needs an API key).
- Quiet hours (the 3-line cap covers most of the need for now).
- Reacting to the robot's own state, such as being woken by a tap.
