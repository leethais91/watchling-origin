import type { ClockMessage } from "../protocol/messages.js";

/** Wall clock for the device: UTC seconds plus the host's local offset (minutes east of UTC). */
export function clockMessage(now: Date = new Date()): ClockMessage {
  return { t: "time", epoch: Math.floor(now.getTime() / 1000), tz: -now.getTimezoneOffset() };
}
