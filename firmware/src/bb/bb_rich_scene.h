/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef BB_RICH_SCENE_H
#define BB_RICH_SCENE_H
#include "bb_phrase_index.h"
#define BB_RICH_SCENE_PARTS 2u
#define BB_RICH_SCENE_LEGACY_OVERHEAD (16u+260u*BB_RICH_SCENE_PARTS)
#define BB_RICH_SCENE_OVERHEAD (16u+292u*BB_RICH_SCENE_PARTS+16u+4u*2u*BB_STEPS)
/* Cold framing only, not a live restore API or nested-scene validator.
 * Encode nested scene at output+OVERHEAD; zero mask emits exact legacy bytes.
 * Mask bits select canonical packed phrases; inactive slots are all zero.
 * Source ordinals/voice/step/intensity/modifiers and grid header are retained.
 */
size_t bb_rich_scene_finish(void *,size_t,size_t,const bb_phrase [BB_RICH_SCENE_PARTS],unsigned);
/* 2.0 writer always emits BBRP v2, even with all counts x1; 1.5 rejects it.
 * Native grid counters are sparse for the two supported machines. Rich source
 * hit counts live in their immutable phrases. Nonzero machine2/3 counts refuse.
 * The legacy finish API above refuses ratchet loss and preserves v1 bytes. */
size_t bb_rich_scene_finish_v2(void *,size_t,size_t,const bb_phrase [2],unsigned,const bb_core *);
/* Apply only after the nested core decoder succeeds in disposable staging.
 * Legacy scenes supply all-x1. Framing/metadata validation precedes mutation. */
int bb_rich_scene_apply_ratchets(const void *,size_t,bb_core *);
/* Complete input/alias/framing/phrase validation precedes output publication.
 * Legacy input supplies zero mask/phrases. Nested view borrows input lifetime.
 * Caller must validate nested scene AND engine admission before live commit. */
int bb_rich_scene_open(const void *,size_t,bb_phrase [BB_RICH_SCENE_PARTS],uint8_t *,const void **,size_t *);
#endif
