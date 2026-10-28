/* SPDX-License-Identifier: GPL-3.0-only
 * Drum-layer switch-on bound to the applied outcome (r9.8; Duke, #4 6469741984).
 * Confirming a kit or pattern on a drum layer that is off switches it on (Joe, #4 6469173268), but only
 * once the audio owner has APPLIED that choice. Pure decision logic: no globals, no commands, host-tested
 * by claude-code/integration/tests/test_nav_arm.c. The firmware glue is in bb_nav_firmware.inc. */
#ifndef BB_NAV_ARM_H
#define BB_NAV_ARM_H
#include <stdint.h>

enum { BB_NAV_ARM_NONE, BB_NAV_ARM_PATTERN, BB_NAV_ARM_SOUND };
enum { BB_NAV_ARM_WAIT, BB_NAV_ARM_ENABLE, BB_NAV_ARM_DROP };     /* bb_nav_arm_step() verdicts */
typedef struct { uint8_t kind; uint32_t ticket; } bb_nav_arm_state;

/* What the main loop observes for one drum layer, each scan. */
typedef struct {
    uint32_t request_ticket;   /* the layer's current pattern request (0 = none)            */
    uint32_t status_seen;      /* the ticket the audio owner has reported on                */
    uint8_t  status_applied;   /* that report is BB_PATTERN_APPLIED (else REJECTED/CANCELLED) */
    uint8_t  pick_pending;     /* the sound picker still has a request in flight            */
    uint8_t  pick_this_layer;  /* the picker's request is for this layer                    */
    uint8_t  pick_applied;     /* the picker settled on APPLIED or SAME (else FAILED/SUPERSEDED) */
} bb_nav_arm_view;

/* Arm after an ACCEPTED pattern confirmation; a refused/not-ready/busy one must not call this. */
static inline void bb_nav_arm_on_pattern(bb_nav_arm_state *s, int layer_enabled, uint32_t ticket)
{
    if (!layer_enabled && ticket) { s->kind = BB_NAV_ARM_PATTERN; s->ticket = ticket; }
}
/* Arm after an ACCEPTED native-kit confirmation that queued a sound request. */
static inline void bb_nav_arm_on_sound(bb_nav_arm_state *s, int layer_enabled)
{
    if (!layer_enabled) { s->kind = BB_NAV_ARM_SOUND; s->ticket = 0; }
}
/* One scan: WAIT, ENABLE (the choice was applied: switch the layer on) or DROP (it never will be). */
static inline int bb_nav_arm_step(bb_nav_arm_state *s, const bb_nav_arm_view *v)
{
    int verdict = BB_NAV_ARM_WAIT;
    if (s->kind == BB_NAV_ARM_PATTERN) {
        if (v->request_ticket != s->ticket) verdict = BB_NAV_ARM_DROP;            /* superseded by a newer request */
        else if (v->status_seen == s->ticket) verdict = v->status_applied ? BB_NAV_ARM_ENABLE : BB_NAV_ARM_DROP;
    } else if (s->kind == BB_NAV_ARM_SOUND && !v->pick_pending) {
        verdict = v->pick_this_layer && v->pick_applied ? BB_NAV_ARM_ENABLE : BB_NAV_ARM_DROP;
    }
    if (verdict != BB_NAV_ARM_WAIT) s->kind = BB_NAV_ARM_NONE;
    return verdict;
}
#endif
