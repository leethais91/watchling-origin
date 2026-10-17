/** Minimal RIFF/WAVE reader for the PCM that TTS engines write. */

export interface PcmFormat {
  sampleRate: number;
  channels: number;
  bitsPerSample: number;
}

/** Returns the raw sample bytes of a PCM WAV file; throws if the format differs from `expected`. */
export function wavPcmData(wav: Buffer, expected: PcmFormat): Buffer {
  if (wav.toString("ascii", 0, 4) !== "RIFF" || wav.toString("ascii", 8, 12) !== "WAVE") {
    throw new Error("not a WAVE file");
  }
  let format: PcmFormat | null = null;
  // Chunks follow the 12-byte header; engines may add JUNK/LIST chunks before "fmt ".
  for (let offset = 12; offset + 8 <= wav.length; ) {
    const id = wav.toString("ascii", offset, offset + 4);
    const size = wav.readUInt32LE(offset + 4);
    const body = offset + 8;
    if (id === "fmt ") {
      if (wav.readUInt16LE(body) !== 1) throw new Error("WAVE is not integer PCM");
      format = {
        channels: wav.readUInt16LE(body + 2),
        sampleRate: wav.readUInt32LE(body + 4),
        bitsPerSample: wav.readUInt16LE(body + 14),
      };
    } else if (id === "data") {
      if (!format) throw new Error("WAVE data before fmt chunk");
      if (
        format.sampleRate !== expected.sampleRate ||
        format.channels !== expected.channels ||
        format.bitsPerSample !== expected.bitsPerSample
      ) {
        throw new Error(`unexpected WAVE format ${JSON.stringify(format)}`);
      }
      return wav.subarray(body, Math.min(body + size, wav.length));
    }
    offset = body + size + (size & 1); // chunks are word-aligned
  }
  throw new Error("WAVE has no data chunk");
}
