/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef BB_NOTE_EXPRESSION_H
#define BB_NOTE_EXPRESSION_H
#include <stdint.h>
/* Canonical sidecar, not a reinterpretation of legacy bb_note bytes.
 * Missing fields remain missing: word0 means existing articulation exactly.
 * Gate is an exact integer percentage, not a rounded approximation. */
typedef uint16_t bb_note_expression;
typedef struct {uint8_t has_velocity,velocity,has_gate,gate_percent;} bb_note_expression_view;
int bb_note_expression_make(unsigned,unsigned,unsigned,unsigned,bb_note_expression *);
int bb_note_expression_read(bb_note_expression,bb_note_expression_view *);
/* Exact little-endian IEEE754 binary64 source spelling for percent/100.
 * Rejects intermediate fractions rather than silently quantizing them. */
int bb_note_expression_gate_bits_match(unsigned,const uint8_t *);
#endif
