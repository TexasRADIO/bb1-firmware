/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef BB_RICH_SCENE_H
#define BB_RICH_SCENE_H
#include "bb_phrase_index.h"
#define BB_RICH_SCENE_PARTS 2u
#define BB_RICH_SCENE_LEGACY_OVERHEAD (16u+260u*BB_RICH_SCENE_PARTS)
#if BB_WITH_STEP_CONDITIONS
#define BB_RICH_SCENE_PHRASE_BYTES (293u+2u*BB_PHRASE_CONDITION_EDITS)
#else
#define BB_RICH_SCENE_PHRASE_BYTES 292u
#endif
#define BB_RICH_SCENE_NUDGE_BYTES 34u
#if BB_WITH_PHRASE_NUDGE
#define BB_RICH_SCENE_TIMING_BYTES (BB_RICH_SCENE_NUDGE_BYTES*BB_RICH_SCENE_PARTS)
#else
#define BB_RICH_SCENE_TIMING_BYTES 0u
#endif
#define BB_RICH_SCENE_OVERHEAD (16u+BB_RICH_SCENE_PHRASE_BYTES*BB_RICH_SCENE_PARTS+BB_RICH_SCENE_TIMING_BYTES+16u+4u*2u*BB_STEPS)
/* Cold framing only, not a live restore API or nested-scene validator.
 * Encode nested scene at output+OVERHEAD; zero mask emits exact legacy bytes.
 * Mask bits select canonical packed phrases; inactive slots are all zero.
 * Source ordinals/voice/step/intensity/modifiers and grid header are retained.
 */
size_t bb_rich_scene_finish(void *,size_t,size_t,const bb_phrase [BB_RICH_SCENE_PARTS],unsigned);
/* Normal writer emits BBRP v2, even with all counts x1; 1.5 rejects it.
 * A condition-enabled research build emits v3 and reads v1/v2 as ALWAYS.
 * Native grid counters are sparse for the two supported machines. Rich source
 * hit counts live in their immutable phrases. Nonzero machine2/3 counts refuse.
 * The legacy finish API above refuses ratchet loss and preserves v1 bytes. */
size_t bb_rich_scene_finish_v2(void *,size_t,size_t,const bb_phrase [2],unsigned,const bb_core *);
#if BB_WITH_PHRASE_NUDGE
/* BBRP v4 stores one canonical timing sidecar after each active phrase; v5
 * adds the condition records before that sidecar.  The timing reader treats
 * v1-v3 as zero timing.  The legacy reader refuses v4/v5 rather than silently
 * dropping saved timing. */
size_t bb_rich_scene_finish_v4(void *,size_t,size_t,const bb_phrase [2],const bb_phrase_nudges [2],unsigned,const bb_core *);
int bb_rich_scene_open_v4(const void *,size_t,bb_phrase [2],bb_phrase_nudges [2],uint8_t *,const void **,size_t *);
#endif
/* Apply only after the nested core decoder succeeds in disposable staging.
 * Legacy scenes supply all-x1. Framing/metadata validation precedes mutation. */
int bb_rich_scene_apply_ratchets(const void *,size_t,bb_core *);
/* Complete input/alias/framing/phrase validation precedes output publication.
 * Legacy input supplies zero mask/phrases. Nested view borrows input lifetime.
 * Caller must validate nested scene AND engine admission before live commit. */
int bb_rich_scene_open(const void *,size_t,bb_phrase [BB_RICH_SCENE_PARTS],uint8_t *,const void **,size_t *);
#endif
