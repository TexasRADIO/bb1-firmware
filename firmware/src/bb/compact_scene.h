/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef BB_PROPOSAL_COMPACT_SCENE_H
#define BB_PROPOSAL_COMPACT_SCENE_H
#include "bb_core.h"
/* Isolated RAM representation, NOT a new on-disk session format. */
typedef struct { uint16_t notes[BB_STEPS]; uint8_t length,muted; } compact_sequence;
typedef struct {
    bb_machine drums[BB_MACHINES];
    compact_sequence synths[BB_SYNTHS];
    bb_dillo_patch dillo;
    bb_telegram_patch telegram;
    bb_fx_settings effects;
    uint8_t band_level[BB_MIX_CHANNELS];
    uint16_t bpm;
    uint8_t swing;
} compact_scene;
/* Exclusive stable source. Invalid source leaves destination untouched. */
int compact_capture(compact_scene *, const bb_core *);
/* Control-side disposable scratch only. Failure may overwrite scratch; it
 * must never be the running core or published until this returns success. */
int compact_prepare(bb_core *scratch,const compact_scene *,unsigned rate);
#endif
