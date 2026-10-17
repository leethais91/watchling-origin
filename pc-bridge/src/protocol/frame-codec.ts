/**
 * Wire framing shared with the firmware (see docs/device-protocol.md):
 *
 *   0x00 | COBS( type:u8 | payload | crc16:u16 big-endian ) | 0x00
 *
 * The leading delimiter separates frames from ESP log text that shares the
 * USB-Serial-JTAG port; log text never contains 0x00 and fails the CRC check.
 */

export const FrameType = {
  Json: 0x01,
} as const;

/** Upper bound for a decoded frame; anything longer is treated as noise. */
export const MAX_FRAME_BYTES = 4096;

/** CRC-16/CCITT-FALSE (poly 0x1021, init 0xFFFF). Check value for "123456789" is 0x29B1. */
export function crc16(data: Uint8Array): number {
  let crc = 0xffff;
  for (const byte of data) {
    crc ^= byte << 8;
    for (let bit = 0; bit < 8; bit++) {
      crc = crc & 0x8000 ? ((crc << 1) ^ 0x1021) & 0xffff : (crc << 1) & 0xffff;
    }
  }
  return crc;
}

export function cobsEncode(input: Uint8Array): Uint8Array {
  const out = new Uint8Array(input.length + Math.ceil(input.length / 254) + 1);
  let codeIndex = 0;
  let outIndex = 1;
  let code = 1;
  for (const byte of input) {
    if (byte === 0) {
      out[codeIndex] = code;
      codeIndex = outIndex++;
      code = 1;
      continue;
    }
    out[outIndex++] = byte;
    code++;
    if (code === 0xff) {
      out[codeIndex] = code;
      codeIndex = outIndex++;
      code = 1;
    }
  }
  out[codeIndex] = code;
  return out.subarray(0, outIndex);
}

/** Returns null when the input is not valid COBS. */
export function cobsDecode(input: Uint8Array): Uint8Array | null {
  const out = new Uint8Array(input.length);
  let inIndex = 0;
  let outIndex = 0;
  while (inIndex < input.length) {
    const code = input[inIndex++];
    if (code === 0 || inIndex + code - 1 > input.length) return null;
    for (let i = 1; i < code; i++) out[outIndex++] = input[inIndex++];
    if (code !== 0xff && inIndex < input.length) out[outIndex++] = 0;
  }
  return out.subarray(0, outIndex);
}

export function encodeFrame(type: number, payload: Uint8Array): Uint8Array {
  const body = new Uint8Array(payload.length + 3);
  body[0] = type;
  body.set(payload, 1);
  const crc = crc16(body.subarray(0, payload.length + 1));
  body[payload.length + 1] = crc >> 8;
  body[payload.length + 2] = crc & 0xff;
  const encoded = cobsEncode(body);
  const frame = new Uint8Array(encoded.length + 2);
  frame.set(encoded, 1);
  return frame;
}

export type DecodedChunk =
  | { kind: "frame"; type: number; payload: Uint8Array }
  | { kind: "text"; text: string };

/** Splits the incoming byte stream into valid frames and interleaved log text. */
export class FrameDecoder {
  private pending: Buffer = Buffer.alloc(0);

  push(data: Uint8Array): DecodedChunk[] {
    this.pending = Buffer.concat([this.pending, data]);
    const chunks: DecodedChunk[] = [];
    let delimiter: number;
    while ((delimiter = this.pending.indexOf(0)) !== -1) {
      const raw = this.pending.subarray(0, delimiter);
      this.pending = this.pending.subarray(delimiter + 1);
      if (raw.length > 0) chunks.push(decodeChunk(raw));
    }
    // A log stream with no frames never hits a delimiter; flush it as text.
    if (this.pending.length > MAX_FRAME_BYTES) {
      chunks.push({ kind: "text", text: this.pending.toString("utf8") });
      this.pending = Buffer.alloc(0);
    }
    return chunks;
  }
}

function decodeChunk(raw: Buffer): DecodedChunk {
  const body = raw.length <= MAX_FRAME_BYTES ? cobsDecode(raw) : null;
  if (body && body.length >= 3) {
    const crc = (body[body.length - 2] << 8) | body[body.length - 1];
    if (crc16(body.subarray(0, body.length - 2)) === crc) {
      return { kind: "frame", type: body[0], payload: body.subarray(1, body.length - 2) };
    }
  }
  return { kind: "text", text: raw.toString("utf8") };
}
