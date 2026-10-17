import assert from "node:assert/strict";
import { test } from "node:test";
import type { HostMessage } from "../protocol/messages.js";
import { MAX_SPEECH_BYTES, SPEECH_CHUNK_BYTES, Speaker, type SpeechLink } from "./speaker.js";

function fakeLink(alive = true) {
  const sent: Array<HostMessage | { audio: { id: number; offset: number; bytes: number } }> = [];
  const link: SpeechLink = {
    alive,
    send: (msg) => (sent.push(msg), true),
    sendAudio: (p) => {
      const b = Buffer.from(p);
      sent.push({ audio: { id: b.readUInt16LE(0), offset: b.readUInt32LE(2), bytes: b.length - 6 } });
      return true;
    },
  };
  return { link, sent };
}

const engine = (bytes: number) => ({ synthesize: async () => Buffer.alloc(bytes) });

test("streams a clip as header, ordered chunks, end", async () => {
  const { link, sent } = fakeLink();
  await new Speaker(engine(SPEECH_CHUNK_BYTES + 10), link, assert.fail).say("hi");
  assert.deepEqual(sent, [
    { t: "say", id: 1, bytes: SPEECH_CHUNK_BYTES + 10 },
    { audio: { id: 1, offset: 0, bytes: SPEECH_CHUNK_BYTES } },
    { audio: { id: 1, offset: SPEECH_CHUNK_BYTES, bytes: 10 } },
    { t: "say_end", id: 1 },
  ]);
});

test("caps clips at the device limit and skips when the device is gone", async () => {
  const { link, sent } = fakeLink();
  await new Speaker(engine(MAX_SPEECH_BYTES + 3001), link, assert.fail).say("long");
  assert.deepEqual(sent[0], { t: "say", id: 1, bytes: MAX_SPEECH_BYTES });

  const offline = fakeLink(false);
  await new Speaker(engine(100), offline.link, assert.fail).say("hi");
  assert.equal(offline.sent.length, 0);
});

test("a failing engine reports the error and later phrases still play", async () => {
  const { link, sent } = fakeLink();
  const errors: string[] = [];
  let calls = 0;
  const flaky = { synthesize: async () => (++calls === 1 ? Promise.reject(new Error("boom")) : Buffer.alloc(4)) };
  const speaker = new Speaker(flaky, link, (err) => errors.push(err.message));
  void speaker.say("first");
  await speaker.say("second");
  assert.deepEqual(errors, ["boom"]);
  assert.deepEqual(sent.at(-1), { t: "say_end", id: 1 });
});
