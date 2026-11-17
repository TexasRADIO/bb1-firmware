/* SPDX-License-Identifier: GPL-3.0-only */
/* Feature-gated bounded microtiming sidecar.  Runtime layout is not used as a
 * wire ABI: BBRP v4 encodes it explicitly as count/reserved + LE16 cells. */
#ifndef BB_PHRASE_NUDGE_H
#define BB_PHRASE_NUDGE_H
#include "bb_phrase.h"
#include "bb_delayed.h"

/* One sorted 16-bit cell record: voice[4:0], zero-based step[10:5],
 * signed nudge+8[14:11], reserved[15].  Zero nudge has no record.  A cell,
 * rather than a source ordinal, makes duplicate catalogue hits move together
 * without consuming several edit slots. */
enum {BB_PHRASE_NUDGE_EDITS=16};
#define BB_NUDGE_GROUP UINT32_C(0x20000000)
typedef struct {
    uint16_t cell[BB_PHRASE_NUDGE_EDITS];
    uint8_t count,reserved;
} bb_phrase_nudges;
typedef struct {
    uint64_t frame;
    uint32_t source_frames;
    uint8_t voice,step,early,reserved;
} bb_phrase_nudge_event;
typedef struct {
    uint16_t base,nudges,combined;
    uint8_t step,reserved;
} bb_phrase_nudge_queue_profile;

/* Canonical maps are sorted by step/voice, contain only existing playable
 * cells, have no zero-valued edits, and zero every unused word. */
int bb_phrase_nudges_valid(const bb_phrase *,const bb_phrase_nudges *);
/* Returns 1 for a valid phrase/map/cell and writes -8..+7 (zero if absent).
 * Invalid input returns 0 and leaves the output unchanged. */
int bb_phrase_nudge_get(const bb_phrase *,const bb_phrase_nudges *,
                        unsigned voice,unsigned step,int *out);
/* O(16) lookup for a sidecar already admitted inside an immutable phrase
 * index. Returns -8..+7; absent or structurally impossible input returns 0. */
int bb_phrase_nudge_prepared(const bb_phrase_nudges *,unsigned voice,unsigned step);
/* Transactional.  -8..+7; zero removes the cell.  Refusal leaves the map
 * byte-identical. */
int bb_phrase_nudge_set(const bb_phrase *,bb_phrase_nudges *,
                        unsigned voice,unsigned step,int nudge);
/* Carry timing edits across an ordinary phrase edit.  Cells that still exist
 * keep their value; cells removed by the edit disappear.  New cells do not
 * inherit timing from another source.  Invalid input leaves out unchanged;
 * out may alias the old sidecar. */
int bb_phrase_nudges_rebase(const bb_phrase *before,
                            const bb_phrase_nudges *old,
                            const bb_phrase *after,
                            bb_phrase_nudges *out);
/* Boundary policy for one existing cell: 1 triggers now, 0 is scheduled, -1
 * is invalid.  A negative cell triggers at a fresh transport/pattern opening;
 * on later passes it was scheduled early by the preceding step. */
int bb_phrase_nudge_primary(const bb_phrase *,const bb_phrase_nudges *,
                            unsigned voice,unsigned step,unsigned opening);
/* Plan this step's positive cells and the next step's negative cells as one
 * event per musical cell.  period_frames is the unswung step period, matching
 * the inherited -8..+7 policy; step_frames is this swung step's duration.
 * next_step_frames is the swung duration owned by an early next-step cell;
 * every derivative stays relative to that source duration. Results are sorted
 * by frame then step/voice. Failure is transactional. */
int bb_phrase_nudge_plan(const bb_phrase *,const bb_phrase_nudges *,
                         unsigned step,uint64_t frame,uint32_t step_frames,
                         uint32_t next_step_frames,uint32_t period_frames,
                         bb_phrase_nudge_event *,
                         unsigned capacity,unsigned *count);
/* Create the primary tagged group for one edited musical cell. The immutable
 * source may contain duplicate ordinals; dispatch expands the group back to
 * each exact source ordinal and never invents or merges a hit. The queue bridge
 * derives additional tagged groups for that cell's original modifiers. */
int bb_phrase_nudge_group(const bb_phrase *,const bb_phrase_nudges *,
                          const bb_phrase_nudge_event *,uint32_t loop,
                          unsigned fill,uint32_t *group);
/* -1 invalid group/state, 0 ordinal is not selected by the tagged gesture,
 * 1 returns the exact primary/flam/retrigger/ratchet payload. The group
 * freezes loop phase/FIRST/FILL so a delayed conditional hit cannot change
 * meaning after a loop boundary. */
int bb_phrase_nudge_group_hit(const bb_phrase *,const bb_phrase_nudges *,
                              uint32_t group,unsigned ordinal,uint32_t *payload);
/* Audio-owner queue bridge. Expands each already-proven cell plan into one
 * primary group plus its original flam, retrigger and ratchet groups, all
 * relative to the shifted source origin, then offers the entire batch under
 * the queue's current generation. No derivative can be accepted alone.
 * An early next-step hit at step zero belongs to the next loop; overflow of
 * that loop counter refuses before touching the queue. FILL is frozen at the
 * scheduling boundary, matching retained delayed-source policy. */
int bb_phrase_nudge_offer(const bb_phrase *,const bb_phrase_nudges *,
                          const bb_phrase_nudge_event *,unsigned count,
                          unsigned source_step,uint32_t loop,unsigned fill,
                          uint32_t sample_rate,uint64_t now,bb_delayed *);
/* Private-rollback-queue helper: visits canonical edited cells one at a time
 * and uses at most seven descriptors per cell. Unlike offer(), failure may
 * leave this queue changed; callers must pass a disposable copy and publish it
 * only after success. */
int bb_phrase_nudge_schedule_scratch(const bb_phrase *,const bb_phrase_nudges *,
                                     unsigned source_step,uint64_t frame,
                                     uint32_t step_frames,
                                     uint32_t next_step_frames,
                                     uint32_t period_frames,
                                     uint32_t sample_rate,uint32_t loop,
                                     unsigned fill,bb_delayed *);
/* Conservative cyclic delayed-queue profile. It mirrors retained 22-step
 * retrigger and three-step flam horizons, reserves primary groups plus shifted
 * ratchet classes, and carries late prior-step ratchets over the next boundary.
 * Conditions are treated as enabled. Success reports the worst boundary; the
 * caller decides whether combined fits its queue capacity. */
int bb_phrase_nudge_queue_measure(const bb_phrase *,const bb_phrase_nudges *,
                                  bb_phrase_nudge_queue_profile *);
#endif
