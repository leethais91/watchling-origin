/**
 * What the bridge needs from a link to the device (USB serial or BLE), plus the framing
 * both links share: the same COBS + CRC16 stream, see docs/device-protocol.md.
 */
import type { EventEmitter } from "node:events";
import { FrameDecoder, FrameType, encodeFrame } from "../protocol/frame-codec.js";
import type { DeviceMessage, HostMessage } from "../protocol/messages.js";

/** Bridge pings every 2 s; a link counts as alive if the device answered within 5 s. */
export const PING_INTERVAL_MS = 2000;
export const ALIVE_WINDOW_MS = 5000;
/** An open link the device has not answered on for this long is reopened. */
export const STALE_LINK_MS = 8000;

/** True when the device has been silent for `STALE_LINK_MS` since the link opened or last answered. */
export function isLinkStale(openedAtMs: number, lastSeenMs: number, nowMs = Date.now()): boolean {
  return nowMs - Math.max(openedAtMs, lastSeenMs) >= STALE_LINK_MS;
}

export interface LinkEvents {
  message: [DeviceMessage];
  log: [string];
  connected: [string];
  disconnected: [];
}

export interface DeviceLink extends EventEmitter<LinkEvents> {
  /** The device answered recently on this link. */
  readonly alive: boolean;
  start(): void;
  send(msg: HostMessage): boolean;
  sendAudio(payload: Uint8Array): boolean;
  close(): void;
}

export function encodeJsonFrame(msg: HostMessage): Uint8Array {
  // NFC: the device font has precomposed letters only (Vietnamese "ế", not "e" + two marks).
  return encodeFrame(FrameType.Json, Buffer.from(JSON.stringify(msg).normalize("NFC"), "utf8"));
}

/** Turns received bytes into device messages; text between frames is ESP log output. */
export function createFrameReader(onMessage: (msg: DeviceMessage) => void, onLog: (line: string) => void) {
  const decoder = new FrameDecoder();
  return (data: Uint8Array): void => {
    for (const chunk of decoder.push(data)) {
      if (chunk.kind === "text") {
        for (const line of chunk.text.split(/\r?\n/)) if (line.trim()) onLog(line);
        continue;
      }
      if (chunk.type !== FrameType.Json) continue;
      try {
        onMessage(JSON.parse(Buffer.from(chunk.payload).toString("utf8")) as DeviceMessage);
      } catch {
        onLog("dropped malformed JSON frame");
      }
    }
  };
}
