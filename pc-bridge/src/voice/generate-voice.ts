/**
 * Makes the chatter voice clips with Gemini TTS. Run by hand; needs GEMINI_API_KEY (the bridge
 * itself never calls the API). See docs/chatter-design-v3.md.
 *
 *   npm run voice:design                          create the candidate voices (+ their samples)
 *   npm run voice:audition                        same few lines in every candidate voice
 *   npm run voice:generate -- --voice voice_...   every catalog line → assets/chatter/<id>.wav
 *   npm run voice:generate -- --voice voice_... --ids SELF_03,DEV_07   regenerate some lines
 *   npx tsx src/voice/generate-voice.ts page     rebuild the listening page from the clips on disk
 *
 * Listening pages land in pc-bridge/voice-candidates/ (gitignored).
 */
import { existsSync } from "node:fs";
import { mkdir, readFile, writeFile } from "node:fs/promises";
import { join } from "node:path";
import { parseArgs } from "node:util";
import { ASSETS_DIR } from "../chatter/chatter-clips.js";
import { CHATTER_LINES, type ChatterLine, type Emotion } from "../chatter/chatter-lines.js";
import { encodeWav, toDevicePcm, trimSilence } from "./pcm-tools.js";
import { MANIFEST_PATH, type Manifest, textHash } from "./voice-manifest.js";

const API = "https://generativelanguage.googleapis.com/v1beta";
const MODEL = "gemini-3.8-flash-tts";
const CANDIDATES_DIR = join(ASSETS_DIR, "..", "voice-candidates");

/**
 * Identity only (gender, timbre, accent); emotion goes in each clip's style. Prompts naming a child
 * or an age are blocked by Gemini's safety policy, so cuteness is described through the voice itself.
 */
const VOICE_CANDIDATES = [
  {
    name: "english-robot",
    prompt:
      "A cute little desk robot with a high-pitched, sweet, playful voice and a light synthetic sparkle, " +
      "speaking American English.",
  },
];

const EMOTION_STYLE: Record<Emotion, string> = {
  happy: "cheerful, warm, smiling voice",
  playful: "lightly teasing, mischievous, conversational",
  curious: "inquisitive, gentle rising intonation",
  confused: "slightly puzzled, hesitant, natural pauses",
  sleepy: "relaxed, soft, slower delivery",
  surprised: "brief, gentle surprise, expressive",
  proud: "playfully confident, lighthearted",
  encouraging: "warm, reassuring, gentle",
};

/** Lines that need more direction than their emotion gives. */
const STYLE_OVERRIDES: Record<string, string> = {
  SELF_03:
    "playful, slightly confused: a brief pause after the question, then the last phrase as a soft, " +
    "amused realization",
  SELF_22: "a soft conspiratorial whisper, then the final words with cute comedic timing",
  CTX_06: "friendly, mild surprise and curiosity; clear but not alarming, lighthearted",
};

/** Mixed emotions plus the English words most likely to be mispronounced. */
const AUDITION_IDS = ["SELF_03", "SELF_22", "SELF_04", "SELF_21", "DEV_07", "CTX_06"];

function styleFor(line: ChatterLine): string {
  const style = STYLE_OVERRIDES[line.id] ?? EMOTION_STYLE[line.emotion];
  return `Natural American English, cute, adorable and playful, sweet and expressive, not shouting; ${style}`;
}

function apiKey(): string {
  const key = process.env.GEMINI_API_KEY;
  if (!key) throw new Error("GEMINI_API_KEY is not set");
  return key;
}

/** POSTs JSON, retrying rate limits and server errors with backoff. */
async function post(path: string, body: unknown): Promise<any> {
  for (let attempt = 1; ; attempt++) {
    const res = await fetch(`${API}/${path}`, {
      method: "POST",
      headers: {
        "x-goog-api-key": apiKey(),
        "Content-Type": "application/json",
      },
      body: JSON.stringify(body),
    });
    if (res.ok) return res.json();
    const detail = (await res.text()).slice(0, 500);
    if ((res.status === 429 || res.status >= 500) && attempt < 6) {
      const waitMs = 2000 * 2 ** attempt;
      console.warn(`  ${res.status}, retrying in ${waitMs / 1000} s`);
      await new Promise((resolve) => setTimeout(resolve, waitMs));
      continue;
    }
    throw new Error(`Gemini ${path} ${res.status}: ${detail}`);
  }
}

/** One line in `voice`, as trimmed PCM in the device format. */
async function synthesize(voice: string, line: ChatterLine): Promise<Buffer> {
  const res = await post("interactions", {
    model: MODEL,
    input: [
      {
        type: "user_input",
        content: [
          {
            type: "text",
            text: line.text,
            annotations: [{ type: "speech_metadata", style: styleFor(line) }],
          },
        ],
      },
    ],
    response_format: { type: "audio", mime_type: "audio/wav" },
    generation_config: { speech_config: [{ voice }] },
  });
  const audio = (res.steps ?? [])
    .filter((step: any) => step.type === "model_output")
    .flatMap((step: any) => step.content ?? [])
    .filter((part: any) => part.type === "audio")
    .at(-1);
  if (!audio?.data) throw new Error(`no audio for ${line.id}: ${JSON.stringify(res).slice(0, 300)}`);
  return trimSilence(await toDevicePcm(Buffer.from(audio.data, "base64")));
}

function listeningPage(title: string, items: { label: string; text: string; src: string }[]): string {
  const esc = (s: string) => s.replace(/&/g, "&amp;").replace(/</g, "&lt;");
  const rows = items
    .map(
      (i) =>
        `<li><code>${esc(i.label)}</code> ${esc(i.text)}<br><audio controls preload="none" src="${i.src}"></audio></li>`,
    )
    .join("\n");
  return `<!doctype html><meta charset="utf-8"><title>${esc(title)}</title>
<style>body{font:15px system-ui;margin:24px;background:#111;color:#eee}li{margin:12px 0}code{color:#9cf}</style>
<h1>${esc(title)}</h1><ol>${rows}</ol>`;
}

async function design(): Promise<void> {
  await mkdir(CANDIDATES_DIR, { recursive: true });
  const voices: { name: string; id: string }[] = [];
  for (const candidate of VOICE_CANDIDATES) {
    let res: any;
    try {
      res = await post("voices", {
        store: true,
        voice: {
          model: MODEL,
          type: "prompted",
          display_name: `echoear-${candidate.name}`,
          gender: "female",
          language_code: "en-US",
          prompted: { input: candidate.prompt },
        },
      });
    } catch (err) {
      console.warn(`${candidate.name}: skipped, ${(err as Error).message.split("\n")[0]}`);
      continue;
    }
    voices.push({ name: candidate.name, id: res.id });
    if (res.sample_audio?.data) {
      await writeFile(
        join(CANDIDATES_DIR, `${candidate.name}-sample.wav`),
        Buffer.from(res.sample_audio.data, "base64"),
      );
    }
    console.log(`${candidate.name}: ${res.id}`);
  }
  await writeFile(join(CANDIDATES_DIR, "voices.json"), JSON.stringify(voices, null, 2));
}

async function audition(): Promise<void> {
  const voices: { name: string; id: string }[] = JSON.parse(
    await readFile(join(CANDIDATES_DIR, "voices.json"), "utf8"),
  );
  const lines = AUDITION_IDS.map((id) => CHATTER_LINES.find((line) => line.id === id)!);
  const items: { label: string; text: string; src: string }[] = [];
  for (const voice of voices) {
    await mkdir(join(CANDIDATES_DIR, voice.name), { recursive: true });
    items.push({
      label: `${voice.name} (design sample)`,
      text: voice.id,
      src: `${voice.name}-sample.wav`,
    });
    for (const line of lines) {
      const pcm = await synthesize(voice.id, line);
      await writeFile(join(CANDIDATES_DIR, voice.name, `${line.id}.wav`), encodeWav(pcm));
      items.push({
        label: `${voice.name} ${line.id}`,
        text: line.text,
        src: `${voice.name}/${line.id}.wav`,
      });
      console.log(`${voice.name} ${line.id} ${Math.round(pcm.length / 32)} ms`);
    }
  }
  await writeFile(join(CANDIDATES_DIR, "audition.html"), listeningPage("Chatter voice audition", items));
  console.log(`open ${join(CANDIDATES_DIR, "audition.html")}`);
}

async function generate(voice: string, ids: string[] | null): Promise<void> {
  const lines = ids ? CHATTER_LINES.filter((line) => ids.includes(line.id)) : CHATTER_LINES;
  if (ids && lines.length !== ids.length) throw new Error(`unknown ids in ${ids.join(",")}`);
  let manifest: Manifest = { model: MODEL, voiceId: voice, lines: {} };
  try {
    manifest = JSON.parse(await readFile(MANIFEST_PATH, "utf8"));
  } catch {
    // first run
  }
  if (manifest.voiceId !== voice) {
    if (ids) throw new Error(`manifest voice is ${manifest.voiceId}; regenerate everything to switch voices`);
    manifest = { model: MODEL, voiceId: voice, lines: {} };
  }
  await mkdir(join(ASSETS_DIR, "chatter"), { recursive: true });
  try {
    for (const line of lines) {
      const pcm = await synthesize(voice, line);
      await writeFile(join(ASSETS_DIR, "chatter", `${line.id}.wav`), encodeWav(pcm));
      manifest.lines[line.id] = { textHash: textHash(line.text) };
      await writeFile(MANIFEST_PATH, `${JSON.stringify(manifest, null, 2)}\n`);
      console.log(`${line.id} ${Math.round(pcm.length / 32)} ms  ${line.text}`);
    }
  } finally {
    // Also after a failed run (e.g. the daily quota), so the clips made so far can be heard.
    await writeChatterPage();
  }
}

/** Lists every catalog line that has a clip, with its text. */
async function writeChatterPage(): Promise<void> {
  await mkdir(CANDIDATES_DIR, { recursive: true });
  const items = CHATTER_LINES.filter((line) => existsSync(join(ASSETS_DIR, "chatter", `${line.id}.wav`))).map(
    (line) => ({ label: line.id, text: line.text, src: `../assets/chatter/${line.id}.wav` }),
  );
  await writeFile(
    join(CANDIDATES_DIR, "chatter.html"),
    listeningPage(`Chatter clips (${items.length}/${CHATTER_LINES.length})`, items),
  );
  console.log(`open ${join(CANDIDATES_DIR, "chatter.html")}`);
}

const { positionals, values } = parseArgs({
  allowPositionals: true,
  options: { voice: { type: "string" }, ids: { type: "string" } },
});
switch (positionals[0]) {
  case "design":
    await design();
    break;
  case "audition":
    await audition();
    break;
  case "page":
    await writeChatterPage();
    break;
  case "generate":
    if (!values.voice) throw new Error("--voice voice_... is required");
    await generate(values.voice, values.ids ? values.ids.split(",") : null);
    break;
  default:
    throw new Error("usage: generate-voice.ts design | audition | page | generate --voice <id> [--ids A,B]");
}
