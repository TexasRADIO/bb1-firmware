/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef BB_TRANSPOSE_H
#define BB_TRANSPOSE_H
#include "bb_core.h"
#define BB_TRANSPOSE_LIMIT 48
/* Performance pitch only: never alter the stored phrase or articulation.
 * Clamp at MIDI boundaries instead of wrapping. Rests retain their bytes. */
static inline bb_note bb_note_transposed(bb_note n,int semitones) {
    int pitch;
    if(!n.gate)return n;
    if(semitones < -BB_TRANSPOSE_LIMIT)semitones=-BB_TRANSPOSE_LIMIT;
    if(semitones > BB_TRANSPOSE_LIMIT)semitones=BB_TRANSPOSE_LIMIT;
    pitch=(int)n.note+semitones;
    n.note=(uint8_t)(pitch<0?0:pitch>127?127:pitch);
    return n;
}
#endif
