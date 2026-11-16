/* SPDX-License-Identifier: GPL-3.0-only */
/* Experimental semantic phrase representation; NOT a playback admission API. */
#ifndef BB_PHRASE_EXPERIMENT_H
#define BB_PHRASE_EXPERIMENT_H
#include "bb_factory_drum.h"
#ifndef BB_WITH_STEP_CONDITIONS
#define BB_WITH_STEP_CONDITIONS 0
#endif
enum {BB_PHRASE_SHAKER=BB_FDV_COUNT,BB_PHRASE_VOICES};
enum {
    BB_PC_ALWAYS,
    BB_PC_FIRST,
    BB_PC_EVERY_2,
    BB_PC_EVERY_4,
    BB_PC_FILL,
    BB_PC_NOT_FILL,
    BB_PC_COUNT
};
enum {BB_PHRASE_CONDITION_EDITS=16};
typedef struct {
    uint16_t events[BB_FACTORY_DRUM_EVENTS];
    uint8_t length,count,lattice,bars;
    uint8_t ratchets[(BB_FACTORY_DRUM_EVENTS+3u)/4u]; /* two bits per source hit; zero=x1 */
#if BB_WITH_STEP_CONDITIONS
    /* Three bits per source ordinal.  This is deliberately a sidecar: the
     * event word's modifier bits retain source flam/retrigger provenance. */
    uint8_t conditions[(BB_FACTORY_DRUM_EVENTS*3u+7u)/8u];
#endif
} bb_phrase;
typedef struct {uint8_t voice,step,intensity,modifiers;} bb_phrase_event;
/* Source order is retained: ordinal identifies the original catalogue event.
 * Catalogue/provenance ownership stays with the caller. No voice substitution,
 * duplicate merging, time expansion or executable-engine claim is made here.
 * In-memory representation only; scene migration/wire format not implemented.
 */
/* Explicit source SH to independent maracas-based shaker adaptation. */
int bb_phrase_pack_shaker(const bb_factory_drum_data *,bb_phrase *);
int bb_phrase_pack(const bb_factory_drum_data *,bb_phrase *);
/* Cell setter updates every source ordinal for that voice/step, preserving
 * source order, accents and original flam/retrigger metadata. No hit is added.
 * Step is zero-based; failure leaves the phrase unchanged. */
/* Canonical sidecar: unused ordinals zero; control events never repeat. */
int bb_phrase_ratchets_valid(const bb_phrase *);
unsigned bb_phrase_ratchet_count(const bb_phrase *,unsigned ordinal);
int bb_phrase_ratchet_set(bb_phrase *,unsigned voice,unsigned step,unsigned count);
#if BB_WITH_STEP_CONDITIONS
/* Loop is zero based: FIRST is loop 0; EVERY 2 is loops 1,3,...; EVERY 4 is
 * loops 3,7,... .  FILL is an explicit per-loop latch supplied by the caller.
 * Invalid state fails closed.  A failed setter leaves the phrase unchanged. */
int bb_phrase_conditions_valid(const bb_phrase *);
unsigned bb_phrase_condition_count(const bb_phrase *);
unsigned bb_phrase_condition(const bb_phrase *,unsigned ordinal);
/* O(1) read for an already validated/prepared immutable phrase. */
unsigned bb_phrase_condition_prepared(const bb_phrase *,unsigned ordinal);
int bb_phrase_condition_set(bb_phrase *,unsigned ordinal,unsigned condition);
int bb_phrase_condition_set_cell(bb_phrase *,unsigned voice,unsigned step,unsigned condition);
int bb_phrase_condition_play(unsigned condition,uint32_t loop,unsigned fill);
#endif
int bb_phrase_read(const bb_phrase *,unsigned,bb_phrase_event *);
#endif
