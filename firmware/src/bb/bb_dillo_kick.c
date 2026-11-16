/* SPDX-License-Identifier: GPL-3.0-only
 * BB1 2.3: Kick-aware Dillo response.
 *
 * Copyright (C) 2026 Big Beat Project */
#include "bb_dillo_kick.h"

/* Scale intervals from root for walking bass (minor pentatonic) */
static const int8_t walk_intervals[] = { 0, 3, 5, 7, 10, 12 };
#define WALK_COUNT 6

int bb_dillo_kick_response(uint16_t kick_steps, unsigned root, unsigned style,
                           bb_sequence *out)
{
    unsigned i, n;
    if (!out || root > 127 || style >= BB_KICK_BASS_STYLES)
        return 0;

    /* Clear the output sequence */
    for (i = 0; i < BB_STEPS; i++) {
        out->notes[i].note = 0;
        out->notes[i].gate = 0;
        out->notes[i].accent = 0;
        out->notes[i].slide = 0;
    }
    out->length = 16;

    n = 0;
    switch (style) {

    case BB_KICK_BASS_ROOT:
        /* Root note on every kick, rest elsewhere */
        for (i = 0; i < 16; i++) {
            if (kick_steps & (1u << i)) {
                out->notes[i].note = (uint8_t)root;
                out->notes[i].gate = 1;
                out->notes[i].accent = (i == 0) ? 1 : 0;
                n++;
            }
        }
        break;

    case BB_KICK_BASS_WALKING:
        /* Root on kick, passing tones between kicks */
        {
            unsigned wi = 0;
            for (i = 0; i < 16; i++) {
                if (kick_steps & (1u << i)) {
                    /* Kick hit: play root */
                    out->notes[i].note = (uint8_t)root;
                    out->notes[i].gate = 1;
                    out->notes[i].accent = 1;
                    wi = 0;
                    n++;
                } else {
                    /* Between kicks: walk up the scale */
                    unsigned prev_kick = 0, next_kick = 0;
                    unsigned j;
                    /* Find distance to previous and next kick */
                    for (j = i; j > 0; j--) {
                        if (kick_steps & (1u << (j - 1))) { prev_kick = i - (j - 1); break; }
                    }
                    for (j = i + 1; j < 16; j++) {
                        if (kick_steps & (1u << j)) { next_kick = j - i; break; }
                    }
                    /* Play on every other 16th between kicks */
                    if (prev_kick > 0 && (prev_kick & 1)) {
                        wi = (wi + 1) % WALK_COUNT;
                        int note = (int)root + walk_intervals[wi];
                        if (note > 127) note = 127;
                        out->notes[i].note = (uint8_t)note;
                        out->notes[i].gate = 1;
                        /* Slide into the next kick */
                        if (next_kick == 1) out->notes[i].slide = 1;
                        n++;
                    }
                }
            }
        }
        break;

    case BB_KICK_BASS_SYNCO:
        /* Anticipate the kick by one 16th */
        for (i = 0; i < 16; i++) {
            unsigned next = (i + 1) & 15u;
            if (kick_steps & (1u << next)) {
                /* Play one step before the kick */
                out->notes[i].note = (uint8_t)root;
                out->notes[i].gate = 1;
                out->notes[i].slide = 1; /* slide into the kick */
                n++;
            } else if (kick_steps & (1u << i)) {
                /* Also play on the kick itself */
                out->notes[i].note = (uint8_t)root;
                out->notes[i].gate = 1;
                out->notes[i].accent = 1;
                n++;
            }
        }
        break;

    case BB_KICK_BASS_OCTAVE:
        /* Root on kick, octave up on the next 16th */
        for (i = 0; i < 16; i++) {
            if (kick_steps & (1u << i)) {
                out->notes[i].note = (uint8_t)root;
                out->notes[i].gate = 1;
                out->notes[i].accent = 1;
                n++;
                /* Octave on the next step if it's free */
                unsigned next = (i + 1) & 15u;
                if (!(kick_steps & (1u << next)) && !out->notes[next].gate) {
                    unsigned oct = root + 12;
                    out->notes[next].note = (uint8_t)(oct > 127 ? 127 : oct);
                    out->notes[next].gate = 1;
                    n++;
                }
            }
        }
        break;
    }

    out->muted = 0;
    return (int)n;
}
