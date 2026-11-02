/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef BB_ARR_CONTROLLER_H
#define BB_ARR_CONTROLLER_H
#include "bb_arr_sources.h"
enum { BB_ARR_CTL_OK,BB_ARR_CTL_INVALID,BB_ARR_CTL_BUSY,BB_ARR_CTL_SOURCE,
       BB_ARR_CTL_LATE,BB_ARR_CTL_OWNER,BB_ARR_CTL_CHANGED,BB_ARR_CTL_REJECTED,BB_ARR_CTL_UNCHANGED };
#define BB_ARR_ROW_NONE 255u
typedef struct {
    bb_arrangement *model;
    bb_arr_sources *sources;
    bb_section_bundle preparation; /* main-loop staging, never on callback stack */
    bb_section_status view;
    uint32_t ticket,prepared_revision,accepted_revision,seen_applied;
    uint32_t target_tick;
    uint32_t edit_ticket,edit_section,edit_revision,edit_source_revision;
    uint8_t edit_target,edit_voice,edit_step,edit_count;
    uint8_t edit_undo,edit_cursor,edit_history_count,edit_result;
    uint32_t edit_completed,edit_commit_revision;
    uint8_t selected,playing_row,requested_row,prepared_row;
    uint8_t armed,loop,error,source_error,prepared,cancel_wait,request_cue,playing_cue,restart_wait;
    uint8_t cue_row,return_row,cue_return;
} bb_arr_controller;
int bb_arr_controller_init(bb_arr_controller *,bb_arrangement *,bb_arr_sources *);
int bb_arr_controller_start(bb_arr_controller *,struct bb_audio *);
int bb_arr_controller_load(bb_arr_controller *,struct bb_audio *,unsigned row);
int bb_arr_controller_set_loop(bb_arr_controller *,struct bb_audio *,int on);
int bb_arr_controller_stop(bb_arr_controller *,struct bb_audio *);
int bb_arr_controller_cue(bb_arr_controller *,struct bb_audio *,unsigned row);
/* Main-loop-only bounded poll. No source decoding happens in an IRQ.
 * Playback/next labels advance only on matched complete-owner acknowledgements. */
int bb_arr_controller_poll(bb_arr_controller *,struct bb_audio *);
/* Live SONG count edit. All main-owned authoring must remain frozen while
 * edit_ticket is nonzero; poll commits the immutable model fork only after
 * matching owner acknowledgement. No view pointer is retained. */
int bb_arr_controller_ratchet(bb_arr_controller *,struct bb_audio *,
                               const bb_phrase_view *,unsigned target,
                               unsigned voice,unsigned step,unsigned count);
/* Backend for a caller's eight-deep STEPS-journal token, not an invitation
 * to expose the deeper arrangement history as STEPS Undo. Only an exact
 * count-only top section edit is admitted. Main authoring stays frozen. */
int bb_arr_controller_ratchet_undo(bb_arr_controller *,struct bb_audio *,
    const bb_phrase_view *,unsigned target,uint32_t section,uint32_t commit_revision);
/* An authoring edit is admitted only while stopped and all commands settled.
 * Live cueing is separate and never rewrites the saved arrangement. */
int bb_arr_controller_can_edit(bb_arr_controller *,struct bb_audio *);
#endif
