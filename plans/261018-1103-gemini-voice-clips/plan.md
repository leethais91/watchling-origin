# Gemini voice clips — Plan

Status: chatter part superseded by `plans/261018-1405-chatter-v3/`; mood clips deferred (reuse that plan's generator + voice ID) · Created: 2026-10-18

Pre-generated voice clips for the robot's moods and idle chatter, made with Gemini 3.8 Flash TTS
and a custom designed voice, stored in the bridge and played over the existing speech path.

## Decisions (user, 2026-10-18)
- Scope: mood reactions **and** idle chatter. Chatter switches from live `say -v Linh` to
  pre-generated clips; Linh stays as the fallback for any line without a clip.
- Device-only moods (surprised on tap, sleep when dozing) get sound too: the device reports
  them to the bridge with a new device → bridge message.
- Chimes stay; the voice clip plays right after the chime (no firmware change for chimes).
- Clips are stored in the bridge (`pc-bridge/assets/voice/`), committed, so the bridge needs no
  API key at runtime.
- Research voice design before generating any sound (done, below).

## Findings
- Model `gemini-3.8-flash-tts` (GA 2026-09-22), Vietnamese supported, 30 prebuilt voices.
  https://ai.google.dev/gemini-api/docs/models/gemini-3.8-flash-tts
- Synthesis: `POST /v1beta/interactions`, body `model`, `input[{type:"user_input", content:[{type:
  "text", text, annotations:[{type:"speech_metadata", style}]}]}]`, `response_format`,
  `generation_config.speech_config: [{voice}]`. Audio at
  `steps[type=model_output].content[type=audio].data` (base64).
  https://ai.google.dev/gemini-api/docs/speech-generation
- `response_format: {type:"audio", mime_type:"audio/wav"|"audio/l16", sample_rate:16000}`
  gives the device format (16 kHz mono s16le) directly: no resampling. Untested: the first
  request failed with 403 because `GEMINI_API_KEY` in launchctl is empty.
- Inline tags (English even for Vietnamese text): `<gasp>`, `<giggle>`, `<laugh>`, `<cheer>`,
  `<sigh>`, `<yawn>`, `<argh>`, `<groan>`, `<whispers>`, `<short pause>`, `<long pause>`, …
- Voice design: `POST /v1beta/voices` with `store:true`, `voice:{model, type:"prompted",
  display_name, gender, language_code, prompted:{input}}` returns a persistent `voice_...` ID
  plus `sample_audio` (WAV). Use the ID wherever a voice name goes. Limit 200 stored voices per
  project, 1-year TTL (only matters for generating new lines later).
  https://ai.google.dev/gemini-api/docs/voice-design
- Google's guidance: permanent traits (age, gender, timbre, accent) in the voice design prompt,
  1–2 concise sentences; situational emotion in a short `speech_metadata.style` per clip.

## Voice design
Three layers:

1. **Identity** (voice design, once). Candidates, all `language_code: "vi-VN"`:
   - A, bright little robot: "A tiny, cheerful desk robot with a bright, high, youthful voice and
     a light playful bounce, speaking Vietnamese with a soft Saigon accent."
   - B, cheeky sidekick: "A cheeky little boy-like robot sidekick with a clear, energetic voice
     and quick comic timing, speaking Vietnamese with a Southern accent."
   - C, cute synthetic: "A small friendly robot with a crisp, slightly flat, synthetic-sounding
     voice that still feels warm, speaking Vietnamese with a gentle Southern accent."
2. **Emotion** (`style` + inline tags per clip):

   | Mood | Style | Sample line |
   |---|---|---|
   | happy | proud and delighted | `<cheer>` Yeah, xong rồi nè! |
   | attention | eager, calling for attention | Ê ê, cần bạn nè! |
   | error | pained but comic | `<argh>` Ui da… lỗi rồi. |
   | surprised | startled, then giggly | `<gasp>` Hả?! `<giggle>` Nhột quá! |
   | sleep | sleepy, drowsy, very soft | `<yawn>` Buồn ngủ quá… khò… khò… |
   | chatter | casual, playful, a bit bored | lines from `docs/chatter-design.md` |

   Two variants per mood so repeated taps don't always sound the same.
3. **Audition**: create the 3 voices, synthesize the same ~5 lines (one per mood + one chatter)
   with each, build an HTML page to compare, user picks one voice, then generate all ~30 clips.

## Implementation
### Generator (`pc-bridge`, run by hand, needs `GEMINI_API_KEY`)
- `src/voice/voice-catalog.ts`: clip id → `{text, style}` for moods (2 variants each) and every
  chatter line. Shared with the runtime so ids can't drift.
- `src/voice/generate-voice-clips.ts`:
  - `design`: create the candidate voices, save their sample audio and IDs.
  - `audition --voices <ids>`: synthesize the audition lines per voice into
    `voice-candidates/<voice>/` + `index.html` (gitignored).
  - `generate --voice <id>`: synthesize the whole catalog into `assets/voice/<clip-id>.wav`
    (16 kHz mono). Sequential, retries on 429.
- npm scripts `voice:design`, `voice:audition`, `voice:generate`. Chosen voice ID recorded in
  `voice-catalog.ts` so new lines can be generated later in the same voice.
- Optional post-processing (ffmpeg pitch/ring modulation) only if no candidate sounds robotic
  enough.

### Bridge runtime
- `Speaker`: add `playClip(pcm)` on the same queue as `say()` (one queue, one id counter).
- Mood clips: event `done` → happy, `attention` → attention, `error` → error, queued before
  the spoken summary of a finished task. Device `mood` message → surprised / sleep. At most one
  mood clip per 5 s so repeated taps don't pile up.
- Chatter: `chatterLine()` returns `{id, text}`; play the clip if it exists, else `say -v Linh`.
  The cost line becomes a fixed line ("Hôm nay đốt nhiều tiền lắm rồi đó") so it can be
  pre-generated.

### Firmware
- `ui_init(on_decision, link_probe, on_mood)`; `on_mood("surprised")` on tap,
  `on_mood("sleep")` once when dozing starts (only while the bridge is linked).
- `main.c`: send `{"t":"mood","mood":"surprised"|"sleep"}` via `host_link_send_json`.
- Simulator: pass a stub callback.

### Docs
- `docs/device-protocol.md`: new device → bridge `mood` message.
- `docs/chatter-design.md`: lines now come from clips; how to regenerate.
- README: voice clip workflow.

## Validation
- Unit tests: catalog ids unique and every chatter line has an id; clip lookup falls back to
  Linh; mood throttle; device `mood` message handling.
- Listen to every generated clip before committing; check none exceeds 15 s (device limit).
- On device: tap → "Hả?!" after the touch; doze → yawn; done → chime then "Yeah, xong rồi nè!".

## Open questions
- Voice gender/region preference (candidates assume a Southern accent).
- Valid `gender` values for a robot voice (docs only show "male").
- Whether voice design can sound robotic enough or needs post-processing.
- Pricing not checked (one-off: ~3 voice creations + ~15 audition clips + ~30 clips).
