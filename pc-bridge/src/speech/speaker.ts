/**
 * Synthesizes phrases (or takes ready-made clips) and streams them to the device one at a time:
 *   {t:"say", id, bytes, mood?, look?}  →  audio frames (id:u16 LE | offset:u32 LE | pcm)  →  {t:"say_end", id}
 * The device buffers the whole clip and plays it after any chime already queued.
 */
import type { FaceLook, FaceMood, HostMessage } from "../protocol/messages.js";
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

/** Face shown on the device while a clip plays. */
export interface Face {
  mood: FaceMood;
  look?: FaceLook;
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
    return this.enqueue(async () => {
      if (this.link.alive) this.send(await engine.synthesize(text));
    });
  }

  /** Queues pre-generated PCM in SPEECH_FORMAT, with the face to show while it plays. Never rejects. */
  playClip(pcm: Buffer, face?: Face): Promise<void> {
    return this.enqueue(async () => {
      if (this.link.alive) this.send(pcm, face);
    });
  }

  private enqueue(job: () => Promise<void>): Promise<void> {
    this.queue = this.queue.then(job).catch((err: Error) => this.onError(err));
    return this.queue;
  }

  private send(pcm: Buffer, face?: Face): void {
    const clip = pcm.subarray(0, Math.min(pcm.length, MAX_SPEECH_BYTES) & ~1);
    if (clip.length === 0) return;

    const id = this.nextId;
    this.nextId = (this.nextId % 0xffff) + 1;
    const faceFields = face ? { mood: face.mood, ...(face.look && { look: face.look }) } : {};
    if (!this.link.send({ t: "say", id, bytes: clip.length, ...faceFields })) return;
    for (let offset = 0; offset < clip.length; offset += SPEECH_CHUNK_BYTES) {
      const header = Buffer.alloc(6);
      header.writeUInt16LE(id, 0);
      header.writeUInt32LE(offset, 2);
      this.link.sendAudio(Buffer.concat([header, clip.subarray(offset, offset + SPEECH_CHUNK_BYTES)]));
    }
    this.link.send({ t: "say_end", id });
  }
}
