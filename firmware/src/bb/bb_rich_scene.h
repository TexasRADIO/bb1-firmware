/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef BB_RICH_SCENE_H
#define BB_RICH_SCENE_H
#include "bb_phrase_index.h"
#define BB_RICH_SCENE_PARTS 2u
#define BB_RICH_SCENE_OVERHEAD (16u+260u*BB_RICH_SCENE_PARTS)
/* Cold framing only, not a live restore API or nested-scene validator.
 * Encode nested scene at output+OVERHEAD; zero mask emits exact legacy bytes.
 * Mask bits select canonical packed phrases; inactive slots are all zero.
 * Source ordinals/voice/step/intensity/modifiers and grid header are retained.
 */
size_t bb_rich_scene_finish(void *,size_t,size_t,const bb_phrase [BB_RICH_SCENE_PARTS],unsigned);
/* Complete input/alias/framing/phrase validation precedes output publication.
 * Legacy input supplies zero mask/phrases. Nested view borrows input lifetime.
 * Caller must validate nested scene AND engine admission before live commit. */
int bb_rich_scene_open(const void *,size_t,bb_phrase [BB_RICH_SCENE_PARTS],uint8_t *,const void **,size_t *);
#endif
