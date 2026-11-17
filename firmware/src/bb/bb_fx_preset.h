/* SPDX-License-Identifier: GPL-3.0-only
 * BB1 2.3: Per-song FX preset capture and recall.
 *
 * An FX preset bundles the full effect state: the 15 legacy parameters
 * (bb_fx_settings) plus the 4 extended parameters (bb_fx_extended).
 * Each song section can store one preset. When the section plays, its
 * FX preset loads automatically via the section bundle path.
 *
 * Copyright (C) 2026 Big Beat Project */
#ifndef BB_FX_PRESET_H
#define BB_FX_PRESET_H
#include "bb_fx_settings.h"

typedef struct {
    bb_fx_settings settings;    /* 15 × uint16_t = 30 bytes */
    bb_fx_extended extended;    /*  4 × uint8_t  =  4 bytes */
    uint8_t valid;              /* 1 if this preset holds saved data */
} bb_fx_preset;                 /* 35 bytes total */

/* Capture the current live FX state into a preset. */
static inline void bb_fx_preset_capture(bb_fx_preset *p,
                                        const bb_fx_settings *s,
                                        const bb_fx_extended *e)
{
    unsigned i;
    if (!p || !s || !e) return;
    for (i = 0; i < BB_FX_PARAMETER_COUNT; i++)
        p->settings.value[i] = s->value[i];
    p->extended = *e;
    p->valid = 1;
}

/* Apply a saved preset to live state. Returns 1 if the preset was valid
 * and applied, 0 if it was empty (no change made). */
static inline int bb_fx_preset_apply(const bb_fx_preset *p,
                                     bb_fx_settings *s,
                                     bb_fx_extended *e)
{
    unsigned i;
    if (!p || !p->valid || !s || !e) return 0;
    for (i = 0; i < BB_FX_PARAMETER_COUNT; i++)
        s->value[i] = p->settings.value[i];
    *e = p->extended;
    return 1;
}

/* Clear a preset (mark as empty). */
static inline void bb_fx_preset_clear(bb_fx_preset *p)
{
    if (p) p->valid = 0;
}

#endif
