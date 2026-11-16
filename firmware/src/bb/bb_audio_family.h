/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once
#include "bb_audio.h"
#if BB_WITH_909_ROUTE && (BB_WITH_PARTS || BB_WITH_EVOLVE) && !BB_WITH_PATTERN
#error "909 snapshots require the family-aware pattern handoff"
#endif
static inline const uint8_t *bb_audio_families(const bb_audio *a){
#if BB_WITH_PATTERN
    return a->drum_patterns.family;
#else
    static const uint8_t legacy[BB_MACHINES]={0};
    (void)a;return legacy;
#endif
}
