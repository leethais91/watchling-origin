/**
 * assets/manifest.json: which designed voice made the chatter clips and the text each clip was
 * made from, so a test can tell when a line was edited without regenerating its clip.
 */
import { createHash } from "node:crypto";
import { join } from "node:path";
import { ASSETS_DIR } from "../chatter/chatter-clips.js";

export const MANIFEST_PATH = join(ASSETS_DIR, "manifest.json");

export interface Manifest {
  model: string;
  /** Gemini designed voice id (`voice_...`); reuse it for new lines so they sound the same. */
  voiceId: string;
  lines: Record<string, { textHash: string }>;
}

export function textHash(text: string): string {
  return createHash("sha256").update(text).digest("hex").slice(0, 16);
}
