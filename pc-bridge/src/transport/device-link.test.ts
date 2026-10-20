import assert from "node:assert/strict";
import { test } from "node:test";
import { STALE_LINK_MS, isLinkStale } from "./device-link.js";

test("a link is stale once the device stays silent for the whole window", () => {
  const opened = 1_000;
  assert.equal(isLinkStale(opened, 0, opened + STALE_LINK_MS - 1), false, "just opened, never answered yet");
  assert.equal(isLinkStale(opened, 0, opened + STALE_LINK_MS), true, "opened and never answered");

  const answered = opened + 5_000;
  assert.equal(isLinkStale(opened, answered, answered + STALE_LINK_MS - 1), false, "answers keep it fresh");
  assert.equal(isLinkStale(opened, answered, answered + STALE_LINK_MS), true, "silent since the last answer");
});
