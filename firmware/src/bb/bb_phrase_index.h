/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef BB_PHRASE_INDEX_H
#define BB_PHRASE_INDEX_H
#include "bb_phrase.h"
#include "bb_phrase_controls.h"
#include "bb_delayed.h"
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
} bb_phrase_index;
/* All playable source voice bits must be provided by the chosen engine.
 * Control events are separate. Invalid/missing capability leaves out unchanged.
 * Success is semantic admission only: no promise of independent polyphony.
 */
int bb_phrase_index_prepare(const bb_phrase *,uint32_t playable,bb_phrase_index *);
/* Validate a copied index without constructing another phrase-sized object.
 * Publication boundary only, not a required per-hit operation. */
int bb_phrase_index_valid(const bb_phrase_index *,uint32_t playable);
/* Audio owner: consumes only a successfully prepared immutable index. Maintains
 * the retained explicit Studio-reference delay policy, not recording fidelity.
 * Writes the queue transactionally through bb_delayed_offer; no dropped hits.
 */
int bb_phrase_index_delays(const bb_phrase_index *,unsigned step,uint64_t frame,
                          uint32_t step_frames,uint32_t sample_rate,bb_delayed *);
#endif
