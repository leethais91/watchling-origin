import assert from "node:assert/strict";
import { mkdtemp, readFile, rm, writeFile } from "node:fs/promises";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { after, test } from "node:test";
import { UsageStore } from "./usage-store.js";

const dir = await mkdtemp(join(tmpdir(), "echoear-usage-"));
after(() => rm(dir, { recursive: true, force: true }));

const inAnHour = Math.floor(Date.now() / 1000) + 3600;

test("a restarted bridge restores Claude usage and today's spend", async () => {
  const statePath = join(dir, "restart.json");
  const before = new UsageStore(statePath);
  before.updateFromStatusline({
    session_id: "a",
    cost: { total_cost_usd: 1.25 },
    rate_limits: { five_hour: { used_percentage: 41.6, resets_at: inAnHour }, seven_day: { used_percentage: 12 } },
  });
  before.updateFromStatusline({ session_id: "b", cost: { total_cost_usd: 0.5 } });
  await before.save();

  const after = new UsageStore(statePath);
  await after.restore();
  assert.deepEqual(after.snapshot().claude, { h5: 42, d7: 12, costToday: 1.75 });
});

test("spend saved on another day is dropped, usage windows are kept", async () => {
  const statePath = join(dir, "yesterday.json");
  await writeFile(
    statePath,
    JSON.stringify({
      claude: { h5: { pct: 30, resetsAt: inAnHour }, d7: null },
      costDay: "2000-01-01",
      sessionCosts: { old: 9.99 },
    }),
  );
  const store = new UsageStore(statePath);
  await store.restore();
  assert.deepEqual(store.snapshot().claude, { h5: 30, d7: null, costToday: null });
});

test("a missing or corrupt file leaves the store empty", async () => {
  const corrupt = join(dir, "corrupt.json");
  await writeFile(corrupt, "{not json");
  for (const statePath of [join(dir, "missing.json"), corrupt]) {
    const store = new UsageStore(statePath);
    await store.restore();
    assert.deepEqual(store.snapshot().claude, { h5: null, d7: null, costToday: null });
  }
});

test("statusline updates are saved after a short delay", async () => {
  const statePath = join(dir, "delayed.json");
  const store = new UsageStore(statePath);
  store.updateFromStatusline({ rate_limits: { five_hour: { used_percentage: 7 } } });
  await new Promise((resolve) => setTimeout(resolve, 1200));
  const saved = JSON.parse(await readFile(statePath, "utf8"));
  assert.equal(saved.claude.h5.pct, 7);
});
