/* SPDX-License-Identifier: GPL-3.0-only */
/* Which v3 page draws a navigation state. Pure: depends only on the navigation
 * model (page, bank, play keys, instrument), so a host test can walk every
 * state navigation can reach and prove no normal page falls back to a legacy
 * renderer. Service screens (update boot, diagnostics) are the only BB_UI_NO. */
#ifndef BB_UI_V3_ROUTE_H
#define BB_UI_V3_ROUTE_H
#include "bb_nav.h"
#include "bb_ui_v3/bb_ui.h"

/* FX banks (bb_nav.c nav_fx_bank_names): ECHO TAPE SPRING MOTION SENDS. The
 * echo machine shows the echo/tape/motion banks; spring and sends show their
 * four controls as meters. */
enum { BB_V3_FX_ECHO, BB_V3_FX_TAPE, BB_V3_FX_SPRING, BB_V3_FX_MOTION, BB_V3_FX_SENDS };

static inline int bb_v3_nav_is_drum(const bb_nav_state *s)
{
    return s->instrument == BB_NAV_INSTR_DRUM || s->instrument == BB_NAV_INSTR_DRUM_909;
}

static inline uint8_t bb_v3_route(const bb_nav_state *s, int service)
{
    if (service)
        return BB_UI_NO;                       /* update boot / diagnostics: service presentation */
    switch (s->page) {
    case BB_NAV_STUDIO:  return BB_UI_STUDIO;  /* HOME: Big Beat + Dillo */
    case BB_NAV_PLAY:
        if (bb_v3_nav_is_drum(s))
            return s->play_keys == BB_NAV_PLAY_STEPS ? BB_UI_STEPS : BB_UI_PLAY_DRUMS;
        return BB_UI_PLAY_DILLO;               /* Dillo live and steps: the piano roll */
    case BB_NAV_KIT:     return BB_UI_KIT;
    case BB_NAV_PATTERN: return BB_UI_PATTERN;
    case BB_NAV_FX:
        return s->bank == BB_V3_FX_SPRING || s->bank == BB_V3_FX_SENDS ? BB_UI_KNOBS : BB_UI_FX;
    case BB_NAV_SOUND:
    case BB_NAV_SAVE:
    case BB_NAV_EVOLVE:
    case BB_NAV_GLOBAL:  return BB_UI_KNOBS;
    default:             return BB_UI_KNOBS;   /* any future page still gets the shared frame */
    }
}
#endif
