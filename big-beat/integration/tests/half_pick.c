/* SPDX-License-Identifier: GPL-3.0-only
 * 2.1 P1 measurement picker (bb_half_pick.h) through the firmware wrapper's path on the real bb_nav_input:
 * quiet = BB_HALF_PICK_QUIET_BEFORE(nav) before the scan, bb_nav_input, then bb_half_pick_step(nav after, quiet, ...),
 * exactly as bb_nav_scan + bb_half_pick_scan in bb_nav_firmware.inc. Duke #64: with help open or HOME held on
 * GLOBAL, OCT must page help only; the picker must not step (or overwrite the notice). */
#include "bb_half_pick.h"
#include <stdio.h>
#include <string.h>
static unsigned checks, failures, pick, steps;
static uint32_t now = 1000;
#define CHECK(c, ...) do { checks++; if (!(c)) { failures++; printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); } } while (0)
#define B(x) BB_NAV_BIT(BB_NAV_B_##x)
static bb_nav_out o;
static void wrap(bb_nav_state *s, uint32_t pressed, uint32_t held, uint32_t dt)
{
    bb_nav_in in; memset(&in, 0, sizeof in);
    in.pressed = pressed; in.held = held; in.now_ms = (now += dt);
    in.facts.kit_slot = in.facts.kit_slot_queued = in.facts.section_queued = in.facts.section = BB_NAV_NONE_IDX;
    in.facts.list_cursor = in.facts.list_playing = in.facts.list_queued = BB_NAV_NONE_IDX;
    int quiet = BB_HALF_PICK_QUIET_BEFORE(s);
    bb_nav_input(s, &in, &o);
    int next = bb_half_pick_step(s, quiet, pressed, held, pick);
    if (next >= 0) { pick = (unsigned)next; steps++; }
}
static void global(bb_nav_state *s)
{
    bb_nav_init(s, BB_NAV_STUDIO, 600);
    bb_nav_set_instrument(s, BB_NAV_INSTR_DRUM, 2);
    s->page = BB_NAV_GLOBAL; s->bank = 0;
}
int main(void)
{
    bb_nav_state s; unsigned st;
    /* 1. plain OCT on GLOBAL steps both ways */
    global(&s); pick = 0; wrap(&s, B(OCTUP), B(OCTUP), 20); wrap(&s, 0, 0, 20);
    CHECK(pick == 1, "OCT+ steps to 1 (%u)", pick);
    wrap(&s, B(OCTDN), B(OCTDN), 20); wrap(&s, 0, 0, 20); CHECK(pick == 0, "OCT- steps back (%u)", pick);
    wrap(&s, B(OCTDN), B(OCTDN), 20); wrap(&s, 0, 0, 20); CHECK(pick == 4, "OCT- wraps to 4 (%u)", pick);
    /* 2. HOME held into help, OCT+ / OCT- inside help: help pages, picker unchanged; leaving help: no step */
    for (int dir = 0; dir < 2; dir++) {
        uint32_t oct = dir ? B(OCTDN) : B(OCTUP);
        global(&s); pick = 2; st = steps;
        wrap(&s, B(HOME), B(HOME), 20); wrap(&s, 0, B(HOME), 700);
        CHECK(s.help, "HOME held opens help");
        wrap(&s, oct, B(HOME) | oct, 20); wrap(&s, 0, B(HOME), 20);
        wrap(&s, oct, B(HOME) | oct, 20); wrap(&s, 0, B(HOME), 20);
        CHECK(s.help && pick == 2 && steps == st, "OCT inside help: picker %u steps %u", pick, steps - st);
        wrap(&s, 0, 0, 20);
        CHECK(!s.help && pick == 2 && steps == st, "leaving help: no step");
        CHECK(!(o.actions) && s.page == BB_NAV_GLOBAL, "leaving help acts on nothing");
    }
    /* 3. HOME held (before help opens) with OCT: no step */
    global(&s); pick = 1; st = steps;
    wrap(&s, B(HOME), B(HOME), 20); wrap(&s, B(OCTUP), B(HOME) | B(OCTUP), 20);
    CHECK(pick == 1 && steps == st, "HOME held + OCT: no step");
    /* 4. HOME and OCT in the same scan: no step */
    global(&s); pick = 1; st = steps;
    wrap(&s, B(HOME) | B(OCTUP), B(HOME) | B(OCTUP), 20);
    CHECK(pick == 1 && steps == st, "HOME + OCT in one scan: no step");
    /* 5. other pages and GLOBAL's MIDI banks: no step */
    global(&s); s.page = BB_NAV_PLAY; pick = 0; wrap(&s, B(OCTUP), B(OCTUP), 20); CHECK(pick == 0, "PLAY: no step");
    global(&s); s.bank = 1; pick = 0; wrap(&s, B(OCTUP), B(OCTUP), 20); CHECK(pick == 0, "MIDI bank: no step");
    /* 6. after help closed, OCT works again */
    global(&s); pick = 0; wrap(&s, B(HOME), B(HOME), 20); wrap(&s, 0, B(HOME), 700); wrap(&s, 0, 0, 20);
    wrap(&s, B(OCTUP), B(OCTUP), 20); CHECK(pick == 1, "OCT after help: steps (%u)", pick);
    /* 7. RC1: the release default is Joe's pick, HALF-LIT 3/4 = MID 1/8 (Duke PR68 4a82b1d), and has no picker */
#ifndef BB_HALF_LIT_RELEASE
    CHECK(0, "no release half-lit default (the release keeps today's glow, not Joe's MID 1/8)");
#else
    CHECK(BB_HALF_LIT_RELEASE == 3u, "release default is setting 3 (MID 1/8), not %u", (unsigned)BB_HALF_LIT_RELEASE);
    CHECK(BB_P1_HALF_LIT_PICKER == 0, "release flags: no picker");
    for (unsigned d = 0; d < 256u; d++) CHECK(BB_HALF_MID((uint8_t)d) == d, "release: half-lit LEDs at the mid level (%u)", d);
    for (unsigned k = 0; k < 5u; k++) CHECK(bb_half_mid(k, 0xA5u) == (k >= 3u ? 0xA5u : 0u), "setting %u mid", k);
    CHECK(bb_half_glow_lo(3u, 0) == 0 && bb_half_glow_lo(4u, 0) == 1 && bb_half_glow_lo(0u, 1) == 1 && bb_half_glow_lo(2u, 0) == 1,
          "glow per setting (3: DIM HI, as Joe saw it)");
#endif
    printf("half pick: %u checks, %u failures\n", checks, failures);
    return failures != 0;
}
