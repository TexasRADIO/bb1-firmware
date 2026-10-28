/* SPDX-License-Identifier: GPL-3.0-only
 * Playback routing only: never rewrite the original phrase or kit identity.
 * Voice numbers follow bb_phrase's BD..ZAP ordering; accents are not voices.
 */
#ifndef BB_VOICE_FALLBACK_H
#define BB_VOICE_FALLBACK_H
#include <stdint.h>
#define BB_FALLBACK_NONE 255u
static inline unsigned bb_voice_fallback(unsigned voice, uint32_t native) {
    static const uint8_t preferred[19][4] = {
        {2,4,1,8}, {10,8,9,2}, {3,4,5,0}, {2,4,6,0},
        {3,2,7,1}, {2,6,3,0}, {3,5,7,2}, {4,6,3,1},
        {9,10,1,12}, {8,12,10,11}, {1,8,9,11},
        {15,9,14,10}, {9,8,4,13}, {16,14,15,12},
        {15,13,16,11}, {14,11,9,8}, {13,14,12,15},
        {18,12,9,8}, {17,4,3,12}
    };
    native &= ((1u << 19)-1)|(1u<<21); /* 21 is the independent shaker. */
    if ((voice >= 19 && voice != 21) || !native) return BB_FALLBACK_NONE;
    if (native & (1u << voice)) return voice;
    if (voice == 21) voice=11; /* maracas is the first shaker substitute */
    if (native & (1u << voice)) return voice;
    for (unsigned i=0;i<4;i++)
        if (native & (1u << preferred[voice][i])) return preferred[voice][i];
    /* Last resort is deterministic, bounded, and always an existing sound. */
    static const uint8_t last[] = {8,9,1,4,3,2,12,10,15,11,14,13,16,0,7,6,5,17,18};
    for (unsigned i=0;i<19;i++) if(native & (1u << last[i])) return last[i];
    return (native & (1u<<21)) ? 21 : BB_FALLBACK_NONE;
}
#endif
