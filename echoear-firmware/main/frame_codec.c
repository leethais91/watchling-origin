#include "frame_codec.h"

uint16_t frame_crc16(const uint8_t *data, size_t len)
{
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= (uint16_t)data[i] << 8;
        for (int bit = 0; bit < 8; bit++) {
            crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
        }
    }
    return crc;
}

size_t frame_cobs_encode(const uint8_t *in, size_t len, uint8_t *out)
{
    size_t code_index = 0;
    size_t out_index = 1;
    uint8_t code = 1;
    for (size_t i = 0; i < len; i++) {
        if (in[i] == 0) {
            out[code_index] = code;
            code_index = out_index++;
            code = 1;
            continue;
        }
        out[out_index++] = in[i];
        if (++code == 0xFF) {
            out[code_index] = code;
            code_index = out_index++;
            code = 1;
        }
    }
    out[code_index] = code;
    return out_index;
}

size_t frame_cobs_decode(const uint8_t *in, size_t len, uint8_t *out)
{
    size_t in_index = 0;
    size_t out_index = 0;
    while (in_index < len) {
        uint8_t code = in[in_index++];
        if (code == 0 || in_index + code - 1 > len) {
            return 0;
        }
        for (uint8_t i = 1; i < code; i++) {
            out[out_index++] = in[in_index++];
        }
        if (code != 0xFF && in_index < len) {
            out[out_index++] = 0;
        }
    }
    return out_index;
}
