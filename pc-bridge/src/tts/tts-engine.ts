/**
 * Text-to-speech engines. Every engine returns the device's speech format, so adding
 * a cloud engine later only means implementing `synthesize`.
 *
 *   WATCHLING_TTS=say|off        engine (default: say on macOS, off elsewhere)
 *   WATCHLING_TTS_VOICE=Samantha macOS voice name (`say -v '?'` lists them). Unset = the system
 *                              voice from Spoken Content, the only way to use Siri voices;
 *                              `say` silently falls back to its default for unknown names.
 */
import { SayEngine } from "./say-engine.js";

/** 16 kHz mono signed 16-bit little-endian, what the device plays. */
export const SPEECH_FORMAT = { sampleRate: 16000, channels: 1, bitsPerSample: 16 } as const;

export interface TtsEngine {
  /** Raw PCM in SPEECH_FORMAT. */
  synthesize(text: string): Promise<Buffer>;
}

export function createTtsEngine(env: NodeJS.ProcessEnv = process.env): TtsEngine | null {
  const name = env.WATCHLING_TTS ?? (process.platform === "darwin" ? "say" : "off");
  switch (name) {
    case "off":
      return null;
    case "say":
      return new SayEngine(env.WATCHLING_TTS_VOICE);
    default:
      throw new Error(`unknown WATCHLING_TTS engine "${name}" (expected say or off)`);
  }
}
