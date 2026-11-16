/* SPDX-License-Identifier: GPL-3.0-only
 * BB1 2.3: Kick-aware Dillo response.
 *
 * Reads the kick pattern from the active drum phrase and generates a
 * complementary bass line. The generated pattern follows the kick hits
 * with syncopation and fills around them.
 *
 * Called from the Evolve system or the Conductor when a pattern swap
 * brings a new kick line. The output is a bb_sequence (same format as
 * the Dillo's own sequence) that can be loaded as a suggestion.
 *
 * Copyright (C) 2026 Big Beat Project */
#ifndef BB_DILLO_KICK_H
#define BB_DILLO_KICK_H
#include "bb_core.h"

/* Style of bass response */
enum {
    BB_KICK_BASS_ROOT,     /* root note on every kick, rest elsewhere */
    BB_KICK_BASS_WALKING,  /* root on kick, passing tones between */
    BB_KICK_BASS_SYNCO,    /* anticipate the kick by one 16th */
    BB_KICK_BASS_OCTAVE,   /* root + octave alternation on kick hits */
    BB_KICK_BASS_STYLES
};

/* Generate a complementary Dillo bass pattern from a 16-step kick pattern.
 *
 * kick_steps: 16-bit bitmask (bit 0 = step 1, etc.) of kick hits.
 * root:       MIDI note number for the root (e.g. 36 for C2).
 * style:      one of BB_KICK_BASS_*.
 * out:        bb_sequence to fill. Caller owns the memory.
 *
 * Returns the number of notes written (= out->length), or 0 on error. */
int bb_dillo_kick_response(uint16_t kick_steps, unsigned root, unsigned style,
                           bb_sequence *out);

#endif
