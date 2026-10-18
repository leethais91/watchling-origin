/**
 * Pre-generated chatter audio, loaded once at start: one clip per line (assets/chatter/<id>.wav,
 * Gemini TTS) and five mood sounds (assets/sfx/<name>.wav, ElevenLabs). A line without a clip is
 * simply never picked. Sound effects stay separate files and are joined in front of a line here.
 */
import { readFile } from "node:fs/promises";
import { join } from "node:path";
import { fileURLToPath } from "node:url";
import type { Face } from "../speech/speaker.js";
import { SPEECH_FORMAT } from "../tts/tts-engine.js";
import { wavPcmData } from "../tts/wav.js";
import { CHATTER_LINES, type ChatterLine, type Emotion } from "./chatter-lines.js";

/** pc-bridge/assets, from both src/chatter and dist/chatter. */
export const ASSETS_DIR = fileURLToPath(new URL("../../assets/", import.meta.url));

/** Share of played lines that get a mood sound in front. */
export const SFX_PROBABILITY = 0.25;
/** Pause between the mood sound and the line. */
export const SFX_GAP_MS = 150;

export type SfxName = "curious" | "playful" | "sleepy" | "surprised" | "happy";
const SFX_NAMES: SfxName[] = ["curious", "playful", "sleepy", "surprised", "happy"];

/** Mood sound per emotion; encouraging lines are gentle and go without one. */
const EMOTION_SFX: Record<Emotion, SfxName | null> = {
  happy: "happy",
  proud: "happy",
  playful: "playful",
  curious: "curious",
  confused: "curious",
  sleepy: "sleepy",
  surprised: "surprised",
  encouraging: null,
};

/** The firmware has no wink or puzzled eyes, so emotions map onto the moods it has. */
function faceFor(emotion: Emotion, random: () => number): Face {
  switch (emotion) {
    case "curious":
      return { mood: "idle", look: random() < 0.5 ? "left" : "right" };
    case "confused":
    case "surprised":
      return { mood: "surprised" };
    case "sleepy":
      return { mood: "sleep" };
    default:
      return { mood: "happy" };
  }
}

export interface ChatterClip {
  pcm: Buffer;
  face: Face;
  sfx: SfxName | null;
}

export class ChatterClips {
  /** Reads every clip under `dir`. Missing files are skipped; unreadable ones are listed in `problems`. */
  static async load(dir = ASSETS_DIR): Promise<ChatterClips> {
    const problems: string[] = [];
    const read = async (path: string): Promise<Buffer | null> => {
      try {
        return wavPcmData(await readFile(path), SPEECH_FORMAT);
      } catch (err) {
        if ((err as NodeJS.ErrnoException).code !== "ENOENT") problems.push(`${path}: ${(err as Error).message}`);
        return null;
      }
    };
    const lines = new Map<string, Buffer>();
    for (const line of CHATTER_LINES) {
      const pcm = await read(join(dir, "chatter", `${line.id}.wav`));
      if (pcm) lines.set(line.id, pcm);
    }
    const sfx = new Map<SfxName, Buffer>();
    for (const name of SFX_NAMES) {
      const pcm = await read(join(dir, "sfx", `${name}.wav`));
      if (pcm) sfx.set(name, pcm);
    }
    return new ChatterClips(lines, sfx, problems);
  }

  constructor(
    private readonly lines: ReadonlyMap<string, Buffer>,
    private readonly sfx: ReadonlyMap<SfxName, Buffer>,
    readonly problems: string[] = [],
  ) {}

  /** Ids of the lines that have a clip. */
  get available(): ReadonlySet<string> {
    return new Set(this.lines.keys());
  }

  /** The clip for `line` (which must be available), sometimes with its mood sound in front. */
  build(line: ChatterLine, random: () => number): ChatterClip {
    const speech = this.lines.get(line.id);
    if (!speech) throw new Error(`no clip for ${line.id}`);
    const face = faceFor(line.emotion, random);
    const name = EMOTION_SFX[line.emotion];
    const sound = name && random() < SFX_PROBABILITY ? this.sfx.get(name) : undefined;
    if (!name || !sound) return { pcm: speech, face, sfx: null };
    const gap = Buffer.alloc(((SPEECH_FORMAT.sampleRate * SFX_GAP_MS) / 1000) * 2);
    return { pcm: Buffer.concat([sound, gap, speech]), face, sfx: name };
  }
}
