/**
 * Synthesizes phrases and streams them to the device one at a time:
 *   {t:"say", id, bytes}  →  audio frames (id:u16 LE | offset:u32 LE | pcm)  →  {t:"say_end", id}
 * The device buffers the whole clip and plays it after any chime already queued.
 */
import type { HostMessage } from "../protocol/messages.js";
import { SPEECH_FORMAT, type TtsEngine } from "../tts/tts-engine.js";

/** 15 s of speech; the device rejects larger clips. Keep in sync with SPEECH_MAX_BYTES in audio.c. */
export const MAX_SPEECH_BYTES = 15 * SPEECH_FORMAT.sampleRate * 2;
/** PCM bytes per audio frame; with the 6-byte header this stays under the 4096-byte frame limit. */
export const SPEECH_CHUNK_BYTES = 4000;

export interface SpeechLink {
  readonly alive: boolean;
  send(msg: HostMessage): boolean;
  sendAudio(payload: Uint8Array): boolean;
}

export class Speaker {
  private nextId = 1;
  private queue: Promise<void> = Promise.resolve();

  constructor(
    private readonly engine: TtsEngine,
    private readonly link: SpeechLink,
    private readonly onError: (err: Error) => void,
  ) {}

  /**
   * Queues `text`, spoken by `engine` (default: the one given to the constructor). Resolves
   * once it has been sent (or skipped). Never rejects.
   */
  say(text: string, engine: TtsEngine = this.engine): Promise<void> {
    this.queue = this.queue.then(() => this.speak(text, engine)).catch((err: Error) => this.onError(err));
    return this.queue;
  }

  private async speak(text: string, engine: TtsEngine): Promise<void> {
    if (!this.link.alive) return;
    const pcm = await engine.synthesize(text);
    const clip = pcm.subarray(0, Math.min(pcm.length, MAX_SPEECH_BYTES) & ~1);
    if (clip.length === 0) return;

    const id = this.nextId;
    this.nextId = (this.nextId % 0xffff) + 1;
    if (!this.link.send({ t: "say", id, bytes: clip.length })) return;
    for (let offset = 0; offset < clip.length; offset += SPEECH_CHUNK_BYTES) {
      const header = Buffer.alloc(6);
      header.writeUInt16LE(id, 0);
      header.writeUInt32LE(offset, 2);
      this.link.sendAudio(Buffer.concat([header, clip.subarray(offset, offset + SPEECH_CHUNK_BYTES)]));
    }
    this.link.send({ t: "say_end", id });
  }
}
