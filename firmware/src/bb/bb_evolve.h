/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef BB_EVOLVE_H
#define BB_EVOLVE_H
#include "bb_core.h"
#define BB_EVOLVE_PARTS (BB_MACHINES+BB_SYNTHS)
#define BB_EVOLVE_MAX_CHANGED_STEPS 8u
enum {BB_EVOLVE_RHYTHM=1,BB_EVOLVE_PITCH=2,BB_EVOLVE_ACCENT=4,BB_EVOLVE_SLIDE=8};
typedef struct {
    bb_drum_step drums[BB_MACHINES][BB_STEPS];
    bb_note synths[BB_SYNTHS][BB_STEPS];
    uint8_t length[BB_EVOLVE_PARTS];
    uint8_t family[BB_MACHINES];
} bb_pattern_set;
typedef struct {
    uint64_t locked[BB_EVOLVE_PARTS]; /* entire protected steps, including hidden steps */
    uint16_t drum_voices,scale; /* 11 voice bits; 12 relative pitch-class bits */
    uint8_t parts,attributes,amount,every_phrases,root,low_note,high_note;
} bb_evolve_settings;
typedef struct {
    bb_pattern_set home,expected;
    bb_evolve_settings settings;
    uint32_t seed,random,changes;
    uint8_t phrases[BB_EVOLVE_PARTS],seen[BB_EVOLVE_PARTS];
    uint8_t enabled,frozen,conflict,ready;
    uint8_t current_family[BB_MACHINES];
} bb_evolve;
/* All APIs require exclusive audio-owner access; no locks, allocation, flash
 * writes or UI integration. Capture replaces the in-memory home deliberately,
 * leaves evolution frozen, and does not save to disk. Return Home rewrites
 * patterns only, requires matching lengths, and resets the repeatable seed. */
int bb_evolve_init(bb_evolve *,const bb_core *,uint32_t seed);
int bb_evolve_init_families(bb_evolve *,const bb_core *,uint32_t,const uint8_t [BB_MACHINES]);
/* Suspended/exclusive context update; a different home family freezes evolution
 * as a conflict, retaining the original home until an explicit capture. */
int bb_evolve_bind_families(bb_evolve *,const uint8_t [BB_MACHINES]);
int bb_evolve_configure(bb_evolve *,const bb_evolve_settings *);
int bb_evolve_capture(bb_evolve *,const bb_core *);
int bb_evolve_home(bb_evolve *,bb_core *);
/* Read-only conflict check, also usable while frozen or awaiting Return Home. */
int bb_evolve_matches(const bb_evolve *,const bb_core *);
/* Pass to bb_process_render_hook with bb_evolve as context when event/render
 * callbacks are null; otherwise a small caller wrapper supplies its own ctx. */
void bb_evolve_before_tick(void *,bb_core *);
#endif
