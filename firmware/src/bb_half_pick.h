/* SPDX-License-Identifier: GPL-3.0-only
 * 2.1 P1 measurement builds only: the half-lit picker's decision, shared by the firmware wrapper
 * (bb_nav_firmware.inc, bb_half_pick_scan) and the host test (big-beat/integration/tests/half_pick.c).
 * OCT- / OCT+ step the setting only on GLOBAL's main bank, and only when the press is not part of a HOME gesture:
 * not while help is up or HOME is held before the scan (quiet_before), nor when this scan's input opened help or
 * started a HOME press (Duke #64: inside help OCT turns help pages; the picker must not also step and overwrite
 * the notice). Returns the new setting 0..4, or -1 for no change. */
#pragma once
#include "bb_nav.h"
#include "bb_p1_measure.h"
/* RC1: the release half-lit level. Joe picked HALF-LIT 3/4 = MID 1/8 on the unit (Duke PR68 4a82b1d, measurement
 * pair a793e293 / daa4fd8a / 10c2367f, with the default DIM HI glow). The settings: 0 today's glow (MENU > LEDS),
 * 1 glow DIM HI, 2 glow DIM LO, 3 MID 1/8 (+ DIM HI glow), 4 MID 1/12 (+ DIM LO glow). A release build has no picker
 * (BB_P1_HALF_LIT_PICKER 0): its half-lit LEDs are at this setting's mid level, and the glow stays MENU > LEDS, whose
 * default is DIM HI (with DIM LO the HAL runs the mid level 1 frame in 12). */
#define BB_HALF_LIT_RELEASE 3u
/* the mid level of the half-lit (glowing) LEDs at a setting: 3 and 4 light them 1 frame in N */
static inline uint8_t bb_half_mid(unsigned setting, uint8_t dim) { return setting >= 3u ? dim : (uint8_t)0u; }
/* the glow at a setting (the measurement picker): 1 = DIM LO; setting 0 is MENU > LEDS */
static inline unsigned bb_half_glow_lo(unsigned setting, int menu_lo) { return setting ? setting == 2u || setting == 4u : menu_lo != 0; }
#if BB_P1_HALF_LIT_PICKER
#define BB_HALF_MID(dim) bb_half_mid(bb_half_pick, (dim))   /* measurement builds: the picker's setting */
#else
#define BB_HALF_MID(dim) bb_half_mid(BB_HALF_LIT_RELEASE, (dim))
#endif
/* help up or HOME held before the scan: the firmware wrapper and the host test use this same expression */
#define BB_HALF_PICK_QUIET_BEFORE(s) ((s)->help || (s)->home_down)
static inline int bb_half_pick_quiet(const bb_nav_state *s, uint32_t held_logical)
{
    return s->help || s->home_down || (held_logical & BB_NAV_BIT(BB_NAV_B_HOME)) != 0;
}
static inline int bb_half_pick_step(const bb_nav_state *after, int quiet_before, uint32_t pressed_logical,
                                    uint32_t held_logical, unsigned current)
{
    const uint32_t oct = BB_NAV_BIT(BB_NAV_B_OCTDN) | BB_NAV_BIT(BB_NAV_B_OCTUP);
    if (quiet_before || bb_half_pick_quiet(after, held_logical)) return -1;
    if (after->page != BB_NAV_GLOBAL || after->bank || !(pressed_logical & oct)) return -1;
    return (int)((current + ((pressed_logical & BB_NAV_BIT(BB_NAV_B_OCTUP)) ? 1u : 4u)) % 5u);
}
