/** Small helpers for 16-bit mono PCM in the device format (SPEECH_FORMAT). */
import { spawn } from "node:child_process";
import { SPEECH_FORMAT } from "../tts/tts-engine.js";

/** Any audio file ffmpeg reads → raw PCM in SPEECH_FORMAT (Gemini returns 24 kHz WAV). */
export function toDevicePcm(audio: Buffer): Promise<Buffer> {
  return new Promise((resolve, reject) => {
    const args = ["-v", "error", "-i", "pipe:0", "-ac", "1", "-ar", String(SPEECH_FORMAT.sampleRate), "-f", "s16le", "pipe:1"];
    const ffmpeg = spawn("ffmpeg", args, { stdio: ["pipe", "pipe", "pipe"] });
    const out: Buffer[] = [];
    let stderr = "";
    ffmpeg.stdout.on("data", (chunk: Buffer) => out.push(chunk));
    ffmpeg.stderr.on("data", (chunk) => (stderr += chunk));
    ffmpeg.on("error", reject);
    ffmpeg.on("close", (code) =>
      code === 0 ? resolve(Buffer.concat(out)) : reject(new Error(`ffmpeg exited ${code}: ${stderr.trim()}`)),
    );
    ffmpeg.stdin.end(audio);
  });
}

/** Below this (fraction of full scale) a sample counts as silence. */
const SILENCE = 0.01;
/** Kept around the speech so the first and last syllables aren't clipped. */
const PAD_MS = 40;

/** Drops leading and trailing silence, keeping a short pad. All-silent input comes back empty. */
export function trimSilence(pcm: Buffer): Buffer {
  const samples = pcm.length >> 1;
  const threshold = SILENCE * 32767;
  let first = 0;
  while (first < samples && Math.abs(pcm.readInt16LE(first * 2)) <= threshold) first++;
  if (first === samples) return Buffer.alloc(0);
  let last = samples - 1;
  while (Math.abs(pcm.readInt16LE(last * 2)) <= threshold) last--;
  const pad = (SPEECH_FORMAT.sampleRate * PAD_MS) / 1000;
  return pcm.subarray(Math.max(0, first - pad) * 2, Math.min(samples, last + 1 + pad) * 2);
}

/** Wraps PCM in a 44-byte RIFF/WAVE header. */
export function encodeWav(pcm: Buffer): Buffer {
  const { sampleRate, channels, bitsPerSample } = SPEECH_FORMAT;
  const blockAlign = (channels * bitsPerSample) / 8;
  const header = Buffer.alloc(44);
  header.write("RIFF", 0, "ascii");
  header.writeUInt32LE(36 + pcm.length, 4);
  header.write("WAVEfmt ", 8, "ascii");
  header.writeUInt32LE(16, 16);
  header.writeUInt16LE(1, 20);
  header.writeUInt16LE(channels, 22);
  header.writeUInt32LE(sampleRate, 24);
  header.writeUInt32LE(sampleRate * blockAlign, 28);
  header.writeUInt16LE(blockAlign, 32);
  header.writeUInt16LE(bitsPerSample, 34);
  header.write("data", 36, "ascii");
  header.writeUInt32LE(pcm.length, 40);
  return Buffer.concat([header, pcm]);
}
