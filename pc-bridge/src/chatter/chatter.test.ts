import assert from "node:assert/strict";
import { mock, test } from "node:test";
import type { UsageMessage } from "../protocol/messages.js";
import { BUSY_TIMEOUT_MS, Chatter, FIRST_QUIET_MS, MAX_LINES_PER_SPELL, NEXT_QUIET_MS } from "./chatter.js";
import { CHATTER_LINES, type ChatterLine } from "./chatter-lines.js";

const usage: UsageMessage = { t: "usage", claude: { h5: 10, d7: null, costToday: null }, codex: { h5: null, d7: null } };
const ALL = new Set(CHATTER_LINES.map((line) => line.id));
/** With random() = 0.5 every wait is the middle of its range. */
const FIRST_MS = (FIRST_QUIET_MS.min + FIRST_QUIET_MS.max) / 2;
const NEXT_MS = (NEXT_QUIET_MS.min + NEXT_QUIET_MS.max) / 2;

function setup(
  opts: { at?: Date; random?: () => number; play?: (line: ChatterLine) => boolean; available?: Set<string> } = {},
) {
  mock.timers.enable({ apis: ["setTimeout", "Date"], now: opts.at ?? new Date(2026, 9, 18, 15, 0) });
  const played: string[] = [];
  const chatter = new Chatter({
    play: (line) => (opts.play ?? (() => true))(line) && played.push(line.id) > 0,
    usage: () => usage,
    available: opts.available ?? ALL,
    random: opts.random ?? (() => 0.5),
  });
  return { chatter, played };
}

const STEP_MS = 60 * 1000;
/**
 * Mock timers skip timeouts scheduled during a tick, so long waits advance in one-minute steps;
 * a wait scheduled mid-step fires at the end of a later step, up to a minute late.
 */
function wait(ms: number) {
  for (let left = ms; left > 0; left -= STEP_MS) mock.timers.tick(Math.min(left, STEP_MS));
}

test("first line after the first wait, one more after each next wait, then quiet for the spell", (t) => {
  t.after(() => mock.timers.reset());
  const { chatter, played } = setup();
  chatter.start();
  mock.timers.tick(FIRST_MS - 1);
  assert.equal(played.length, 0);
  mock.timers.tick(1);
  assert.equal(played.length, 1);
  for (let n = 2; n <= MAX_LINES_PER_SPELL; n++) {
    mock.timers.tick(NEXT_MS);
    assert.equal(played.length, n);
  }
  wait(NEXT_QUIET_MS.max * 10);
  assert.equal(played.length, MAX_LINES_PER_SPELL);
  assert.equal(new Set(played).size, played.length, "no line repeats within a spell");
  chatter.stop();
});

test("nothing during quiet hours", (t) => {
  t.after(() => mock.timers.reset());
  const { chatter, played } = setup({ at: new Date(2026, 9, 18, 21, 50) });
  chatter.start();
  wait(9 * 60 * 60 * 1000); // until 06:50
  assert.equal(played.length, 0);
  chatter.stop();
});

test("agent activity starts a new spell and keeps the robot quiet while busy", (t) => {
  t.after(() => mock.timers.reset());
  let r = 0.5;
  const { chatter, played } = setup({ random: () => r });
  chatter.start();
  wait(NEXT_QUIET_MS.max * 10);
  assert.equal(played.length, MAX_LINES_PER_SPELL);

  r = 0; // shortest waits, so turns come due while Claude is still busy
  chatter.onAgent("claude", true);
  wait(BUSY_TIMEOUT_MS - STEP_MS);
  assert.equal(played.length, MAX_LINES_PER_SPELL, "silent while Claude works");

  r = 0.5;
  chatter.onAgent("claude", false);
  mock.timers.tick(FIRST_MS);
  assert.equal(played.length, MAX_LINES_PER_SPELL + 1);
  chatter.stop();
});

test("a busy flag with no end runs out", (t) => {
  t.after(() => mock.timers.reset());
  const { chatter, played } = setup();
  chatter.onAgent("codex", true);
  wait(BUSY_TIMEOUT_MS);
  assert.equal(played.length, 0);
  // Turns that come due while busy wait the first interval again, until one lands after the timeout.
  const firstFreeTurnMs = (Math.floor(BUSY_TIMEOUT_MS / FIRST_MS) + 1) * FIRST_MS;
  wait(firstFreeTurnMs - BUSY_TIMEOUT_MS + STEP_MS);
  assert.equal(played.length, 1, "first turn after the flag ran out plays");
  chatter.stop();
});

test("a line that could not be played doesn't count", (t) => {
  t.after(() => mock.timers.reset());
  let deviceOn = false;
  const attempts: string[] = [];
  const { chatter, played } = setup({ play: (line) => (attempts.push(line.id), deviceOn) });
  chatter.start();
  wait(FIRST_MS + (NEXT_MS + STEP_MS) * 3);
  assert.equal(attempts.length, 4, "keeps trying while the device is away");
  assert.equal(played.length, 0);
  deviceOn = true;
  wait((NEXT_MS + STEP_MS) * MAX_LINES_PER_SPELL);
  assert.equal(played.length, MAX_LINES_PER_SPELL);
  chatter.stop();
});

test("only lines with a clip are picked", (t) => {
  t.after(() => mock.timers.reset());
  const { chatter, played } = setup({ available: new Set(["DEV_03"]) });
  chatter.start();
  wait(FIRST_MS + NEXT_MS);
  assert.deepEqual(played, ["DEV_03"], "the only line is in the history, so the second turn is skipped");
  chatter.stop();
});

test("a custom quiet time replaces the default waits", (t) => {
  t.after(() => mock.timers.reset());
  mock.timers.enable({ apis: ["setTimeout", "Date"], now: new Date(2026, 9, 18, 15, 0) });
  const played: string[] = [];
  const chatter = new Chatter({
    play: (line) => played.push(line.id) > 0,
    usage: () => usage,
    available: ALL,
    random: () => 0.5,
    quietMs: { min: 30_000, max: 30_000 },
  });
  chatter.start();
  mock.timers.tick(30_000);
  assert.equal(played.length, 1);
  chatter.stop();
});
