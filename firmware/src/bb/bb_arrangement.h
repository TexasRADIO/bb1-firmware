/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef BB_ARRANGEMENT_H
#define BB_ARRANGEMENT_H
#include <stdint.h>
/* Main-loop composition model. No renderer, heap, flash or transport access.
 * References resolve to complete rich/working objects, never 808 projections. */
enum { BB_ARR_SECTIONS=32, BB_ARR_ROWS=64, BB_ARR_HISTORY=16,
       BB_ARR_WORK_LIMIT=16, BB_ARR_NAME=24, BB_ARR_REPEAT_MAX=64, BB_ARR_DURATION_MAX=4096 };
enum { BB_ARR_OK, BB_ARR_INVALID, BB_ARR_SECTION_FULL, BB_ARR_ROW_FULL,
       BB_ARR_HISTORY_FULL, BB_ARR_REFERENCED, BB_ARR_NOT_FOUND,
       BB_ARR_EXHAUSTED, BB_ARR_NO_UNDO, BB_ARR_NO_REDO };
enum { BB_ARR_CURATED=1, BB_ARR_WORKING=2 };
typedef struct { uint32_t id, revision; uint8_t source; } bb_arr_part;
typedef struct {
    uint32_t id, revision;
    char name[BB_ARR_NAME];
    bb_arr_part part[3]; /* A, B, optional Dillo; all-zero means absent Dillo. */
    uint16_t duration; /* declared 1/16-note units, not source event positions */
} bb_arr_section;
typedef struct { uint32_t section; uint8_t repeats; } bb_arr_row;
enum { BB_ARR_SECTION_EDIT=1, BB_ARR_INSERT, BB_ARR_REMOVE, BB_ARR_REPLACE, BB_ARR_MOVE, BB_ARR_APPEND };
typedef struct {
    bb_arr_section before, after;
    bb_arr_row old_row, new_row;
    uint8_t kind, index, destination;
} bb_arr_change;
typedef struct {
    bb_arr_section sections[BB_ARR_SECTIONS];
    bb_arr_row rows[BB_ARR_ROWS];
    bb_arr_change history[BB_ARR_HISTORY];
    uint32_t revision, saved_revision, next_id;
    uint8_t work_refs[BB_ARR_WORK_LIMIT]; /* sections + both history halves; runtime only */
    uint8_t row_count, cursor, history_count;
} bb_arrangement;
void bb_arr_init(bb_arrangement *a);
int bb_arr_validate(const bb_arrangement *a);
const bb_arr_section *bb_arr_find(const bb_arrangement *a,uint32_t id);
int bb_arr_dirty(const bb_arrangement *a);
/* A save completion is acknowledged only for exactly the captured revision. */
int bb_arr_saved(bb_arrangement *a,uint32_t captured_revision);
/* Caller-owned edit input may alias this model; it is copied before API mutation.
 * Do not directly edit a published model: construct edits in a separate copy.
 * Cold direct construction must rebuild derived counters before publication.
 * New section: id=revision=0. Existing: both must match the current object.
 * Removing a referenced section is refused. New commits age the oldest history
 * entry at the bounded depth; a new branch releases Redo ownership.
 * Unchanged row/section replacements return OK without revising the model,
 * consuming history or discarding Redo, even when history is already full. */
int bb_arr_section_put(bb_arrangement *a,const bb_arr_section *section,uint32_t *id);
/* One reversible action: create a complete named section and append its row. */
int bb_arr_section_append(bb_arrangement *,const bb_arr_section *,unsigned repeats);
int bb_arr_section_remove(bb_arrangement *a,uint32_t id);
int bb_arr_row_insert(bb_arrangement *a,unsigned position,bb_arr_row row);
int bb_arr_row_remove(bb_arrangement *a,unsigned position);
int bb_arr_row_replace(bb_arrangement *a,unsigned position,bb_arr_row row);
int bb_arr_row_move(bb_arrangement *a,unsigned from,unsigned to);
/* Explicit user-confirmed RAM history discard; never implies a project save. */
int bb_arr_history_discard(bb_arrangement *a,uint32_t captured_revision);
int bb_arr_undo(bb_arrangement *a);
int bb_arr_redo(bb_arrangement *a);
/* Cold exclusive construction/import only: reconstruct derived owner counters. */
int bb_arr_work_rebuild(bb_arrangement *);
/* Exact owner counts after the next successful commit drops Redo/ages history.
 * No model mutation. Output must not overlap the model. */
int bb_arr_next_work_refs(const bb_arrangement *,uint8_t [BB_ARR_WORK_LIMIT]);
#endif
