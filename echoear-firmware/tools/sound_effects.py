#!/usr/bin/env python3
"""UI sound effects for the device, generated with the ElevenLabs Sound Effects API.

  ELEVENLABS_API_KEY=... python3 tools/sound_effects.py generate --style premium [--variants 4] [--only done]
      Writes candidates to tools/sfx-candidates/<style>/<effect>-<n>.wav (already trimmed
      and levelled, so what you hear is what the device plays). Open
      tools/sfx-candidates/index.html to compare and pick.

  python3 tools/sound_effects.py embed boot=premium:1 working=premium:2 done=premium:1 attention=premium:3 error=premium:1
      Re-shapes the picked candidates into main/assets/sounds/ and regenerates
      main/assets/sound_effects.c, which audio.c plays from flash. Styles can be mixed.

Stdlib only. The API key is read from the environment and never written anywhere.
"""
import argparse
import array
import json
import os
import sys
import urllib.error
import urllib.request
import wave
from pathlib import Path

FIRMWARE = Path(__file__).resolve().parent.parent
CANDIDATES = FIRMWARE / "tools" / "sfx-candidates"
SOUNDS = FIRMWARE / "main" / "assets" / "sounds"
C_OUT = FIRMWARE / "main" / "assets" / "sound_effects.c"

MAX_PROMPT_CHARS = 450  # ElevenLabs limit for the whole text
API_URL = "https://api.elevenlabs.io/v1/sound-generation?output_format=pcm_16000"
SAMPLE_RATE = 16000  # device format: 16 kHz mono s16le

# Each style shares one suffix so its sounds feel like a family. Per effect: prompt and the
# length to ask ElevenLabs for.
STYLES = {
    "premium": {
        "suffix": "premium futuristic AI desk companion UI sound, warm rounded glass-like synth, soft "
                  "crystalline shimmer, minimal reverb, smooth decay, no speech, no music, no harsh highs",
        "boot": ("tiny AI robot gently waking up: a soft airy power-up sweep, then three delicate ascending "
                 "glass synth notes, ending in a subtle luminous shimmer that fades naturally; no motors, "
                 "no laser, no arcade", 2.0),
        "working": ("AI starting to think: three tightly spaced soft rounded digital pulses rising gently, "
                    "then a faint crystalline shimmer; light, quiet, focused; no keyboard, no arcade "
                    "beeps", 1.0),
        "done": ("satisfying task complete: three distinct bright crystalline notes ascending, the first "
                 "light, the second brighter, the last warm and complete, then a soft luminous resolution; "
                 "subtly joyful; no fanfare, no orchestra", 1.5),
        "attention": ("politely asking for a decision: two clear gently ascending crystalline chimes, then "
                      "a delicate suspended unresolved tone that fades; noticeable but never urgent; no "
                      "alarm, no phone ring", 1.2),
        "error": ("gentle failure: two soft rounded synth notes descending in pitch, lower and warmer, then "
                  "a muted digital resonance that quickly fades; not alarming; no siren, no buzzer, no "
                  "game-over", 1.0),
    },
}
# Per effect: longest length kept on the device, and its peak level (fraction of full scale).
# Loudest to quietest: attention > done > error > boot > working, so the sounds that need
# the user stand out and the frequent "working" stays in the background.
EFFECTS = {
    "boot": (2000, 0.40),
    "working": (800, 0.30),
    "done": (1500, 0.50),
    "attention": (1200, 0.55),
    "error": (800, 0.45),
}

FADE_IN_MS = 5      # avoids a click when a clip starts mid-waveform
FADE_MS = 80
SILENCE = 0.02       # of full scale; quieter edges are trimmed


def request_effect(api_key: str, prompt: str, seconds: float) -> array.array:
    if len(prompt) > MAX_PROMPT_CHARS:
        sys.exit(f"prompt is {len(prompt)} chars, ElevenLabs takes {MAX_PROMPT_CHARS}: {prompt[:60]}...")
    body = json.dumps({
        "text": prompt,
        "duration_seconds": seconds,
        "prompt_influence": 0.7,
    }).encode()
    req = urllib.request.Request(API_URL, data=body, method="POST",
                                 headers={"xi-api-key": api_key, "Content-Type": "application/json"})
    try:
        with urllib.request.urlopen(req, timeout=60) as resp:
            raw = resp.read()
    except urllib.error.HTTPError as err:
        sys.exit(f"ElevenLabs error {err.code}: {err.read().decode(errors='replace')[:300]}")
    pcm = array.array("h")
    pcm.frombytes(raw[: len(raw) // 2 * 2])
    if sys.byteorder != "little":
        pcm.byteswap()
    return pcm


def shape(pcm: array.array, name: str) -> array.array:
    """Trim silent edges, cap the length with short fades, and level the peak. Idempotent."""
    max_ms, target_peak = EFFECTS[name]
    threshold = int(SILENCE * 32767)
    loud = [i for i, s in enumerate(pcm) if abs(s) > threshold]
    if not loud:
        return array.array("h")
    start = max(0, loud[0] - SAMPLE_RATE * 5 // 1000)
    end = min(loud[-1] + 1, start + SAMPLE_RATE * max_ms // 1000)
    out = pcm[start:end]

    fade_in = min(len(out), SAMPLE_RATE * FADE_IN_MS // 1000)
    for i in range(fade_in):
        out[i] = int(out[i] * i / fade_in)
    fade = min(len(out), SAMPLE_RATE * FADE_MS // 1000)
    for i in range(fade):
        out[len(out) - fade + i] = int(out[len(out) - fade + i] * (fade - i) / fade)

    peak = max(abs(s) for s in out) or 1
    gain = target_peak * 32767 / peak
    return array.array("h", (max(-32768, min(32767, int(s * gain))) for s in out))


def write_wav(path: Path, pcm: array.array) -> None:
    with wave.open(str(path), "wb") as wav:
        wav.setnchannels(1)
        wav.setsampwidth(2)
        wav.setframerate(SAMPLE_RATE)
        wav.writeframes(pcm.tobytes())


def read_wav(path: Path) -> array.array:
    with wave.open(str(path), "rb") as wav:
        if (wav.getnchannels(), wav.getsampwidth(), wav.getframerate()) != (1, 2, SAMPLE_RATE):
            sys.exit(f"{path}: expected 16 kHz mono 16-bit")
        pcm = array.array("h")
        pcm.frombytes(wav.readframes(wav.getnframes()))
    return pcm


def generate(args: argparse.Namespace) -> None:
    api_key = os.environ.get("ELEVENLABS_API_KEY")
    if not api_key:
        sys.exit("ELEVENLABS_API_KEY is not set")
    style = STYLES[args.style]
    out_dir = CANDIDATES / args.style
    out_dir.mkdir(parents=True, exist_ok=True)
    for name in EFFECTS:
        if args.only and name not in args.only:
            continue
        prompt, seconds = style[name]
        for n in range(1, args.variants + 1):
            pcm = shape(request_effect(api_key, f"{prompt}; {style['suffix']}", seconds), name)
            path = out_dir / f"{name}-{n}.wav"
            write_wav(path, pcm)
            print(f"{path.relative_to(FIRMWARE)}  {len(pcm) * 1000 // SAMPLE_RATE} ms")


def embed(args: argparse.Namespace) -> None:
    picks = dict(pick.split("=", 1) for pick in args.picks)
    missing = set(EFFECTS) - set(picks)
    if missing or set(picks) - set(EFFECTS):
        sys.exit(f"pick exactly one variant for each of: {', '.join(EFFECTS)}")
    SOUNDS.mkdir(parents=True, exist_ok=True)
    for name, pick in picks.items():
        style, _, variant = pick.partition(":")
        write_wav(SOUNDS / f"{name}.wav", shape(read_wav(CANDIDATES / style / f"{name}-{variant}.wav"), name))

    lines = [
        "/* Generated by tools/sound_effects.py from the WAVs in main/assets/sounds/ (ElevenLabs). Do not edit. */",
        '#include "sound_effects.h"',
        "",
    ]
    for name in EFFECTS:
        pcm = read_wav(SOUNDS / f"{name}.wav")
        lines.append(f"static const int16_t s_{name}[{len(pcm)}] = {{")
        for i in range(0, len(pcm), 16):
            lines.append("    " + ", ".join(str(s) for s in pcm[i:i + 16]) + ",")
        lines.append("};")
        lines.append(f"const sound_effect_t SFX_{name.upper()} = {{s_{name}, {len(pcm)}}};")
        lines.append("")
    C_OUT.write_text("\n".join(lines))
    print(f"wrote {C_OUT.relative_to(FIRMWARE)}")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="command", required=True)
    gen = sub.add_parser("generate", help="generate candidate variants")
    gen.add_argument("--style", choices=list(STYLES), required=True)
    gen.add_argument("--variants", type=int, default=4)
    gen.add_argument("--only", nargs="*", choices=list(EFFECTS))
    gen.set_defaults(func=generate)
    emb = sub.add_parser("embed", help="embed picked variants into the firmware")
    emb.add_argument("picks", nargs="+", metavar="effect=style:variant")
    emb.set_defaults(func=embed)
    args = parser.parse_args()
    args.func(args)


if __name__ == "__main__":
    main()
