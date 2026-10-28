/* SPDX-License-Identifier: GPL-3.0-only */
#include "bb_asset_transfer.h"
#include "bb/bb_sample_directory.h"
#include <string.h>
static uint32_t update(uint32_t crc, const void *data, size_t n) {
    const uint8_t *p = data;
    while (n--) {
        crc ^= *p++;
        for (unsigned i = 0; i < 8; ++i)
            crc = (crc >> 1) ^ (UINT32_C(0xEDB88320) & (0u - (crc & 1u)));
    }
    return crc;
}
uint32_t bb_asset_crc32(const void *data, size_t n) { return ~update(UINT32_MAX, data, n); }
static int fail(bb_asset_transfer *t) { t->state = BB_ASSET_FAILED; return 0; }
int bb_asset_begin(bb_asset_transfer *t, const bb_asset_io *io, unsigned mode,
                   uint32_t bytes, uint32_t expected, uint32_t header_crc) {
    if (!t || !io || !io->read || !io->erase || !io->program || mode > BB_ASSET_SAMPLE_INSTALL) return 0;
    if(mode==BB_ASSET_SAMPLE_INSTALL){if(bytes<800||bytes>BB_ASSET_REGION_BYTES)return 0;}
    else if(bytes!=(mode==BB_ASSET_INSTALL?BB_ASSET_PACK_BYTES:BB_ASSET_REGION_BYTES))return 0;
    if (t->state == BB_ASSET_RECEIVING)
        return t->mode == mode && t->bytes == bytes && t->expected_crc == expected && t->header_crc == header_crc;
    bb_asset_io hooks = *io;
    memset(t, 0, sizeof(*t)); t->io = hooks; t->state = BB_ASSET_RECEIVING;
    t->mode = mode; t->bytes = bytes; t->expected_crc = expected; t->header_crc = header_crc;
    t->crc = UINT32_MAX; return 1;
}
int bb_asset_write(bb_asset_transfer *t, uint32_t off, const void *data, unsigned n, uint32_t crc) {
    if (!t || t->state != BB_ASSET_RECEIVING || !data || !n || n > BB_ASSET_CHUNK_BYTES
        || off % BB_ASSET_CHUNK_BYTES || off > t->bytes || n > t->bytes - off
        || (n != BB_ASSET_CHUNK_BYTES && off + n != t->bytes)
        || bb_asset_crc32(data, n) != crc) return 0;
    uintptr_t a = (uintptr_t)t, b = (uintptr_t)data;
    if (a <= b ? b - a < sizeof(*t) : a - b < n) return 0;
    uint8_t check[BB_ASSET_CHUNK_BYTES];
    if (off != t->next) {
        /* A lost acknowledgement can replay exactly the immediately prior
         * chunk; it cannot rewind or advance the write cursor. */
        if (off + n != t->next) return 0;
        if (off < BB_ASSET_HEADER_BYTES) return !memcmp(t->header + off, data, n);
        if (t->io.read(t->io.context, BB_ASSET_BASE + off, check, n)) return fail(t);
        return !memcmp(check, data, n);
    }
    if (off < BB_ASSET_HEADER_BYTES) {
        memcpy(t->header + off, data, n);
        if (off + n == BB_ASSET_HEADER_BYTES &&
            (bb_asset_crc32(t->header, BB_ASSET_HEADER_BYTES) != t->header_crc ||
             (t->mode==BB_ASSET_SAMPLE_INSTALL&&!bb_sample_directory_valid(t->header,t->bytes))))
            return fail(t); /* No flash is erased before complete header validation. */
    } else {
        if (off == BB_ASSET_HEADER_BYTES || off % 4096u == 0) {
            uint32_t sector = BB_ASSET_BASE + (off & ~4095u);
            if (t->io.erase(t->io.context, sector)) return fail(t);
        }
        if (t->io.program(t->io.context, BB_ASSET_BASE + off, data, n)
            || t->io.read(t->io.context, BB_ASSET_BASE + off, check, n)
            || memcmp(check, data, n)) return fail(t);
    }
    t->crc = update(t->crc, data, n); t->next += n; return 1;
}
int bb_asset_commit(bb_asset_transfer *t) {
    if (t && t->state == BB_ASSET_COMMITTED) return 1;
    if (!t || t->state != BB_ASSET_RECEIVING || t->next != t->bytes) return 0;
    if (~t->crc != t->expected_crc) return fail(t);
    uint8_t check[BB_ASSET_CHUNK_BYTES];
    uint32_t crc = update(UINT32_MAX, t->header, BB_ASSET_HEADER_BYTES);
    for (uint32_t off = BB_ASSET_HEADER_BYTES; off < t->bytes;) {
        unsigned n = t->bytes - off > sizeof check ? sizeof check : t->bytes - off;
        if (t->io.read(t->io.context, BB_ASSET_BASE + off, check, n)) return fail(t);
        crc = update(crc, check, n); off += n;
    }
    if (~crc != t->expected_crc) return fail(t);
    /* Publication is last: interruption earlier leaves an erased header. */
    for (unsigned off = 0; off < BB_ASSET_HEADER_BYTES; off += sizeof check)
        if (t->io.program(t->io.context, BB_ASSET_BASE + off, t->header + off, sizeof check)
            || t->io.read(t->io.context, BB_ASSET_BASE + off, check, sizeof check)
            || memcmp(check, t->header + off, sizeof check)) return fail(t);
    t->state = BB_ASSET_COMMITTED; return 1;
}
void bb_asset_abort(bb_asset_transfer *t) {
    if (t) t->state = BB_ASSET_IDLE; /* Does not resume audio or alter flash. */
}
