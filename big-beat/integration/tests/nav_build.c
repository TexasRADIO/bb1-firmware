/* SPDX-License-Identifier: GPL-3.0-only
 * 2.0 kit builder: BUILD and SAVE AS on the real bb_nav.c input function (bb_nav_input), plus a BUILD route matrix.
 * Joe's D1 rules (#64 6554059333): EDIT commits what the screen names; HOME only goes back, one level at a time;
 * OCT pages within a page (BUILD and SAVE AS are one page each, so OCT is dark); K1-K4 do what the bottom of the
 * screen says; the key LEDs follow the key roles.
 *  1. KIT on drum A: black 1 opens BUILD (A_BUILD, bank 1); black 1 again goes back to the list.
 *  2. BUILD keys: white 1-11 and black 2-7 are the 17 lanes (args 0..16), sound keys (REC = accent).
 *  3. BUILD knobs: LEVEL TUNE DECAY VOICE routed to the UK roles; PRESETS = kit (TO_KIT); REC+PRESETS = lane step
 *     (TO_VOICE); REC+ENV = REVERT; OCT does nothing.
 *  4. EDIT in BUILD opens SAVE AS (the label says SAVE AS); in SAVE AS white 1-4 pick the slot, K1/K2 name it, EDIT
 *     stores only when stopped (STOP FIRST otherwise, as SAVE), and the label says STORE.
 *  5. HOME: SAVE AS -> BUILD -> KIT list -> STUDIO, one level per tap. HOME held shows help and changes nothing.
 *  6. Leaving: any page button (ENV, FX, ALGO turn ...) or SELECT leaves BUILD; the next KIT visit opens the list.
 *  7. Drum B, a 909 layer and Dillo have no BUILD: black 1 is dark and does nothing; KIT keeps its list roles.
 *  8. Flag off (BB_WITH_USER_KITS=0) this file checks that KIT is unchanged (black keys dark).
 * Writes the BUILD route matrix as CSV to argv[1] (when given).
 * Build: clang -I firmware/src -DBB_WITH_USER_KITS=1 nav_build.c firmware/src/bb_nav.c */
#include "bb_nav.h"
#include <stdio.h>
#include <string.h>

static uint32_t now_ms = 1000;
static unsigned long checks, failures;
#define CHECK(c, ...) do { checks++; if (!(c)) { failures++; if (failures < 40) { printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); } } } while (0)
#define BIT(b) (1u << (b))
#define KEY(k) (1u << (k))

typedef struct { uint8_t running; } world;
static world w;
static bb_nav_out scan_k(bb_nav_state *s, uint32_t pressed, uint32_t held, uint32_t keys, uint32_t keys_held, int enc, int16_t d)
{
    bb_nav_in in; bb_nav_out o;
    memset(&in, 0, sizeof in); memset(&o, 0, sizeof o);
    now_ms += 20;
    in.pressed = pressed; in.held = held; in.keys = keys; in.keys_held = keys_held; in.now_ms = now_ms;
    in.facts.kit_slot = in.facts.kit_slot_queued = in.facts.section_queued = BB_NAV_NONE_IDX;
    in.facts.list_cursor = in.facts.list_playing = in.facts.list_queued = BB_NAV_NONE_IDX;
    in.facts.section_count = 4; in.facts.list_count = 16; in.facts.step_banks = 2;
    in.facts.running = w.running;
    if (enc >= 0) in.enc[enc] = d;
    bb_nav_input(s, &in, &o);
    return o;
}
static bb_nav_out tap(bb_nav_state *s, uint8_t b)        /* press and release a button */
{
    bb_nav_out o = scan_k(s, BIT(b), BIT(b), 0, 0, -1, 0), r = scan_k(s, 0, 0, 0, 0, -1, 0);
    o.actions |= r.actions; o.page = r.page; o.bank = r.bank; if (r.message) o.message = r.message;
    o.page_changed |= r.page_changed;
    return o;
}
static bb_nav_out key(bb_nav_state *s, uint8_t k, int rec)
{
    uint32_t mod = rec ? BIT(BB_NAV_B_REC) : 0;
    bb_nav_out o = scan_k(s, 0, mod, KEY(k), KEY(k), -1, 0), r = scan_k(s, 0, mod, 0, 0, -1, 0);
    o.actions |= r.actions; o.bank = r.bank; o.page = r.page;
    return o;
}
static bb_nav_out turn(bb_nav_state *s, int enc, int16_t d, int rec)
{
    uint32_t mod = rec ? BIT(BB_NAV_B_REC) : 0;
    now_ms += 400;                                            /* past the encoder latch */
    return scan_k(s, 0, mod, 0, 0, enc, d);
}
static void at_kit(bb_nav_state *s, uint8_t instr, uint8_t focus)
{
    bb_nav_out o;
    bb_nav_init(s, BB_NAV_STUDIO, 600);
    bb_nav_set_instrument(s, instr, instr == BB_NAV_INSTR_DILLO ? 1 : 2);
    bb_nav_set_focus(s, focus);
    memset(&o, 0, sizeof o); bb_nav_goto(s, BB_NAV_KIT, &o);
}
static const char *label(const bb_nav_state *s, unsigned k)
{
    uint8_t r[4]; const char *l[4];
    bb_nav_knobs(s, r, l);
    return l[k];
}
static uint8_t role(const bb_nav_state *s, unsigned k)
{
    uint8_t r[4]; const char *l[4];
    bb_nav_knobs(s, r, l);
    return r[k];
}

#if BB_WITH_USER_KITS
static const char *rname(uint8_t r)
{
    return r == BB_NAV_K_NONE ? "-" : r == BB_NAV_K_PICK ? "PICK" : r == BB_NAV_K_BUILD ? "BUILD" : r == BB_NAV_K_LANE ? "LANE" :
           r == BB_NAV_K_UK_SLOT ? "USER SLOT" : "?";
}
static void build_routes(FILE *csv)
{
    static const char *const BANK[3] = {"LIST", "BUILD", "SAVE AS"};
    bb_nav_state s; bb_nav_facts f; bb_nav_led_out l;
    if (csv) fprintf(csv, "state,instrument,page,bank_name,K1,K2,K3,K4,EDIT,black1,white1,black2,white5,OCT,HOME_tap_to\n");
    for (unsigned focus = 0; focus < 3; focus++)
        for (unsigned bank = 0; bank < 3; bank++) {
            uint8_t instr = focus < 2 ? BB_NAV_INSTR_DRUM : BB_NAV_INSTR_DILLO, a;
            at_kit(&s, instr, (uint8_t)focus);
            if (bank) { if (focus) continue; key(&s, BB_NAV_BLACK(1), 0); }
            if (bank == 2) tap(&s, BB_NAV_B_EDIT);
            CHECK(s.bank == bank, "reached bank %u (%u)", bank, s.bank);
            memset(&f, 0, sizeof f); f.list_cursor = 0; f.list_count = 17;
            bb_nav_leds(&s, &f, 0, &l);
            bb_nav_state h = s; bb_nav_out o = tap(&h, BB_NAV_B_OCTUP);
            int oct = o.oct != BB_NAV_OCT_NONE;
            bb_nav_state g = s; tap(&g, BB_NAV_B_HOME);
            if (csv)
                fprintf(csv, "%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s\n", BANK[bank],
                        focus == 0 ? "DRUM A" : focus == 1 ? "DRUM B" : "DILLO", bb_nav_page_name(&s), bb_nav_bank_name(&s),
                        label(&s, 0), label(&s, 1), label(&s, 2), label(&s, 3), bb_nav_edit_label(&s, &f),
                        rname(bb_nav_key_role(&s, BB_NAV_BLACK(1), 0, &a)), rname(bb_nav_key_role(&s, BB_NAV_WHITE(1), 0, &a)),
                        rname(bb_nav_key_role(&s, BB_NAV_BLACK(2), 0, &a)), rname(bb_nav_key_role(&s, BB_NAV_WHITE(5), 0, &a)),
                        oct ? "PAGES" : "DARK", g.page == BB_NAV_KIT ? (g.bank ? BANK[g.bank] : "KIT LIST") : g.page == BB_NAV_STUDIO ? "STUDIO" : "?");
            /* every lit key has a role, every role is lit (LED table = key legend) */
            for (unsigned k = 0; k < BB_NAV_KEYS; k++) {
                uint8_t r = bb_nav_key_role(&s, (uint8_t)k, 0, &a);
                CHECK((r == BB_NAV_K_NONE) == (l.key[k] == BB_NAV_LED_DARK), "bank %u focus %u key %u role %u led %u", bank, focus, k, r, l.key[k]);
            }
        }
}
#endif

int main(int argc, char **argv)
{
    bb_nav_state s; bb_nav_out o; uint8_t a;
    FILE *csv = argc > 1 ? fopen(argv[1], "w") : NULL;
#if BB_WITH_USER_KITS
    /* 1. black 1 on KIT, drum A */
    at_kit(&s, BB_NAV_INSTR_DRUM, 0);
    CHECK(bb_nav_key_role(&s, BB_NAV_BLACK(1), 0, &a) == BB_NAV_K_BUILD, "KIT black 1 = BUILD");
    CHECK(bb_nav_key_role(&s, BB_NAV_WHITE(3), 0, &a) == BB_NAV_K_PICK && a == 2, "KIT white keys still pick");
    o = key(&s, BB_NAV_BLACK(1), 0);
    CHECK((o.actions & BB_NAV_A_BUILD) && s.page == BB_NAV_KIT && s.bank == BB_NAV_KIT_BUILD, "black 1 opens BUILD");
    CHECK(!strcmp(bb_nav_bank_name(&s), "BUILD"), "bank name BUILD");
    CHECK(o.message == BB_NAV_MSG_BUILD, "BUILD message");
    /* 2. lanes */
    for (unsigned n = 1; n <= 16; n++) {
        uint8_t r = bb_nav_key_role(&s, BB_NAV_WHITE(n), 0, &a);
        CHECK(n <= 11 ? (r == BB_NAV_K_LANE && a == n - 1) : r == BB_NAV_K_NONE, "white %u role %u arg %u", n, r, a);
    }
    for (unsigned n = 1; n <= 11; n++) {
        uint8_t r = bb_nav_key_role(&s, BB_NAV_BLACK(n), 0, &a);
        CHECK(n == 1 ? r == BB_NAV_K_BUILD : n <= 7 ? (r == BB_NAV_K_LANE && a == n + 9) : r == BB_NAV_K_NONE,
              "black %u role %u arg %u", n, r, a);
    }
    CHECK(bb_nav_key_is_sound(BB_NAV_K_LANE), "lanes are sound keys");
    o = scan_k(&s, 0, 0, KEY(BB_NAV_WHITE(2)), KEY(BB_NAV_WHITE(2)), -1, 0);
    CHECK(o.kev_count == 1 && o.kev[0].role == BB_NAV_K_LANE && o.kev[0].arg == 1 && !o.kev[0].flags, "white 2 = lane SD");
    scan_k(&s, 0, 0, 0, 0, -1, 0);
    o = scan_k(&s, 0, BIT(BB_NAV_B_REC), KEY(BB_NAV_BLACK(7)), KEY(BB_NAV_BLACK(7)), -1, 0);
    CHECK(o.kev_count == 1 && o.kev[0].role == BB_NAV_K_LANE && o.kev[0].arg == 16 && (o.kev[0].flags & BB_NAV_KF_ACCENT),
          "REC + black 7 = lane RD, accented");
    scan_k(&s, 0, 0, 0, 0, -1, 0);
    /* 3. knobs and encoders */
    CHECK(role(&s, 0) == BB_NAV_R_UK_LEVEL && role(&s, 1) == BB_NAV_R_UK_TUNE && role(&s, 2) == BB_NAV_R_UK_DECAY &&
          role(&s, 3) == BB_NAV_R_UK_VOICE, "K1-K4 roles");
    CHECK(!strcmp(label(&s, 0), "LEVEL") && !strcmp(label(&s, 1), "TUNE") && !strcmp(label(&s, 2), "DECAY") &&
          !strcmp(label(&s, 3), "VOICE"), "K1-K4 labels");
    o = turn(&s, BB_NAV_E_K4, 1, 0);
    CHECK(o.enc[BB_NAV_E_K4].target == BB_NAV_TO_KNOB && o.enc[BB_NAV_E_K4].role == BB_NAV_R_UK_VOICE, "K4 turns the voice");
    o = turn(&s, BB_NAV_E_K2, -2, 0);
    CHECK(o.enc[BB_NAV_E_K2].target == BB_NAV_TO_KNOB && o.enc[BB_NAV_E_K2].role == BB_NAV_R_UK_TUNE && o.enc[BB_NAV_E_K2].delta == -2,
          "K2 turns the tune");
    o = turn(&s, BB_NAV_E_PRESET, 1, 0);
    CHECK(o.enc[BB_NAV_E_PRESET].target == BB_NAV_TO_KIT && s.page == BB_NAV_KIT && s.bank == BB_NAV_KIT_BUILD, "PRESETS = the lane's kit, stays in BUILD");
    o = turn(&s, BB_NAV_E_PRESET, 1, 1);
    CHECK(o.enc[BB_NAV_E_PRESET].target == BB_NAV_TO_VOICE, "REC+PRESETS steps the lane");
    scan_k(&s, 0, 0, 0, 0, -1, 0);
    o = scan_k(&s, BIT(BB_NAV_B_ENV), BIT(BB_NAV_B_REC) | BIT(BB_NAV_B_ENV), 0, 0, -1, 0);
#ifdef BB_NAV_HAS_P1   /* 2.1 P1: a drum voice has no revert (the firmware only said NOT IN THIS BUILD): dark, same words */
    CHECK(!(o.actions & BB_NAV_A_REVERT) && o.message == BB_NAV_MSG_NOT_IN_BUILD && s.page == BB_NAV_KIT && s.bank == BB_NAV_KIT_BUILD,
          "REC+ENV on drums: not in this build, stays");
#else
    CHECK((o.actions & BB_NAV_A_REVERT) && s.page == BB_NAV_KIT && s.bank == BB_NAV_KIT_BUILD, "REC+ENV = revert, stays");
#endif
    scan_k(&s, 0, 0, 0, 0, -1, 0);
    o = tap(&s, BB_NAV_B_OCTUP);
    CHECK(o.oct == BB_NAV_OCT_NONE && s.bank == BB_NAV_KIT_BUILD, "OCT dark in BUILD");
    /* 4. EDIT -> SAVE AS -> STORE */
    { bb_nav_facts f; memset(&f, 0, sizeof f); CHECK(!strcmp(bb_nav_edit_label(&s, &f), "SAVE AS"), "EDIT says SAVE AS"); }
    o = tap(&s, BB_NAV_B_EDIT);
    CHECK((o.actions & BB_NAV_A_BUILD) && s.bank == BB_NAV_KIT_SAVE_AS && !(o.actions & BB_NAV_A_KIT_CONFIRM), "EDIT opens SAVE AS");
    CHECK(!strcmp(bb_nav_bank_name(&s), "SAVE AS") && role(&s, 0) == BB_NAV_R_NONE && role(&s, 1) == BB_NAV_R_NONE &&
          role(&s, 2) == BB_NAV_R_NONE && role(&s, 3) == BB_NAV_R_NONE, "SAVE AS knobs");
    for (unsigned n = 1; n <= 16; n++) {
        uint8_t r = bb_nav_key_role(&s, BB_NAV_WHITE(n), 0, &a);
        CHECK(n <= 4 ? (r == BB_NAV_K_UK_SLOT && a == n - 1) : r == BB_NAV_K_NONE, "SAVE AS white %u", n);
    }
    CHECK(bb_nav_key_role(&s, BB_NAV_BLACK(1), 0, &a) == BB_NAV_K_NONE, "black 1 dark in SAVE AS");
    { bb_nav_facts f; memset(&f, 0, sizeof f); CHECK(!strcmp(bb_nav_edit_label(&s, &f), "STORE"), "EDIT says STORE");
      f.running = 1; CHECK(!strcmp(bb_nav_edit_label(&s, &f), ""), "no STORE while running"); }
    w.running = 1; o = tap(&s, BB_NAV_B_EDIT); w.running = 0;
    CHECK(!(o.actions & BB_NAV_A_UK_STORE) && o.message == BB_NAV_MSG_STOP_FIRST, "STORE refused while running");
    o = tap(&s, BB_NAV_B_EDIT);
    CHECK((o.actions & BB_NAV_A_UK_STORE) && s.bank == BB_NAV_KIT_SAVE_AS, "EDIT stores, stopped");
    /* 5. HOME back one level at a time */
    o = tap(&s, BB_NAV_B_HOME);
    CHECK((o.actions & BB_NAV_A_BUILD) && s.page == BB_NAV_KIT && s.bank == BB_NAV_KIT_BUILD, "HOME: SAVE AS -> BUILD");
    { /* HOME held: help, nothing else */
        bb_nav_state h = s;
        scan_k(&h, BIT(BB_NAV_B_HOME), BIT(BB_NAV_B_HOME), 0, 0, -1, 0);
        for (int t = 0; t < 40; t++) scan_k(&h, 0, BIT(BB_NAV_B_HOME), 0, 0, -1, 0);
        CHECK(h.help && h.page == BB_NAV_KIT && h.bank == BB_NAV_KIT_BUILD, "HOME held = help in BUILD");
        scan_k(&h, 0, 0, 0, 0, -1, 0);
        CHECK(!h.help && h.page == BB_NAV_KIT && h.bank == BB_NAV_KIT_BUILD, "letting go of HOME returns to BUILD");
    }
    o = tap(&s, BB_NAV_B_HOME);
    CHECK((o.actions & BB_NAV_A_BUILD) && s.page == BB_NAV_KIT && s.bank == BB_NAV_KIT_LIST, "HOME: BUILD -> KIT list");
    o = tap(&s, BB_NAV_B_HOME);
    CHECK(s.page == BB_NAV_STUDIO, "HOME: KIT list -> STUDIO");
    /* black 1 toggles back to the list too */
    at_kit(&s, BB_NAV_INSTR_DRUM, 0); key(&s, BB_NAV_BLACK(1), 0);
    o = key(&s, BB_NAV_BLACK(1), 0);
    CHECK((o.actions & BB_NAV_A_BUILD) && s.bank == BB_NAV_KIT_LIST, "black 1 again: back to the list");
    /* 6. leaving */
    static const uint8_t leave[] = {BB_NAV_B_ENV, BB_NAV_B_FX, BB_NAV_B_SAVE, BB_NAV_B_GLO, BB_NAV_B_ARP, BB_NAV_B_SEQ, BB_NAV_B_LFO};
    for (unsigned i = 0; i < sizeof leave; i++) {
        at_kit(&s, BB_NAV_INSTR_DRUM, 0); key(&s, BB_NAV_BLACK(1), 0);
        tap(&s, leave[i]);
        CHECK(s.page != BB_NAV_KIT && s.bank == 0, "button %u leaves BUILD", leave[i]);
        o = turn(&s, BB_NAV_E_PRESET, 1, 0);
        CHECK(s.page == BB_NAV_KIT && s.bank == BB_NAV_KIT_LIST, "back on KIT: the list (button %u)", leave[i]);
    }
    at_kit(&s, BB_NAV_INSTR_DRUM, 0); key(&s, BB_NAV_BLACK(1), 0);
    o = turn(&s, BB_NAV_E_ALGO, 1, 0);
    CHECK(s.page == BB_NAV_PATTERN && s.bank == 0, "ALGO leaves BUILD for PATTERN");
    at_kit(&s, BB_NAV_INSTR_DRUM, 0); key(&s, BB_NAV_BLACK(1), 0);
    o = turn(&s, BB_NAV_E_SELECT, 1, 0);
    CHECK(o.enc[BB_NAV_E_SELECT].target == BB_NAV_TO_INSTRUMENT && (o.actions & BB_NAV_A_BUILD) && s.bank == BB_NAV_KIT_LIST,
          "SELECT leaves BUILD (focus moves)");
    at_kit(&s, BB_NAV_INSTR_DRUM, 0); key(&s, BB_NAV_BLACK(1), 0);
    o = scan_k(&s, BIT(BB_NAV_B_PLAY), BIT(BB_NAV_B_PLAY), 0, 0, -1, 0);
    CHECK((o.actions & BB_NAV_A_TRANSPORT) && s.bank == BB_NAV_KIT_BUILD, "PLAY is transport and stays in BUILD");
    /* 7. no BUILD on drum B, a 909 layer or Dillo */
    static const struct { uint8_t instr, focus; } none[] = {{BB_NAV_INSTR_DRUM, 1}, {BB_NAV_INSTR_DRUM_909, 0}, {BB_NAV_INSTR_DILLO, 2}};
    for (unsigned i = 0; i < 3; i++) {
        at_kit(&s, none[i].instr, none[i].focus);
        /* 2.1 P2: on the Dillo PATCH page every key is a note; still no BUILD */
        CHECK(bb_nav_key_role(&s, BB_NAV_BLACK(1), 0, &a) == (none[i].instr == BB_NAV_INSTR_DILLO ? BB_NAV_K_NOTE : BB_NAV_K_NONE),
              "no BUILD role (case %u)", i);
        o = key(&s, BB_NAV_BLACK(1), 0);
        CHECK(!(o.actions & BB_NAV_A_BUILD) && s.bank == 0, "black 1 does nothing (case %u)", i);
        if (none[i].instr != BB_NAV_INSTR_DILLO) CHECK(bb_nav_key_role(&s, BB_NAV_WHITE(1), 0, &a) == BB_NAV_K_PICK, "list keys (case %u)", i);
    }
    build_routes(csv);
#else
    at_kit(&s, BB_NAV_INSTR_DRUM, 0);
    CHECK(bb_nav_key_role(&s, BB_NAV_BLACK(1), 0, &a) == BB_NAV_K_NONE, "flag off: KIT black 1 is dark");
    o = key(&s, BB_NAV_BLACK(1), 0);
    CHECK(s.bank == 0 && !o.actions, "flag off: black 1 does nothing");
    (void)turn; (void)tap; (void)label; (void)role;
#endif
    if (csv) fclose(csv);
    printf("nav_build (BB_WITH_USER_KITS=%d): %lu checks, %lu failures\n", BB_WITH_USER_KITS, checks, failures);
    return failures != 0;
}
