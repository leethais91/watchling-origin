import assert from "node:assert/strict";
import { mock, test } from "node:test";
import type { UsageMessage } from "../protocol/messages.js";
import { BUSY_TIMEOUT_MS, Chatter, MAX_LINES_PER_SPELL, MAX_QUIET_MS, MIN_QUIET_MS } from "./chatter.js";
import { chatterLine } from "./chatter-lines.js";

const usage: UsageMessage = { t: "usage", claude: { h5: 10, d7: null, costToday: null }, codex: { h5: null, d7: null } };

function setup() {
  mock.timers.enable({ apis: ["setTimeout", "Date"], now: new Date(2026, 9, 18, 15, 0) });
  const said: string[] = [];
  const chatter = new Chatter({ say: (text) => said.push(text), usage: () => usage, random: () => 0 });
  return { chatter, said };
}

/** Mock timers skip timeouts scheduled during a tick, so long waits advance in steps. */
function wait(ms: number) {
  for (let left = ms; left > 0; left -= MIN_QUIET_MS) mock.timers.tick(Math.min(left, MIN_QUIET_MS));
}

test("speaks after the quiet delay, at most a few times per spell", (t) => {
  t.after(() => mock.timers.reset());
  const { chatter, said } = setup();
  chatter.start();
  mock.timers.tick(MIN_QUIET_MS - 1);
  assert.equal(said.length, 0);
  mock.timers.tick(1);
  assert.equal(said.length, 1);
  wait(MAX_QUIET_MS * 10);
  assert.equal(said.length, MAX_LINES_PER_SPELL);
  assert.notEqual(said[0], said[1], "never the same line twice in a row");
});

test("agent activity starts a new spell and keeps the robot quiet while busy", (t) => {
  t.after(() => mock.timers.reset());
  const { chatter, said } = setup();
  chatter.start();
  wait(MAX_QUIET_MS * 10);
  assert.equal(said.length, MAX_LINES_PER_SPELL);

  chatter.onAgent("claude", true);
  wait(MIN_QUIET_MS * 2);
  assert.equal(said.length, MAX_LINES_PER_SPELL, "silent while Claude works");

  chatter.onAgent("claude", false);
  mock.timers.tick(MIN_QUIET_MS);
  assert.equal(said.length, MAX_LINES_PER_SPELL + 1);
  chatter.stop();
});

test("a busy flag with no end runs out", (t) => {
  t.after(() => mock.timers.reset());
  const { chatter, said } = setup();
  chatter.onAgent("codex", true);
  wait(BUSY_TIMEOUT_MS + MIN_QUIET_MS);
  assert.equal(said.length, 1);
  chatter.stop();
});

test("lines fit the hour and the usage", () => {
  const base = { claudeH5: null, codexH5: null, costToday: null };
  const last = () => 0.999;
  assert.equal(chatterLine({ ...base, hour: 0 }, last), "Mai code tiếp cũng được mà.");
  assert.equal(chatterLine({ ...base, hour: 15, claudeH5: 95 }, last), "Claude xài gần hết quota rồi đó nha.");
  assert.equal(chatterLine({ ...base, hour: 15, costToday: 42.4 }, last), "Hôm nay đốt 42 đô rồi đó.");
  const quiet = chatterLine({ ...base, hour: 15 }, last);
  assert.equal(quiet, "Commit chưa đó? Đừng để mất code nha.");
  assert.notEqual(chatterLine({ ...base, hour: 15 }, last, quiet), quiet);
});
