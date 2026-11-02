/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef BB_RATCHET_H
#define BB_RATCHET_H
#include <stdint.h>
/* Zero storage is x1. Every voice owns exactly two bits; no heap or side table. */
#define BB_RATCHET_VOICES 16u
static inline unsigned bb_ratchet_bits_get(uint32_t packed,unsigned voice){
    return voice<BB_RATCHET_VOICES?1u+((packed>>(2u*voice))&3u):0u;
}
static inline int bb_ratchet_set(uint32_t *packed,unsigned voice,unsigned count){
    if(!packed||voice>=BB_RATCHET_VOICES||count<1u||count>4u)return 0;
    unsigned shift=2u*voice;uint32_t mask=UINT32_C(3)<<shift;
    *packed=(*packed&~mask)|((uint32_t)(count-1u)<<shift);return 1;
}
#endif
