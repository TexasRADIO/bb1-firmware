/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef BB_TRIO_H
#define BB_TRIO_H
#include "bb_core.h"
#define BB_TRIO_PARTS 8u
/* Compact factory program: one explicit musical template plus eight primary
 * drum/Dillo/Telegram phrases. Other drum layers come from the template.
 * Runtime/UI/clock fields in seed are never restored. Not a lossless Studio
 * decoder or permission to omit unsupported source expression/GreyHound. */
typedef struct {
    bb_drum_step drums[BB_STEPS];
    uint16_t notes[BB_SYNTHS][BB_STEPS]; /* pitch7, gate1, accent1, slide1 */
    uint16_t mode_mask,mode_value;
    uint8_t drum_length,synth_length[BB_SYNTHS],phrase;
} bb_trio_part;
typedef struct {
    bb_core seed;
    bb_trio_part part[BB_TRIO_PARTS];
    uint8_t primary,initial;
} bb_trio_program;
/* Invalid input leaves out unchanged; every latent rest flag is retained. */
int bb_trio_pack_note(bb_note,uint16_t *);
bb_note bb_trio_unpack_note(uint16_t);
/* Validated owner-only conversion. Caller supplies count readable packed
 * values and count writable notes in disjoint regions. No allocation, shared
 * state or validation bypass: bank validation precedes this operation. */
#define BB_TRIO_BULK_UNPACK 1
void bb_trio_unpack_notes(bb_note *,const uint16_t *,unsigned);

int bb_trio_valid(const bb_trio_program *);
uint32_t bb_trio_check(const bb_trio_program *);
#endif
