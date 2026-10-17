/** macOS `say`: offline, no API key. Text goes through stdin so it is never parsed as flags. */
import { spawn } from "node:child_process";
import { mkdtemp, readFile, rm } from "node:fs/promises";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { SPEECH_FORMAT, type TtsEngine } from "./tts-engine.js";
import { wavPcmData } from "./wav.js";

export class SayEngine implements TtsEngine {
  constructor(private readonly voice?: string) {}

  async synthesize(text: string): Promise<Buffer> {
    const dir = await mkdtemp(join(tmpdir(), "echoear-tts-"));
    const out = join(dir, "speech.wav");
    try {
      const args = ["-o", out, `--data-format=LEI16@${SPEECH_FORMAT.sampleRate}`];
      if (this.voice) args.push("-v", this.voice);
      await run("say", args, text);
      return wavPcmData(await readFile(out), SPEECH_FORMAT);
    } finally {
      await rm(dir, { recursive: true, force: true });
    }
  }
}

function run(command: string, args: string[], stdin: string): Promise<void> {
  return new Promise((resolve, reject) => {
    const child = spawn(command, args, { stdio: ["pipe", "ignore", "pipe"] });
    let stderr = "";
    child.stderr.on("data", (chunk) => (stderr += chunk));
    child.on("error", reject);
    child.on("close", (code) =>
      code === 0 ? resolve() : reject(new Error(`${command} exited ${code}: ${stderr.trim()}`)),
    );
    child.stdin.end(stdin);
  });
}
