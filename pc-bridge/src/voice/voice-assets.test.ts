import assert from "node:assert/strict";
import { existsSync, readdirSync, readFileSync } from "node:fs";
import { join } from "node:path";
import { test } from "node:test";
import { ASSETS_DIR } from "../chatter/chatter-clips.js";
import { CHATTER_LINES } from "../chatter/chatter-lines.js";
import { MAX_SPEECH_BYTES } from "../speech/speaker.js";
import { SPEECH_FORMAT } from "../tts/tts-engine.js";
import { wavPcmData } from "../tts/wav.js";
import { encodeWav, trimSilence } from "./pcm-tools.js";
import { MANIFEST_PATH, type Manifest, textHash } from "./voice-manifest.js";

const CLIP_DIR = join(ASSETS_DIR, "chatter");

test("trimSilence keeps a short pad around the sound; encodeWav round-trips", () => {
  const pcm = Buffer.alloc(16000 * 2); // 1 s of silence
  pcm.writeInt16LE(10000, 8000 * 2); // one loud sample at 0.5 s
  const trimmed = trimSilence(pcm);
  assert.equal(trimmed.length, (640 + 1 + 640) * 2, "40 ms pad on each side");
  assert.equal(trimSilence(Buffer.alloc(100)).length, 0);
  assert.deepEqual(wavPcmData(encodeWav(trimmed), SPEECH_FORMAT), trimmed);
});

test("every chatter clip matches its line's text and fits the device", (t) => {
  const clips = existsSync(CLIP_DIR) ? readdirSync(CLIP_DIR).filter((f) => f.endsWith(".wav")) : [];
  if (clips.length === 0) return t.skip("no chatter clips generated yet");
  const manifest: Manifest = JSON.parse(readFileSync(MANIFEST_PATH, "utf8"));
  for (const file of clips) {
    const id = file.replace(/\.wav$/, "");
    const line = CHATTER_LINES.find((l) => l.id === id);
    assert.ok(line, `${file} is not in the catalog`);
    assert.equal(manifest.lines[id]?.textHash, textHash(line.text), `${id}: text changed, regenerate its clip`);
    const pcm = wavPcmData(readFileSync(join(CLIP_DIR, file)), SPEECH_FORMAT);
    assert.ok(pcm.length > 0 && pcm.length <= MAX_SPEECH_BYTES, `${id}: ${pcm.length} bytes`);
  }
});
