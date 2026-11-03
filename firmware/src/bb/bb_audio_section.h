/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef BB_AUDIO_SECTION_H
#define BB_AUDIO_SECTION_H
#include "bb_audio_phrase.h"
enum { BB_SECTION_FREE, BB_SECTION_WRITING, BB_SECTION_QUEUED, BB_SECTION_PENDING };
enum { BB_SECTION_SLOT=4 };
enum { BB_SECTION_ERROR_NONE, BB_SECTION_ERROR_DATA, BB_SECTION_ERROR_ENGINE,
       BB_SECTION_ERROR_STALE, BB_SECTION_ERROR_LATE, BB_SECTION_ERROR_FAULT };
typedef struct {
    bb_phrase_index drum[2];
    bb_pattern_data dillo; /* absent Dillo resolves to an explicit all-rest part */
    uint32_t section, revision, project_revision;
    uint8_t at_tick; /* absolute target when1; rejects an already missed target */
    uint8_t keep; /* HOLD mask, bit0 A, bit1 B, bit2 Dillo: that part keeps playing untouched; SONG=0;
                   * refused with restart!=0 (a restart is a whole-transport Start) */
    uint32_t target_tick;
    uint8_t restart; /* single qualified section + existing Start side effects */
    uint32_t duration_ticks; /* 0=manual; otherwise exact bounded end after <=262144 ticks */
    uint16_t wait_ticks; /* relative to guarded submission snapshot, 0=next master tick */
} bb_section_bundle;
typedef struct {
    bb_pattern_status state;
    uint32_t section, revision, project_revision; /* requested identity */
    uint32_t playing_section, playing_revision, playing_project_revision;
    uint32_t applied_tick,end_tick,current_tick,transport_epoch;
    uint8_t phase, error,running,ended;
} bb_section_status;
enum { BB_SECTION_EDIT_IDLE,BB_SECTION_EDIT_QUEUED,BB_SECTION_EDIT_APPLIED,BB_SECTION_EDIT_REJECTED,BB_SECTION_EDIT_WRITING };
typedef struct {
    uint32_t ticket,section,revision,project_revision;
    uint32_t source_ticket,source_edits,source_generation;
    uint8_t target,voice,step,count,phase,error,mode,old_count;
    uint32_t edited_section,edited_revision,result_revision;
} bb_section_ratchet;
typedef struct {
    bb_section_status status;
    bb_section_ratchet ratchet; /* small acknowledgement snapshot */
    bb_phrase ratchet_expected; /* WRITING/QUEUED immutable payload, not status */
    uint8_t ratchet_restore_counts[sizeof(((bb_phrase *)0)->ratchets)];
    uint32_t ticket, due_tick, cancel_serial,next_duration,duration_ticks;
    uint8_t end_armed,restart,restart_guard,keep;
    uint32_t generation[2], epoch[2], synth_applied;
    uint8_t rich_slot[2], synth_slot;
    volatile uint8_t phase;
} bb_section_handoff;
struct bb_audio;
/* Main producer only. Complete input is immutable for the duration of call.
 * All copies/structural preflight occur outside IRQ-masked publication. One
 * section occupies both existing rich slots and one existing synth slot.
 * Caller retains originals and commits PLAYING only after matching APPLIED. */
int bb_audio_section_submit(struct bb_audio *,const bb_section_bundle *,uint32_t *ticket);
int bb_audio_section_status_read(struct bb_audio *,bb_section_status *);
int bb_audio_section_cancel_submit(struct bb_audio *,uint32_t ticket);
/* Main-only request with captured section/source identities; no source/model
 * pointers retained. Caller must reserve the corresponding immutable model edit
 * and keep all main-owned authoring frozen until terminal acknowledgement. */
int bb_audio_section_ratchet_submit(struct bb_audio *,const bb_section_status *,
                                    const bb_phrase_view *,unsigned target,
                                    unsigned voice,unsigned step,unsigned count,uint32_t *);
/* Exact SONG count-sidecar restoration. Before/after event words and grid
 * shape must match. Copies occur outside the producer IRQ guard; no pointer is
 * retained. Works for the current or another section, including a successor. */
int bb_audio_section_counts_restore_submit(struct bb_audio *,const bb_section_status *,
    const bb_phrase_view *,unsigned target,uint32_t edited_section,uint32_t edited_revision,
    uint32_t result_revision,const bb_phrase_index *expected,const bb_phrase_index *replacement,uint32_t *ticket);
/* Native-grid snapshot is a main-owned value, not a pointer into the owner. */
typedef struct {
    uint32_t source_ticket,epoch,packed;
    uint16_t hits;
    uint8_t target,voice,step,length;
} bb_native_ratchet_view;
int bb_audio_native_pattern_identity(struct bb_audio *,unsigned target,uint32_t out[2]);
int bb_audio_native_ratchet_view(struct bb_audio *,unsigned target,unsigned voice,
                                  unsigned step,bb_native_ratchet_view *);
int bb_audio_native_ratchet_submit(struct bb_audio *,const bb_native_ratchet_view *,
                                    unsigned count,uint32_t *ticket);
int bb_audio_section_ratchet_read(struct bb_audio *,bb_section_ratchet *);
int bb_audio_section_ratchet_ack(struct bb_audio *,uint32_t ticket);
void bb_audio_section_ratchet_command(struct bb_audio *,const bb_command *);
void bb_audio_section_ratchet_fault(struct bb_audio *);
/* Owner only: command dispatch, exact master-tick boundary and invalidation. */
void bb_audio_section_command(struct bb_audio *,const bb_command *,bb_live_command_fn,void *);
/* Returns1 only for a qualified owner STOP at the current boundary. */
int bb_audio_section_tick(struct bb_audio *);
void bb_audio_section_cancel_owned(struct bb_audio *,unsigned error);
void bb_audio_section_external_edit_owned(struct bb_audio *);
void bb_audio_section_observe_command(struct bb_audio *,const bb_command *);
#endif
