/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef BB_PHRASE_INDEX_H
#define BB_PHRASE_INDEX_H
#include "bb_phrase.h"
#include "bb_phrase_controls.h"
#include "bb_delayed.h"
#ifndef BB_WITH_PHRASE_NUDGE
#define BB_WITH_PHRASE_NUDGE 0
#endif
#if BB_WITH_PHRASE_NUDGE
#include "bb_phrase_nudge.h"
#endif
/* Prepare off the audio thread, then copy/own the complete immutable object.
 * Source ordinal remains the index into phrase.events, not a sorted position.
 * No source pointer, allocation, catalogue access or live-engine claim.
 */
typedef struct {
    bb_phrase phrase;
    bb_phrase_controls controls;
    uint32_t voices; /* Prepared sound identities; excludes accent controls. */
    uint8_t start[BB_STEPS+1];
    uint8_t ordinal[BB_FACTORY_DRUM_EVENTS];
#if BB_WITH_PHRASE_NUDGE
    bb_phrase_nudges nudges; /* copied atomically with inbox/active source */
#endif
} bb_phrase_index;
/* All playable source voice bits must be provided by the chosen engine.
 * Control events are separate. Invalid/missing capability leaves out unchanged.
 * Success is semantic admission only: no promise of independent polyphony.
 */
int bb_phrase_index_prepare(const bb_phrase *,uint32_t playable,bb_phrase_index *);
#if BB_WITH_PHRASE_NUDGE
/* Main-owner immutable edit primitive.  The complete index is copied only
 * after canonical-map and combined delayed-queue admission pass. */
int bb_phrase_index_nudge_edit(const bb_phrase_index *,unsigned voice,
                               unsigned step,int value,bb_phrase_index *);
#endif
/* Validate a copied index without constructing another phrase-sized object.
 * Publication boundary only, not a required per-hit operation. */
int bb_phrase_index_valid(const bb_phrase_index *,uint32_t playable);
/* O(1) only on an admitted immutable index. Invalid input fails closed. */
int bb_phrase_index_hit_enabled(const bb_phrase_index *,unsigned ordinal,
                                uint32_t loop,unsigned fill);
#define BB_PHRASE_MODIFIER_BATCH_LIMIT 16u
#define BB_RATCHET_GROUP UINT32_C(0x80000000)
#define BB_RATCHET_NATIVE UINT32_C(0x40000000)
#define BB_RATCHET_STRIKE UINT32_C(0x02000000)
/* Group payload: step[5:0], count-1[7:6], numerator[9:8], tag bit31.
 * The owner freezes event counts for the current step before scheduling. */
typedef struct {
    uint8_t event_counts[(BB_FACTORY_DRUM_EVENTS+3u)/4u];
    bb_drum_step native;
    uint8_t step,active;
} bb_ratchet_snapshot;
void bb_phrase_ratchet_snapshot(const bb_phrase_index *,unsigned,uint32_t loop,
                                unsigned fill,bb_ratchet_snapshot *);
#if BB_WITH_PHRASE_NUDGE
/* Snapshot only retained grid-bound ratchets. Shifted cells own their ratchet
 * groups; a fresh opening admits a negative cell at its deliberate clamp. */
void bb_phrase_ratchet_snapshot_nudged(const bb_phrase_index *,unsigned,
                                       uint32_t loop,unsigned fill,
                                       unsigned opening,bb_ratchet_snapshot *);
#endif
/* Convert one tagged group into matching existing source ordinals. No new
 * source event or voice substitution is invented. -1 invalid,0 not selected,
 * 1 returns original encoded hit with the existing repeat attenuation tag. */
int bb_phrase_ratchet_group_hit(const bb_phrase_index *,const bb_ratchet_snapshot *,
                                uint32_t,unsigned ordinal,uint32_t *payload);
/* Audio owner: consumes only a successfully prepared immutable index. Maintains
 * the retained explicit Studio-reference delay policy, not recording fidelity.
 * Writes the queue transactionally through bb_delayed_offer; no dropped hits.
 */
int bb_phrase_index_delays(const bb_phrase_index *,unsigned step,uint64_t frame,
                          uint32_t step_frames,uint32_t sample_rate,uint32_t loop,
                          unsigned fill,bb_delayed *);
#if BB_WITH_PHRASE_NUDGE
/* Research-only whole-gesture scheduler. Combines retained unnudged
 * derivatives and shifted cell gestures against a private queue copy, then
 * publishes the queue only if every offer succeeds. step is one-based. */
int bb_phrase_index_delays_nudged(const bb_phrase_index *,unsigned step,
                                  uint64_t frame,uint32_t step_frames,
                                  uint32_t next_step_frames,
                                  uint32_t period_frames,uint32_t sample_rate,
                                  uint32_t loop,unsigned fill,unsigned opening,
                                  bb_delayed *);
#endif
#endif
