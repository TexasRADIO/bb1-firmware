/* SPDX-License-Identifier: GPL-3.0-only */
/* 2.1 RC3: the PERFORM chord (Joe 11:34: "just push all the buttons at the same time"). Pure, main loop only.
 *
 * Section keys pressed together are one combination: every key that goes down while one of the gesture's keys is
 * still held, or within BB_CHORD_MS of the first press, joins it. It is committed ONCE, when the window has closed:
 * then, if every key is up, or at once if keys are still held. A single tap is that section alone (72 ms later).
 * Hold-and-add (secondary): once the window has closed with a key still held, each further tap toggles that section
 * in or out of the current combination (what plays or is queued); the toggles are committed once, when every key is
 * up. So a key held longer than the window and then a tap is a toggle, never a chord.
 *
 * The window: the key matrix is scanned every ~1.1 ms frame; a press counts after 2 closed frames (1.1-2.2 ms) and a
 * release after 8 open frames (~9 ms) (hal/fm1_input.h). A three-finger press "together" spreads about 20-60 ms
 * (Joe's test: 0 / 20 / 55 ms), two deliberate taps are 100 ms or more apart: 72 ms = 55 ms + the 2.2 ms press
 * debounce + margin, on the main loop's existing fm1_ms clock (no timer).
 * Sections are bits of a 64-bit set (a song has at most 64 sections). */
#ifndef BB_CHORD_H
#define BB_CHORD_H
#include <stdint.h>
#define BB_CHORD_MS 72u
typedef struct {
    uint64_t set, tog;   /* the chord so far; hold-and-add: the sections to toggle */
    uint32_t t0;         /* ms of the first press */
    uint32_t down;       /* gesture keys still held (bit = key) */
    uint8_t active, hold;
} bb_chord;
/* Key `key` (bit in the held mask) for section `s` went down at `now`. */
static inline void bb_chord_press(bb_chord *c, unsigned key, unsigned s, uint32_t now)
{
    uint64_t b = (uint64_t)1 << (s & 63u);
    if (!c->active) { c->active = 1; c->hold = 0; c->t0 = now; c->set = b; c->tog = 0; c->down = 1u << key; return; }
    c->down |= 1u << key;
    if (c->hold) c->tog ^= b; else c->set |= b;
}
/* Every scan: `held` = the gesture keys' levels, `cur` = the current combination. Returns the set to commit, 0 = none. */
static inline uint64_t bb_chord_poll(bb_chord *c, uint32_t held, uint32_t now, uint64_t cur)
{
    if (!c->active) return 0;
    c->down &= held;
    if (!c->hold && (uint32_t)(now - c->t0) < BB_CHORD_MS) return 0;   /* the window is open: more may join */
    if (!c->hold && c->down) { c->hold = 1; c->tog = 0; return c->set; }  /* closed with keys held: commit, then toggles */
    if (c->down) return 0;
    c->active = 0;
    if (!c->hold) return c->set;
    return c->tog && (cur ^ c->tog) ? cur ^ c->tog : 0;
}
#endif
