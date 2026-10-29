/* SPDX-License-Identifier: GPL-3.0-only
 * SAVE into a scene slot that holds other data (BB1 1.2, save-fix r1).
 *
 * A unit that ran the stock M-VAVE firmware arrives with its own bytes in the
 * four scene slots (0x97000..0x9EFFF). They are not FELU commit records, so
 * BB1 1.1 refused every SAVE there ("PROTECTED / NOT BIG BEAT"). That guard
 * stays. This file adds ONE explicit way past it, for SAVE only:
 *
 *   1. EDIT (STORE) on such a slot writes nothing. It arms a claim for that
 *      slot and the SAVE page says "SCENE n HAS OTHER DATA / HOLD SAVE TO
 *      REPLACE".
 *   2. Holding SAVE for BB_CLAIM_HOLD_MS, a press that began AFTER the claim
 *      was armed, on the SAVE page, on the same slot, with no other button
 *      or key held, issues one save with claim=1. Anything else (leaving
 *      the page, another slot, another button, letting go early, waiting
 *      BB_CLAIM_WINDOW_MS) writes nothing; leaving or another slot disarms.
 *   3. The save itself is the unchanged st_save() (erase the other copy,
 *      payload, commit record last, read back).
 *
 * What never changes: LOAD of other data is refused; a slot with any FELU
 * commit record (valid or damaged) keeps the 1.1 behaviour; blank and Big
 * Beat slots save as before with no prompt.
 *
 * Pure: no globals, no flash, no I/O. Host-tested in
 * claude-code/save-fix/tests (big-beat-fm1-review). Unity-build header. */
#ifndef BB_SAVE_CLAIM_H
#define BB_SAVE_CLAIM_H
#include <stdint.h>

/* slot classes (bb_session_slot_state) */
enum {
    BB_SLOT_OTHER = -2,       /* no FELU record in either copy, a non-erased header: other data */
    BB_SLOT_PROTECTED = -1,   /* FELU-owned but not loadable as Big Beat, damaged FELU, read error */
    BB_SLOT_BLANK = 0,        /* both commit headers erased */
    BB_SLOT_BIG_BEAT = 1      /* a committed copy that decodes as a Big Beat scene */
};

/* what one session job may do with a slot */
enum {
    BB_GATE_PROTECT,          /* refuse: "PROTECTED / NOT BIG BEAT", no flash write */
    BB_GATE_ASK,              /* SAVE, other data, no claim: arm the claim, no flash write */
    BB_GATE_STORE,            /* SAVE: st_save() */
    BB_GATE_EMPTY,            /* LOAD of a blank slot */
    BB_GATE_LOAD              /* LOAD of a Big Beat scene */
};

static int bb_save_gate(int state, int load, int claimed)
{
    switch (state) {
    case BB_SLOT_BLANK: return load ? BB_GATE_EMPTY : BB_GATE_STORE;
    case BB_SLOT_BIG_BEAT: return load ? BB_GATE_LOAD : BB_GATE_STORE;
    case BB_SLOT_OTHER:
        if (load) return BB_GATE_PROTECT;              /* never interpret other data */
        return claimed ? BB_GATE_STORE : BB_GATE_ASK;
    default: return BB_GATE_PROTECT;                  /* PROTECTED and anything unknown */
    }
}

#define BB_CLAIM_HOLD_MS 2000u      /* SAVE held this long replaces the slot */
#define BB_CLAIM_WINDOW_MS 15000u   /* the hold must begin within this long of the prompt */

typedef struct {
    uint8_t slot;                   /* armed slot + 1; 0 = not armed */
    uint8_t down;                   /* a SAVE press that began after arming is held */
    uint32_t armed_at, down_at;
} bb_save_claim;

static void bb_claim_clear(bb_save_claim *c) { c->slot = 0; c->down = 0; }

static void bb_claim_arm(bb_save_claim *c, unsigned slot, uint32_t now)
{
    c->slot = (uint8_t)(slot + 1u);
    c->down = 0;
    c->armed_at = now;
    c->down_at = now;
}

/* armed for this slot (the SAVE page shows the prompt) */
static int bb_claim_armed(const bb_save_claim *c, unsigned slot) { return c->slot && c->slot == slot + 1u; }

/* Once per input scan. save_edge: SAVE went down this scan; save_held: SAVE
 * is down now; others: any other button or key is down. Returns 1 exactly
 * once, when a qualifying hold completes; the claim is then disarmed. */
static __attribute__((noinline)) int bb_claim_scan(bb_save_claim *c, uint32_t now, int on_save_page, unsigned slot,
                         int save_edge, int save_held, int others)
{
    if (!c->slot)
        return 0;
    if (!on_save_page || c->slot != slot + 1u) {       /* left the page or chose another slot */
        bb_claim_clear(c);
        return 0;
    }
    if (others) {                                      /* REC+SAVE, keys, ... never confirm */
        c->down = 0;
        return 0;
    }
    if (!c->down) {
        if ((uint32_t)(now - c->armed_at) >= BB_CLAIM_WINDOW_MS) {
            bb_claim_clear(c);
            return 0;
        }
        if (save_edge && save_held) {
            c->down = 1;
            c->down_at = now;
        }
        return 0;
    }
    if (!save_held) {                                  /* let go early: still armed, nothing written */
        c->down = 0;
        return 0;
    }
    if ((uint32_t)(now - c->down_at) >= BB_CLAIM_HOLD_MS) {
        bb_claim_clear(c);
        return 1;
    }
    return 0;
}

/* SAVE page text while armed */
static const char *bb_claim_prompt(const bb_save_claim *c)
{
    return c->down ? "KEEP HOLDING SAVE" : "HOLD SAVE TO REPLACE";
}
#endif
