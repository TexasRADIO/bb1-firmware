/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once
#include <stddef.h>
#include <stdint.h>
#define BB_ASSET_BASE 0xA0000u
#define BB_ASSET_REGION_BYTES 0x3C000u
#define BB_ASSET_PACK_BYTES 235188u
#define BB_ASSET_HEADER_BYTES 256u
#define BB_ASSET_CHUNK_BYTES 32u
enum { BB_ASSET_IDLE, BB_ASSET_RECEIVING, BB_ASSET_FAILED, BB_ASSET_COMMITTED };
enum { BB_ASSET_INSTALL, BB_ASSET_RESTORE, BB_ASSET_SAMPLE_INSTALL };
typedef struct {
    /* Hooks are firmware-owned main-loop operations; never USB pointers. */
    void *context;
    int (*read)(void *, uint32_t, void *, unsigned);
    int (*erase)(void *, uint32_t);
    int (*program)(void *, uint32_t, const void *, unsigned);
} bb_asset_io;
typedef struct {
    bb_asset_io io;
    uint32_t state, mode, bytes, expected_crc, header_crc, next, crc;
    uint8_t header[BB_ASSET_HEADER_BYTES];
} bb_asset_transfer;
uint32_t bb_asset_crc32(const void *, size_t);
/* Zero-initialize the transfer object before first use.
 * Caller must first hold audio/CPU1 and verify unit/backup/approved payload.
 * This module never starts playback, pauses audio or selects a kit.
 * Install accepts exactly the pinned pack size; restore exactly the backed-up
 * region. Sample install accepts a bounded BBSK length and validates its
 * directory before first erase. Caller must validate the full pack beforehand;
 * boot revalidates its audio contents. No arbitrary flash address is accepted.
 */
int bb_asset_begin(bb_asset_transfer *, const bb_asset_io *, unsigned mode,
                   uint32_t bytes, uint32_t expected_crc, uint32_t header_crc);
int bb_asset_write(bb_asset_transfer *, uint32_t offset, const void *, unsigned bytes, uint32_t chunk_crc);
int bb_asset_commit(bb_asset_transfer *);
void bb_asset_abort(bb_asset_transfer *);
