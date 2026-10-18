import assert from "node:assert/strict";
import { test } from "node:test";
import {
  CARE_COOLDOWN_MS,
  CHATTER_LINES,
  type ChatterContext,
  type ChatterLine,
  HISTORY_SIZE,
  LineSelector,
} from "./chatter-lines.js";

const ALL = new Set(CHATTER_LINES.map((line) => line.id));
const HOUR_MS = 60 * 60 * 1000;
const at = (hour: number, minute = 0, day = 18) => new Date(2026, 9, day, hour, minute);
const ctx = (now: Date, over: Partial<ChatterContext> = {}): ChatterContext => ({
  now,
  claudeH5: null,
  codexH5: null,
  costToday: null,
  ...over,
});

/** Pushes real lines out of the history with lines that are not in the catalog. */
function flushHistory(selector: LineSelector, now: Date) {
  for (let i = 0; i < HISTORY_SIZE; i++) {
    selector.played({ id: `X${i}`, category: "self", emotion: "happy", text: "" } satisfies ChatterLine, now);
  }
}

test("catalog: 50 lines, unique ids and texts, ids match their category", () => {
  assert.equal(CHATTER_LINES.length, 50);
  assert.equal(new Set(CHATTER_LINES.map((l) => l.id)).size, 50);
  assert.equal(new Set(CHATTER_LINES.map((l) => l.text)).size, 50, "no line is said twice under two ids");
  const prefix = { self: "SELF_", dev: "DEV_", care: "CARE_", context: "CTX_" };
  for (const line of CHATTER_LINES) {
    assert.ok(line.id.startsWith(prefix[line.category]), line.id);
    assert.equal(line.category === "context", line.when !== undefined, line.id);
  }
});

test("never repeats one of the last 8 played lines", () => {
  const selector = new LineSelector(ALL);
  let seed = 7;
  const random = () => ((seed = (seed * 16807) % 2147483647) - 1) / 2147483646;
  const played: string[] = [];
  for (let i = 0; i < 200; i++) {
    const now = new Date(at(15).getTime() + i * HOUR_MS * 3);
    const line = selector.pick(ctx(now), random);
    assert.ok(line);
    assert.ok(!played.slice(-HISTORY_SIZE).includes(line.id), `${line.id} repeated`);
    selector.played(line, now);
    played.push(line.id);
  }
});

test("category weights: self, then dev, then care", () => {
  const selector = new LineSelector(ALL);
  assert.equal(selector.pick(ctx(at(15)), () => 0)?.category, "self"); // roll 0 of 100
  assert.equal(selector.pick(ctx(at(15)), () => 0.7)?.category, "dev"); // 70: past self's 60
  assert.equal(selector.pick(ctx(at(15)), () => 0.9)?.category, "care"); // 90: past dev's 85
});

test("weights are redistributed when a category has nothing left", () => {
  const selector = new LineSelector(new Set(["DEV_01", "CARE_01"]));
  assert.equal(selector.pick(ctx(at(15)), () => 0.5)?.id, "DEV_01"); // 0.5 × 40 = 20 < dev's 25
  assert.equal(selector.pick(ctx(at(15)), () => 0.7)?.id, "CARE_01");
});

test("a fitting context line wins 40% of the time", () => {
  const selector = new LineSelector(ALL);
  assert.equal(selector.pick(ctx(at(12)), () => 0.39)?.when, "lunch");
  assert.equal(selector.pick(ctx(at(12)), () => 0.41)?.category, "self");
  assert.equal(selector.pick(ctx(at(9)), () => 0)?.when, "morning");
  assert.equal(selector.pick(ctx(at(17, 30)), () => 0)?.when, "evening");
  assert.equal(selector.pick(ctx(at(15)), () => 0)?.category, "self", "no time line mid-afternoon");
});

test("a time line rests 2 hours", () => {
  const selector = new LineSelector(new Set(["CTX_03", "SELF_01"]));
  const first = selector.pick(ctx(at(11)), () => 0);
  assert.equal(first?.id, "CTX_03");
  selector.played(first!, at(11));
  flushHistory(selector, at(11));
  assert.equal(selector.pick(ctx(at(12, 59)), () => 0)?.id, "SELF_01");
  assert.equal(selector.pick(ctx(at(11, 0, 19)), () => 0)?.id, "CTX_03", "fine again the next day");
});

test("care lines share a 2-hour cooldown", () => {
  const selector = new LineSelector(new Set(["CARE_01", "CARE_02"]));
  const first = selector.pick(ctx(at(14)), () => 0)!;
  selector.played(first, at(14));
  assert.equal(selector.pick(ctx(new Date(at(14).getTime() + CARE_COOLDOWN_MS - 1)), () => 0), null);
  assert.equal(selector.pick(ctx(new Date(at(14).getTime() + CARE_COOLDOWN_MS)), () => 0)?.id, "CARE_02");
});

test("quota line: once per crossing, needs real data", () => {
  const selector = new LineSelector(new Set(["CTX_06", "SELF_01"]));
  assert.equal(selector.pick(ctx(at(15)), () => 0)?.id, "SELF_01", "unknown usage");
  const high = ctx(at(15), { claudeH5: 92 });
  const line = selector.pick(high, () => 0)!;
  assert.equal(line.id, "CTX_06");
  selector.played(line, at(15));
  flushHistory(selector, at(15));
  assert.equal(selector.pick(high, () => 0)?.id, "SELF_01", "still above 80: said already");
  selector.pick(ctx(at(16), { claudeH5: 3 }), () => 0); // window reset seen
  assert.equal(selector.pick(ctx(at(18), { claudeH5: 85 }), () => 0)?.id, "CTX_06");
});

test("Codex quota is tracked separately", () => {
  const selector = new LineSelector(new Set(["CTX_06", "CTX_07"]));
  assert.equal(selector.pick(ctx(at(15), { codexH5: 80 }), () => 0)?.id, "CTX_07");
});

test("spend line: at most once a day", () => {
  const selector = new LineSelector(new Set(["CTX_08", "SELF_01"]));
  assert.equal(selector.pick(ctx(at(15), { costToday: 19.9 }), () => 0)?.id, "SELF_01");
  const line = selector.pick(ctx(at(15), { costToday: 25 }), () => 0)!;
  assert.equal(line.id, "CTX_08");
  selector.played(line, at(15));
  flushHistory(selector, at(15));
  assert.equal(selector.pick(ctx(at(20), { costToday: 40 }), () => 0)?.id, "SELF_01");
  assert.equal(selector.pick(ctx(at(9, 0, 19), { costToday: 21 }), () => 0)?.id, "CTX_08", "new day");
});

test("nothing eligible: null", () => {
  assert.equal(new LineSelector(new Set()).pick(ctx(at(15)), () => 0), null);
});
