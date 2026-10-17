import assert from "node:assert/strict";
import { test } from "node:test";
import { wavPcmData } from "./wav.js";

const FORMAT = { sampleRate: 16000, channels: 1, bitsPerSample: 16 };

function chunk(id: string, body: Buffer): Buffer {
  const header = Buffer.alloc(8);
  header.write(id, 0, "ascii");
  header.writeUInt32LE(body.length, 4);
  return Buffer.concat([header, body, body.length & 1 ? Buffer.alloc(1) : Buffer.alloc(0)]);
}

function wav(fmt = FORMAT, extra: Buffer[] = []): Buffer {
  const f = Buffer.alloc(16);
  f.writeUInt16LE(1, 0);
  f.writeUInt16LE(fmt.channels, 2);
  f.writeUInt32LE(fmt.sampleRate, 4);
  f.writeUInt16LE(fmt.bitsPerSample, 14);
  const body = Buffer.concat([Buffer.from("WAVE"), ...extra, chunk("fmt ", f), chunk("data", Buffer.from([1, 2, 3, 4]))]);
  const riff = Buffer.alloc(8);
  riff.write("RIFF", 0, "ascii");
  riff.writeUInt32LE(body.length, 4);
  return Buffer.concat([riff, body]);
}

test("skips leading JUNK chunks (macOS say writes one) and returns the samples", () => {
  assert.deepEqual([...wavPcmData(wav(FORMAT, [chunk("JUNK", Buffer.alloc(27))]), FORMAT)], [1, 2, 3, 4]);
});

test("rejects a sample rate the device cannot play", () => {
  assert.throws(() => wavPcmData(wav({ ...FORMAT, sampleRate: 22050 }), FORMAT), /unexpected WAVE format/);
});
