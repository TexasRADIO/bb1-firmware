/* SPDX-License-Identifier: GPL-3.0-only */
/* Experimental semantic phrase representation; NOT a playback admission API. */
#ifndef BB_PHRASE_EXPERIMENT_H
#define BB_PHRASE_EXPERIMENT_H
#include "bb_factory_drum.h"
enum {BB_PHRASE_SHAKER=BB_FDV_COUNT,BB_PHRASE_VOICES};
typedef struct {
    uint16_t events[BB_FACTORY_DRUM_EVENTS];
    uint8_t length,count,lattice,bars;
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
int bb_phrase_read(const bb_phrase *,unsigned,bb_phrase_event *);
#endif
