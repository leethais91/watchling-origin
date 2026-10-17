/*
 * Wire framing shared with pc-bridge (see docs/device-protocol.md):
 *
 *   0x00 | COBS( type:u8 | payload | crc16:u16 big-endian ) | 0x00
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

#define FRAME_TYPE_JSON  0x01

/* Worst-case COBS output size for `len` input bytes. */
#define FRAME_COBS_MAX_ENCODED(len) ((len) + (len) / 254 + 1)

/* CRC-16/CCITT-FALSE (poly 0x1021, init 0xFFFF). */
uint16_t frame_crc16(const uint8_t *data, size_t len);

/* Encodes `len` bytes into `out` (FRAME_COBS_MAX_ENCODED(len) bytes); returns encoded length. */
size_t frame_cobs_encode(const uint8_t *in, size_t len, uint8_t *out);

/* Decodes into `out` (at least `len` bytes); returns decoded length, or 0 if `in` is not valid COBS. */
size_t frame_cobs_decode(const uint8_t *in, size_t len, uint8_t *out);
