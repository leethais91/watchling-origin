import assert from "node:assert/strict";
import { test } from "node:test";
import { FrameDecoder, FrameType, cobsDecode, cobsEncode, crc16, encodeFrame } from "./frame-codec.js";

const bytes = (...values: number[]) => Uint8Array.from(values);

test("crc16 matches the CCITT-FALSE check value", () => {
  assert.equal(crc16(new TextEncoder().encode("123456789")), 0x29b1);
});

test("cobs round-trips zeros, empty input and 254+ byte runs", () => {
  const cases = [
    bytes(),
    bytes(0),
    bytes(0, 0),
    bytes(1, 0, 2, 0),
    Uint8Array.from({ length: 254 }, (_, i) => i + 1), // exactly one full non-zero block
    Uint8Array.from({ length: 300 }, () => 0xaa),
    Uint8Array.from({ length: 600 }, (_, i) => i % 7),
  ];
  for (const input of cases) {
    const encoded = cobsEncode(input);
    assert.ok(!encoded.includes(0), "encoded data must not contain 0x00");
    assert.deepEqual(cobsDecode(encoded), input);
  }
});

test("decoder separates frames from interleaved log text", () => {
  const json = new TextEncoder().encode('{"t":"pong"}');
  const frame = encodeFrame(FrameType.Json, json);
  const log = new TextEncoder().encode("I (123) bringup: hello\n");
  const stream = Buffer.concat([log, frame, log, frame]);

  const decoder = new FrameDecoder();
  // Feed byte by byte to exercise partial reads.
  const chunks = [...stream].flatMap((b) => decoder.push(bytes(b)));

  assert.deepEqual(
    chunks.map((c) => c.kind),
    ["text", "frame", "text", "frame"],
  );
  const first = chunks[1];
  assert.ok(first.kind === "frame");
  assert.equal(first.type, FrameType.Json);
  assert.deepEqual(first.payload, json);
});

test("decoder rejects a frame with a corrupted byte", () => {
  const frame = encodeFrame(FrameType.Json, new TextEncoder().encode('{"t":"pong"}'));
  frame[5] ^= 0x01;
  const [chunk] = new FrameDecoder().push(frame);
  assert.equal(chunk.kind, "text");
});
