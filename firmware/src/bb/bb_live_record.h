/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef BB_LIVE_RECORD_H
#define BB_LIVE_RECORD_H
#include "bb_phrase.h"
/* Main-owned, bounded pass transaction. No audio mutation, allocation or flash.
 * JIANT-inspired overdub interaction; BB1's existing phrase is the authority.
 * Current format supports grid positions and accent classes, not free timing
 * or arbitrary per-hit velocities. Never advertise those as captured. */
enum { BB_REC_OK, BB_REC_INVALID, BB_REC_BUSY, BB_REC_EARLY, BB_REC_FULL,
       BB_REC_PROTECTED, BB_REC_DUPLICATE, BB_REC_CONFLICT, BB_REC_NO_PASS, BB_REC_WORK_FULL, BB_REC_HISTORY_FULL,
       BB_REC_SOURCE, BB_REC_EXHAUSTED, BB_REC_UNCHANGED };
enum { BB_REC_IDLE, BB_REC_ARMED, BB_REC_RECORDING, BB_REC_REVIEW };
typedef struct {
    bb_phrase before, take;
    uint64_t start_frame, protected_steps;
    uint32_t step_frames, revision, step_divisor, grid_phase, odd_step_frames;
    uint8_t state, changed, start_cell, start_odd;
} bb_live_record;
/* Caller zero-initializes once. Begin only after the caller resolved/copies an immutable source. start_frame
 * is the end of count-in on the audio timeline, not a UI redraw timestamp. */
int bb_live_record_begin(bb_live_record *,const bb_phrase *,uint32_t revision,
                         uint64_t protected_steps,uint64_t start_frame,uint32_t step_frames);
/* Exact rational clock: numerator/divisor frames per step. grid_phase is
 * accumulated clock units at origin_frame, strictly below numerator;
 * start_cell is the source cell at that frame. This supports an engine-phase
 * snapshot without rounding fractional-sample step durations. Straight grid;
 * swing/count-in and UI policy still require their own integration. */
int bb_live_record_begin_phase(bb_live_record *,const bb_phrase *,uint32_t revision,
                              uint64_t protected_steps,uint64_t origin_frame,
                              uint32_t numerator,uint32_t divisor,
                              uint32_t grid_phase,unsigned start_cell);
/* Nearest-grid overdub; exact half rounds forward, including loop wrap.
 * Same voice/cell is retained with all original modifiers, never overwritten.
 * Capacity/refusal leaves the take byte-identical. */
/* Alternating rational grid, with phase/parity supplied by the audio owner.
 * even+odd numerators must fit uint32; old straight API retains its full range.
 * This is transient take state, not a scene/project format. */
int bb_live_record_begin_pair_phase(bb_live_record *,const bb_phrase *,uint32_t revision,
                         uint64_t protected_steps,uint64_t origin_frame,
                         uint32_t even_numerator,uint32_t odd_numerator,uint32_t divisor,
                         uint32_t grid_phase,unsigned start_cell,unsigned start_odd);
/* Whole-frame wait from captured origin to the boundary n cells ahead (1..64).
 * Used for count-in on the same grid as quantisation; no UI/wall-clock timing. */
int bb_live_record_wait_steps(const bb_live_record *,unsigned n,uint64_t *wait);
int bb_live_record_hit(bb_live_record *,uint64_t frame,unsigned voice,unsigned accented);
int bb_live_record_finish(bb_live_record *);
/* Review output only; caller still must prepare/preflight and atomically
 * publish a copied owner. Revision conflict leaves output untouched. */
int bb_live_record_copy(const bb_live_record *,uint32_t revision,bb_phrase *);
/* Pass Undo/discard restores the entire original and ends this transaction. */
int bb_live_record_undo(bb_live_record *);
#endif
