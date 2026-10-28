/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef BB_PROPOSAL_PREPARED_PARTS_H
#define BB_PROPOSAL_PREPARED_PARTS_H
#include "compact_scene.h"
#include "bb_parts.h"
enum { PP_IDLE, PP_PREPARING, PP_READY };
typedef struct {
    uint32_t serial, work_serial, checksum;
    uint8_t state, work_slot, result, expanded;
} pp_handoff;
typedef struct {
    compact_scene scenes[BB_PART_COUNT];
    uint8_t phrase_steps[BB_PART_COUNT];
    uint8_t active,queued,position,ready;
    uint32_t launches;
    uint8_t family[BB_PART_COUNT][BB_MACHINES],current_family[BB_MACHINES];
    bb_core prepared;
    pp_handoff handoff;
} prepared_parts;
/* Isolated feasibility model, not linked into firmware. One main producer,
 * one audio consumer. Caller holds the existing queue guard for request,
 * claim, finish and cancel metadata access. Commit runs on the exclusive
 * audio consumer and expand runs on main outside that guard. Never hold
 * the guard across validation, packing or publication on
 * target: that split still needs integration with the real audio owner.
 * The tests model serialized guard-boundary interleavings, not CPU memory
 * ordering. C atomics/volatile are not being presented as a target solution.
 * No other API may write an inactive scene during a claimed preparation. */
int pp_init(prepared_parts *,const bb_parts *);
/* Startup or fully suspended/exclusive ownership only, not audio callback
 * helpers. No temporary expanded bank is allocated. Initialization is the
 * existing eight-copy semantics; do not silently run it at boot if that
 * would create snapshots before the user requests them. */
int pp_init_core(prepared_parts *,const bb_core *,unsigned phrase_steps,
    const uint8_t family[BB_MACHINES]);
/* Full suspension includes every preparation producer and audio consumer.
 * Rejects any outstanding preparation lease. Stores the requested inactive,
 * nonqueued slot, exactly as the original API; not a flash save operation. */
int pp_store_suspended(prepared_parts *,unsigned slot,const bb_core *,unsigned phrase_steps);
/* Short metadata guard, same family-tag binding as the existing model. This
 * does NOT change a DSP engine or waive the independent family-switch gate.
 * A changed tag makes any prepared mismatching launch fail at commit. */
int pp_bind_families(prepared_parts *,const uint8_t family[BB_MACHINES]);
int pp_request(prepared_parts *,unsigned slot);
int pp_claim(prepared_parts *,uint32_t *ticket);
int pp_expand(prepared_parts *,uint32_t ticket,unsigned sample_rate);
int pp_finish(prepared_parts *,uint32_t ticket,int valid);
void pp_cancel(prepared_parts *);
/* Pattern replacement cancels launch without restarting phrase timing. */
void pp_cancel_pending(prepared_parts *);
int pp_commit(prepared_parts *,bb_core *);
int pp_before_tick(prepared_parts *,bb_core *);
/* Internal proposal helpers: caller owns disjoint validated objects. These
 * do not establish validity, ownership or an audio deadline by themselves. */
uint32_t pp_core_check(const bb_core *);
void pp_publish_music(bb_core *,const bb_core *);
#endif
