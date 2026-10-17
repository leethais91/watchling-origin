import assert from "node:assert/strict";
import { test } from "node:test";
import { clockMessage } from "./clock-message.js";

test("carries UTC seconds and the offset east of UTC", () => {
  const now = new Date(Date.UTC(2026, 9, 17, 13, 7, 59, 900));
  const msg = clockMessage(now);
  assert.equal(msg.t, "time");
  assert.equal(msg.epoch, Date.UTC(2026, 9, 17, 13, 7, 59) / 1000, "fractional seconds dropped");
  // getTimezoneOffset() is minutes *behind* UTC (Asia/Saigon = -420), the device wants +420.
  assert.equal(msg.tz, -now.getTimezoneOffset());
});
