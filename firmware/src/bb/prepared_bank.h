/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef BB_PROPOSAL_PREPARED_BANK_H
#define BB_PROPOSAL_PREPARED_BANK_H
#include "prepared_parts.h"
enum {
    PB_IDLE, PB_PREPARING, PB_READY, PB_REFILL, PB_REFILL_WRITING,
    PB_FAILED, PB_SEED, PB_SEED_WRITING
};
typedef struct {
    bb_trio_program *source;
    uint32_t ticket,source_check;
    uint16_t base_modes;
    uint8_t state,slot,valid_mask,result,expanded,cancel;
} pb_handoff;
typedef struct { prepared_parts parts; pb_handoff bank; } prepared_bank;
/* ISOLATED SERIALIZED OWNERSHIP MODEL, NOT FIRMWARE INTEGRATION.
 * One main producer, one audio owner. Metadata begin/publish/refill-claim/
 * refill-finish/cancel/request operations require the existing short guard.
 * Initial expansion and refill writing run on main OUTSIDE that guard.
 * Commit runs on the exclusive audio owner at the existing bank boundary.
 * Caller supplies an EXCLUSIVELY OWNED WRITABLE factory staging payload,
 * never a catalogue constant or saved original. Initial expansion rewrites
 * its seed as the first playable section; original primary mode bits are
 * retained and covered by the same checksum. All other per-section fields
 * are explicit, so later refill reconstructs the original sections exactly.
 * Caller pins that payload from begin until source becomes NULL. No other
 * write, replacement, deallocation or source alias with live core is allowed
 * during that lease; after initial expansion it is immutable. Each claim
 * permits one expansion attempt; a failed attempt must finish/release.
 * Corruption checks do not replace
 * ownership. Tests model guard boundaries, not actual CPU synchronization.
 * During refill, audio uses live core only; all stored-scene reading/writing,
 * saving and variant preparation must wait for PB_IDLE. Part requests may
 * queue, STOP may cancel them; no partial bank is exposed as ready.
 * Adapters must use pb_scenes_ready, NOT parts.ready, for whole-bank status.
 * That readiness is not permission to bypass the normal ownership guard.
 * PB_FAILED quarantines an unexpectedly corrupt pinned source after commit.
 * It keeps the live section intact, but requires an explicit recovery design
 * before integration. Never silently release or use unfilled scenes. */
int pb_init(prepared_bank *,const bb_parts *);
/* Startup/full-reset constructors: every reader/producer must be suspended.
 * Empty does not create snapshots or disable the existing family selector.
 * Core mirrors the existing explicit first-snapshot operation, but is only
 * an exclusive helper, NOT the eventual live lazy-initialization adapter. */
void pb_init_empty(prepared_bank *);
int pb_init_core(prepared_bank *,const bb_core *,unsigned,const uint8_t [BB_MACHINES]);
/* First manual A-H use captures the live section once on the audio owner,
 * then the main-loop service copies at most one compact inactive seed per
 * call. Repeated requests while seeding are last-request-wins and do not
 * recapture the core. No expanded bb_core copy is made by the seeder. */
int pb_seed_accepting(const prepared_bank *);
int pb_seed_request(prepared_bank *,const bb_core *,unsigned,
    const uint8_t [BB_MACHINES],unsigned slot);
int pb_seed_claim(prepared_bank *,uint32_t *ticket,unsigned *slot);
int pb_seed_copy(prepared_bank *,uint32_t ticket,unsigned slot);
int pb_seed_finish(prepared_bank *,uint32_t ticket,unsigned slot,int valid);
int pb_store_suspended(prepared_bank *,unsigned,const bb_core *,unsigned);
int pb_bind_families(prepared_bank *,const uint8_t [BB_MACHINES]);
int pb_begin(prepared_bank *,bb_trio_program *,uint32_t *ticket);
int pb_expand_initial(prepared_bank *,uint32_t ticket,unsigned sample_rate);
int pb_publish(prepared_bank *,uint32_t ticket,int valid);
/* boundary is 0/1, supplied by the unchanged bank quantization scheduler.
 * Preserves current transport: this never starts playback. */
int pb_commit(prepared_bank *,bb_core *,int boundary);
/* Exclusive audio-owner commit: scan once, retain both owners' checks and
 * pinned source identity. The supplied check never replaces the scan. */
int pb_commit_checked(prepared_bank *,bb_core *,int boundary,
    const bb_trio_program *expected_source,uint32_t expected_check);
int pb_refill_claim(prepared_bank *,uint32_t *ticket,unsigned *slot);
int pb_refill_expand(prepared_bank *,uint32_t ticket,unsigned slot);
int pb_refill_finish(prepared_bank *,uint32_t ticket,unsigned slot,int valid);
void pb_cancel(prepared_bank *);
void pb_cancel_pending(prepared_bank *);
int pb_part_request(prepared_bank *,unsigned slot);
int pb_part_claim(prepared_bank *,uint32_t *ticket);
int pb_part_commit(prepared_bank *,bb_core *);
int pb_before_tick(prepared_bank *,bb_core *);
int pb_scenes_ready(const prepared_bank *);
int pb_part_pending(const prepared_bank *);
/* Additional gate for reset/restore/rebind/asset operations. It is NOT proof
 * that transport, FIFO, other producers or hardware rendering are suspended.
 * Keep the existing controls/worker/USB/volume/exclusive-access guards too.
 * Snapshot existence (parts.ready) is a separate engine-family conflict:
 * never replace that guard with pb_scenes_ready, which is false mid-refill. */
int pb_ownership_idle(const prepared_bank *);
#endif
