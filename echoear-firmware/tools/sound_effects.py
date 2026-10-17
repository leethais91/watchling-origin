#!/usr/bin/env python3
"""UI sound effects for the device, generated with the ElevenLabs Sound Effects API.

  ELEVENLABS_API_KEY=... python3 tools/sound_effects.py generate --style robot [--variants 3] [--only done]
      Writes candidates to tools/sfx-candidates/<style>/<effect>-<n>.wav (already trimmed
      and levelled, so what you hear is what the device plays). Open
      tools/sfx-candidates/index.html to compare and pick.

  python3 tools/sound_effects.py embed confirm=scifi:2 done=robot:1 attention=robot:3 error=scifi:1
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

API_URL = "https://api.elevenlabs.io/v1/sound-generation?output_format=pcm_16000"
SAMPLE_RATE = 16000  # device format: 16 kHz mono s16le

# Each style shares one suffix so its four sounds feel like a family.
STYLES = {
    "scifi": {
        "suffix": "futuristic sci-fi user interface sound, clean digital synth, holographic, "
                  "no music, no voice, no background noise",
        "confirm": ("two quick rising crystalline synth pings, an alert asking for approval", 1.0),
        "done": ("bright ascending synth chime with a short shimmering tail, task complete success", 1.2),
        "attention": ("soft gentle double blip, calm reminder notification", 0.8),
        "error": ("low descending digital buzz with a brief glitch, access denied", 1.0),
    },
    "robot": {
        "suffix": "cute small friendly robot sound effect, playful electronic chirps and bloops, "
                  "toy-like, no words, no music, no background noise",
        "confirm": ("curious questioning robot chirp rising at the end, like asking may I", 1.0),
        "done": ("happy cheerful robot trill and bloop, proud little celebration", 1.2),
        "attention": ("two soft polite robot beeps, gently calling for attention", 0.8),
        "error": ("sad disappointed robot bloop sliding downward, uh-oh", 1.0),
    },
}
EFFECTS = ("confirm", "done", "attention", "error")

MAX_MS = 1200        # speech follows the effect; keep the gap short
FADE_IN_MS = 5      # avoids a click when a clip starts mid-waveform
FADE_MS = 60
SILENCE = 0.02       # of full scale; quieter edges are trimmed
TARGET_PEAK = 0.5    # about -6 dBFS, roughly level with `say` speech on the device


def request_effect(api_key: str, prompt: str, seconds: float) -> array.array:
    body = json.dumps({
        "text": prompt,
        "duration_seconds": seconds,
        "prompt_influence": 0.6,
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


def shape(pcm: array.array) -> array.array:
    """Trim silent edges, cap the length with short fades, and level the peak. Idempotent."""
    threshold = int(SILENCE * 32767)
    loud = [i for i, s in enumerate(pcm) if abs(s) > threshold]
    if not loud:
        return array.array("h")
    start = max(0, loud[0] - SAMPLE_RATE * 5 // 1000)
    end = min(loud[-1] + 1, start + SAMPLE_RATE * MAX_MS // 1000)
    out = pcm[start:end]

    fade_in = min(len(out), SAMPLE_RATE * FADE_IN_MS // 1000)
    for i in range(fade_in):
        out[i] = int(out[i] * i / fade_in)
    fade = min(len(out), SAMPLE_RATE * FADE_MS // 1000)
    for i in range(fade):
        out[len(out) - fade + i] = int(out[len(out) - fade + i] * (fade - i) / fade)

    peak = max(abs(s) for s in out) or 1
    gain = TARGET_PEAK * 32767 / peak
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
            pcm = shape(request_effect(api_key, f"{prompt}, {style['suffix']}", seconds))
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
        write_wav(SOUNDS / f"{name}.wav", shape(read_wav(CANDIDATES / style / f"{name}-{variant}.wav")))

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
    gen.add_argument("--variants", type=int, default=3)
    gen.add_argument("--only", nargs="*", choices=list(EFFECTS))
    gen.set_defaults(func=generate)
    emb = sub.add_parser("embed", help="embed picked variants into the firmware")
    emb.add_argument("picks", nargs="+", metavar="effect=style:variant")
    emb.set_defaults(func=embed)
    args = parser.parse_args()
    args.func(args)


if __name__ == "__main__":
    main()
