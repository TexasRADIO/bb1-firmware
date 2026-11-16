/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef BB_EXPRESSION_SCENE_H
#define BB_EXPRESSION_SCENE_H
#include "bb_core.h"
#include "bb_note_expression.h"
#include <stddef.h>
#define BB_EXPRESSION_SCENE_OVERHEAD (16u+2u*BB_SYNTHS*BB_STEPS)
/* Cold framing component, NOT a scene validator or live restore operation.
 * Encode the existing scene at output+OVERHEAD first, then finish here.
 * This avoids a second full scene buffer. Zero sidecars emit exact legacy
 * bytes. The expression source must be a coherent, separately owned snapshot.
 * Failure leaves output untouched. No flash access. */
size_t bb_expression_scene_finish(void *,size_t,size_t,
                                  const bb_note_expression [BB_SYNTHS][BB_STEPS]);
/* Validates envelope/metadata only. Caller MUST validate the returned nested
 * scene with its existing scene/library decoder before applying either state.
 * Failure leaves expressions, nested pointer and nested length untouched.
 * A legacy scene produces zero sidecars. Nested view borrows input lifetime. */
int bb_expression_scene_open(const void *,size_t,
                             bb_note_expression [BB_SYNTHS][BB_STEPS],
                             const void **,size_t *);
#endif
