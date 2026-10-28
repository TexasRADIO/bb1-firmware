/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef BB_PAGED_CONTAINER_H
#define BB_PAGED_CONTAINER_H
#include <stddef.h>
#include <stdint.h>
#define BB_PC_PAGE 768u
#define BB_PC_MAX_BYTES 1048576u
/* Exclusive control/startup owner, not IRQ/thread-safe. Copies of views may
 * share this scratch sequentially. No globals/heap; text never points here. */
typedef struct {
    const uint8_t *source;size_t length;uint32_t crc,tag[2],hits,misses;
    uint8_t valid[2],next;
    uint8_t page[2][BB_PC_PAGE];
} bb_pc_cache;
typedef struct {
    const uint8_t *bytes;size_t length;bb_pc_cache *cache;
    uint32_t nodes,root,metadata_bytes,sparse,dictionary,blocks,block_count,text,text_bytes,payload,crc,stamp;
    uint16_t shapes;
} bb_pc_view;
/* Container framing/CRC and every compressed page are validated, NOT the
 * complete catalogue node schema. This is not bb_catalogue_open().
 * Failure preserves out; private cache contents may change. Earlier views
 * remain usable with that cache if their immutable sources still exist.
 * Source/out/cache must be mapped, disjoint, and live for all later access.
 */
int bb_pc_open_container(bb_pc_view *out,const uint8_t *,size_t,bb_pc_cache *);
/* Bounds/layout refusals precede writes. A decode failure may partially
 * write a caller-private output. Never use live musical state as output. */
int bb_pc_read_metadata(const bb_pc_view *,uint32_t offset,void *,size_t);
/* Public text pointers are into immutable source bytes, valid until source
 * lifetime ends; cache eviction does not affect them. No NUL is implied. */
int bb_pc_text_bytes(const bb_pc_view *,uint32_t offset,size_t,const uint8_t **);
#endif
