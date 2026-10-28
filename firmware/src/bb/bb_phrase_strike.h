/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef BB_PHRASE_STRIKE_H
#define BB_PHRASE_STRIKE_H
#include "bb_phrase_controls.h"
#include "bb_source_trigger.h"
/* Audio-owner dispatch for a prepared immutable phrase/control pair.
 * Payload: original word bits0..15, ordinal16..22, kind23..24.
 * kind0 primary,1 flam,2/3 repeats; other bits must be zero.
 * Queue generation/timing checked by its owner before calling this function.
 * 0 refusal (engine unchanged),1 triggered,2 accent control (no audio).
 */
int bb_phrase_strike(drum808_t *,const bb_phrase *,const bb_phrase_controls *,uint32_t payload);
typedef struct {unsigned voice;float velocity;} bb_phrase_hit;
/* Resolve the musical strike before choosing a sound backend. Same result
 * codes as strike; output remains unchanged on invalid/control events. */
int bb_phrase_hit_resolve(const bb_phrase *,const bb_phrase_controls *,uint32_t payload,bb_phrase_hit *);
#endif
