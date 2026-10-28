/* SPDX-License-Identifier: GPL-3.0-only */
#include "bb909_assets.h"
#include "pack_constants.h"
#include <stdint.h>
static uint32_t u32(const unsigned char *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static uint32_t crc(const unsigned char *p, size_t n) {
    uint32_t value = UINT32_MAX;
    for (size_t i = 0; i < n; ++i) {
        value ^= p[i];
        for (unsigned b = 0; b < 8; ++b)
            value = (value >> 1) ^ (UINT32_C(0xEDB88320) & (0u - (value & 1u)));
    }
    return ~value;
}
int bb909_assets_parse(const void *image, size_t bytes, drum909_samples_t *out) {
    if (!image || !out || (uintptr_t)image % 4 || (uintptr_t)out % _Alignof(drum909_samples_t)
        || bytes != BB909_PACK_BYTES || bytes > 0x3C000u) return 0;
    uintptr_t a = (uintptr_t)image, b = (uintptr_t)out;
    if (a <= b ? b - a < bytes : a - b < sizeof(*out)) return 0;
    const unsigned char *p = image;
    if (u32(p) != UINT32_C(0x39304242) || u32(p + 4) != 2 || u32(p + 8) != bytes
        || u32(p + 12) != crc(p + 16, 80)) return 0;
    const uint32_t offsets[5] = {BB909_HH_OFFSET, BB909_CR_OFFSET, BB909_RD_OFFSET, BB909_TANH_OFFSET, BB909_POTS_OFFSET};
    const uint32_t frames[5] = {BB909_HH_FRAMES, BB909_CR_FRAMES, BB909_RD_FRAMES, BB909_TANH_FRAMES, BB909_POTS_FRAMES};
    const uint32_t hashes[5] = {BB909_HH_CRC, BB909_CR_CRC, BB909_RD_CRC, BB909_TANH_CRC, BB909_POTS_CRC};
    for (unsigned i = 0; i < 5; ++i) {
        const unsigned char *entry = p + 16 + i * 16;
        uint32_t off = offsets[i], n = frames[i] * (i < 3 ? 2u : 4u);
        if (u32(entry) != off || u32(entry + 4) != frames[i] || u32(entry + 8) != hashes[i]
            || u32(entry + 12) != 0 || off > bytes || n > bytes - off
            || crc(p + off, n) != hashes[i]) return 0;
    }
    for (unsigned i = 96; i < 256; ++i) if (p[i] != 0xFF) return 0;
    for (unsigned entry = 0; entry < 5; ++entry) {
        size_t end = offsets[entry] + frames[entry] * (entry < 3 ? 2u : 4u);
        size_t next = entry + 1 < 5 ? offsets[entry + 1] : bytes;
        for (size_t i = end; i < next; ++i) if (p[i] != 0xFF) return 0;
    }
    const drum909_samples_t result = {(const int16_t *)(p + offsets[0]),
        (const int16_t *)(p + offsets[1]), (const int16_t *)(p + offsets[2]), frames[0], frames[1], frames[2]};
    *out = result;
    return 1;
}
