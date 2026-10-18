import assert from "node:assert/strict";
import { mkdir, mkdtemp, rm, writeFile } from "node:fs/promises";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { test } from "node:test";
import { ASSETS_DIR, ChatterClips, SFX_GAP_MS } from "./chatter-clips.js";
import { CHATTER_LINES } from "./chatter-lines.js";

/** 16 kHz mono s16le WAV with `samples` samples. */
function wav(samples: number, sampleRate = 16000): Buffer {
  const data = Buffer.alloc(samples * 2, 1);
  const header = Buffer.alloc(44);
  header.write("RIFF", 0);
  header.writeUInt32LE(36 + data.length, 4);
  header.write("WAVEfmt ", 8);
  header.writeUInt32LE(16, 16);
  header.writeUInt16LE(1, 20);
  header.writeUInt16LE(1, 22);
  header.writeUInt32LE(sampleRate, 24);
  header.writeUInt32LE(sampleRate * 2, 28);
  header.writeUInt16LE(2, 32);
  header.writeUInt16LE(16, 34);
  header.write("data", 36);
  header.writeUInt32LE(data.length, 40);
  return Buffer.concat([header, data]);
}

const line = (id: string) => CHATTER_LINES.find((l) => l.id === id)!;

async function fixture() {
  const dir = await mkdtemp(join(tmpdir(), "chatter-clips-"));
  await mkdir(join(dir, "chatter"));
  await mkdir(join(dir, "sfx"));
  await writeFile(join(dir, "chatter", "SELF_06.wav"), wav(100)); // happy
  await writeFile(join(dir, "chatter", "CARE_03.wav"), wav(50)); // encouraging
  await writeFile(join(dir, "chatter", "SELF_01.wav"), wav(10, 44100)); // wrong format
  await writeFile(join(dir, "sfx", "happy.wav"), wav(20));
  return dir;
}

test("loads the clips there are, reports unreadable ones", async (t) => {
  const dir = await fixture();
  t.after(() => rm(dir, { recursive: true, force: true }));
  const clips = await ChatterClips.load(dir);
  assert.deepEqual([...clips.available].sort(), ["CARE_03", "SELF_06"]);
  assert.equal(clips.problems.length, 1);
  assert.match(clips.problems[0], /SELF_01\.wav/);
});

test("joins the mood sound, a short gap and the line; maps the face", async (t) => {
  const dir = await fixture();
  t.after(() => rm(dir, { recursive: true, force: true }));
  const clips = await ChatterClips.load(dir);

  const withSfx = clips.build(line("SELF_06"), () => 0);
  assert.equal(withSfx.sfx, "happy");
  assert.deepEqual(withSfx.face, { mood: "happy" });
  assert.equal(withSfx.pcm.length, (20 + 16 * SFX_GAP_MS + 100) * 2);

  const plain = clips.build(line("SELF_06"), () => 0.9);
  assert.equal(plain.sfx, null);
  assert.equal(plain.pcm.length, 200);

  assert.equal(clips.build(line("CARE_03"), () => 0).sfx, null, "encouraging lines go without a sound");
});

test("curious lines glance to a side", async (t) => {
  const dir = await fixture();
  t.after(() => rm(dir, { recursive: true, force: true }));
  await writeFile(join(dir, "chatter", "SELF_01.wav"), wav(10));
  const clips = await ChatterClips.load(dir);
  assert.deepEqual(clips.build(line("SELF_01"), () => 0.2).face, { mood: "idle", look: "left" });
  assert.deepEqual(clips.build(line("SELF_01"), () => 0.8).face, { mood: "idle", look: "right" });
});

test("the shipped sound effects load", async () => {
  const clips = await ChatterClips.load(ASSETS_DIR);
  assert.deepEqual(clips.problems, []);
});
