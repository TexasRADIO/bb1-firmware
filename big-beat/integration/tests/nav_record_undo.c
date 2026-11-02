/* SPDX-License-Identifier: GPL-3.0-only
 * 2.0 series: REC+EDIT is Undo and REC+SEQ records, on the real bb_nav.c.
 * 1. REC+EDIT on PLAY (pads and steps, every instrument) asks for UNDO, never MUTE, and stays on the page.
 * 2. REC+EDIT on every other page still asks for MUTE (the other mute routes are unchanged).
 * 3. REC+SEQ on PLAY/STEPS with a drum kit in focus asks for RECORD: no bank change, no page change.
 *    (Checked when the tree defines BB_NAV_HAS_RECORD, or with -DEXPECT_RECORD to show the old tree fails.)
 * 4. REC+SEQ with Dillo in focus, or from another page, keeps its old meaning (STEPS, next bank).
 * Fails on the candidate before the series: there REC+EDIT is MUTE on PLAY and REC+SEQ is "next bank".
 * Build: clang -I firmware/src nav_record_undo.c firmware/src/bb_nav.c */
#include "bb_nav.h"
#include <stdio.h>
#include <string.h>
/* On a tree without the new actions, use their bit values so the test compiles there and FAILS (discrimination). */
#ifndef BB_NAV_HAS_UNDO
#define BB_NAV_A_UNDO (1u << 24)
#endif
#if !defined(BB_NAV_HAS_RECORD) && defined(EXPECT_RECORD)
#define BB_NAV_A_RECORD (1u << 25)
#endif

static uint32_t now_ms = 1000;
static unsigned long checks, failures;
#define CHECK(c, ...) do { checks++; if (!(c)) { failures++; if (failures < 30) { printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); } } } while (0)
#define BIT(b) (1u << (b))

static bb_nav_out scan(bb_nav_state *s, uint32_t pressed, uint32_t held, uint32_t dt)
{
    bb_nav_in in; bb_nav_out o;
    memset(&in, 0, sizeof in); memset(&o, 0, sizeof o);
    now_ms += dt;
    in.pressed = pressed; in.held = held; in.now_ms = now_ms;
    in.facts.kit_slot = in.facts.kit_slot_queued = in.facts.section_queued = BB_NAV_NONE_IDX;
    in.facts.list_cursor = in.facts.list_playing = in.facts.list_queued = BB_NAV_NONE_IDX;
    in.facts.section_count = 4; in.facts.list_count = 16; in.facts.step_banks = 2;
    bb_nav_input(s, &in, &o);
    return o;
}
static void fresh(bb_nav_state *s, uint8_t page, uint8_t instr, uint8_t keys)
{
    bb_nav_out o;
    bb_nav_init(s, BB_NAV_STUDIO, 600);
    bb_nav_set_instrument(s, instr, instr == BB_NAV_INSTR_DILLO ? 1 : 2);
    bb_nav_set_focus(s, instr == BB_NAV_INSTR_DILLO ? 2 : 0);
    memset(&o, 0, sizeof o); bb_nav_goto(s, page, &o);
    s->play_keys = keys;
}
/* REC held, then the button pressed while REC is still held (a quick chord) */
static bb_nav_out chord(bb_nav_state *s, uint8_t button)
{
    const uint32_t REC = BIT(BB_NAV_B_REC), B = BIT(button);
    bb_nav_out o, all;
    memset(&all, 0, sizeof all);
    o = scan(s, REC, REC, 10); all.actions |= o.actions;
    o = scan(s, B, REC | B, 10); all.actions |= o.actions; all.page = o.page;
    o = scan(s, 0, REC, 10); all.actions |= o.actions;
    o = scan(s, 0, 0, 10); all.actions |= o.actions; all.page = o.page;
    return all;
}

int main(void)
{
    static const uint8_t instr[] = {BB_NAV_INSTR_DRUM, BB_NAV_INSTR_DILLO};
    for (unsigned i = 0; i < 2; i++)
        for (uint8_t keys = 0; keys < 2; keys++) {
            bb_nav_state s; bb_nav_out o;
            fresh(&s, BB_NAV_PLAY, instr[i], keys);
            uint8_t bank = s.bank;
            o = chord(&s, BB_NAV_B_EDIT);
            CHECK(o.actions & BB_NAV_A_UNDO, "PLAY instr %u keys %u: REC+EDIT must ask for UNDO", instr[i], keys);
            CHECK(!(o.actions & BB_NAV_A_MUTE), "PLAY instr %u keys %u: REC+EDIT must not mute", instr[i], keys);
            CHECK(s.page == BB_NAV_PLAY && s.play_keys == keys && s.bank == bank, "PLAY instr %u keys %u: REC+EDIT moved", instr[i], keys);
        }
    for (uint8_t page = 0; page < BB_NAV_PAGES; page++) {
        if (page == BB_NAV_PLAY) continue;
        bb_nav_state s; bb_nav_out o;
        fresh(&s, page, BB_NAV_INSTR_DRUM, 0);
        o = chord(&s, BB_NAV_B_EDIT);
        CHECK(o.actions & BB_NAV_A_MUTE, "page %u: REC+EDIT keeps MUTE", page);
        CHECK(!(o.actions & BB_NAV_A_UNDO), "page %u: REC+EDIT must not UNDO", page);
    }
#if defined(BB_NAV_HAS_RECORD) || defined(EXPECT_RECORD)
    for (uint8_t keys = 0; keys < 2; keys++) {
        bb_nav_state s; bb_nav_out o;
        fresh(&s, BB_NAV_PLAY, BB_NAV_INSTR_DRUM, keys);
        s.bank = 0;
        o = chord(&s, BB_NAV_B_SEQ);
        CHECK(o.actions & BB_NAV_A_RECORD, "drums keys %u: REC+SEQ must ask for RECORD", keys);
        CHECK(!(o.actions & BB_NAV_A_STEP_BANK_NEXT), "drums keys %u: REC+SEQ must not page the bank", keys);
        CHECK(s.page == BB_NAV_PLAY && s.play_keys == keys, "drums keys %u: REC+SEQ changed the view", keys);
    }
    {
        bb_nav_state s; bb_nav_out o;
        fresh(&s, BB_NAV_PLAY, BB_NAV_INSTR_DILLO, BB_NAV_PLAY_STEPS);
        o = chord(&s, BB_NAV_B_SEQ);
        CHECK(!(o.actions & BB_NAV_A_RECORD) && (o.actions & BB_NAV_A_STEP_BANK_NEXT), "Dillo: REC+SEQ keeps next bank");
        fresh(&s, BB_NAV_STUDIO, BB_NAV_INSTR_DRUM, 0);
        o = chord(&s, BB_NAV_B_SEQ);
        CHECK(!(o.actions & BB_NAV_A_RECORD) && s.page == BB_NAV_PLAY && s.play_keys == BB_NAV_PLAY_STEPS,
              "STUDIO: REC+SEQ still opens STEPS");
    }
    printf("nav record/undo: REC+SEQ records, ");
#else
    printf("nav record/undo: (no RECORD in this tree), ");
#endif
    printf("%lu checks, %lu failures\n", checks, failures);
    return failures != 0;
}
