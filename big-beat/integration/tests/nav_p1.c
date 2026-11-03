/* SPDX-License-Identifier: GPL-3.0-only
 * 2.1 Live Play P1 (spec claude/2.1/live-play/SPEC.md 8.1.11-8.1.13): one table drives key roles, button roles,
 * LEDs, the legend, the EDIT caption and HELP, on the real bb_nav.c. For every page x bank x focus (DRUM A, DRUM B,
 * DILLO) x REC x EVOLVE mode x three fact sets:
 *  a. a key or button with no role is dark; a lit one has a role
 *  b. pressing a dark key or button gives no key event, no action, no page / bank / play-keys change
 *  c. the legend names exactly the keys that have a role (its words come from the same table)
 *  d. HELP has a row for every key with a role, every lit button and the K1-K4 labels
 *  e. HELP keeps every light (HOME bright); entering and leaving HELP emits nothing
 *  f. with REC held, LEDs and legend use the REC roles
 *  g. the dry-run of the input path (the 2.0 LED source) agrees with the resolver: an effect => lit; dark => no effect
 *  h. SONG and MIDI (GLOBAL banks) pass a-g
 * and the P1 behaviours: LFO opens EVOLVE then steps the modes; EDIT reads CAPTURE / APPLY; black 3 = LFO there;
 * EVOLVE keys light what is selected; FX white 1-5 choose ECHO..SENDS, FX reopens on the last tab, EDIT does nothing
 * on FX; GLOBAL white 1/2 open MIDI / CLOCK, white 4 asks for SONG, white 5 only with drums; SONG keys, REC tap,
 * HOME back; press-to-explain; OCT pages HELP; PLAY still works under HELP.
 * Build: clang -DBB_WITH_USER_KITS=1 -I firmware/src nav_p1.c firmware/src/bb_nav.c */
#include "bb_nav.h"
#include <stdio.h>
#include <string.h>

static uint32_t now_ms = 1000;
static unsigned long checks, failures;
#define CHECK(c, ...) do { checks++; if (!(c)) { failures++; if (failures < 40) { printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); } } } while (0)
#define BIT(b) (1u << (b))
static const char *const PG[] = {"STUDIO", "PLAY", "KIT", "PATTERN", "SOUND", "FX", "SAVE", "EVOLVE", "GLOBAL", "SONG"};

static void facts_set(bb_nav_facts *f, int which)
{
    memset(f, 0, sizeof *f);
    f->kit_slot = f->kit_slot_queued = f->section_queued = BB_NAV_NONE_IDX;
    f->list_cursor = f->list_playing = f->list_queued = BB_NAV_NONE_IDX;
    f->section = BB_NAV_NONE_IDX;
    if (which == 1) {        /* a busy, playing state */
        f->running = 1; f->section = 1; f->section_count = 6; f->queued = BB_NAV_Q_SECTION; f->section_queued = 3;
        f->list_count = 9; f->list_cursor = 3; f->list_playing = 1; f->step_banks = 2; f->steps = 0x1111;
        f->evo_parts = 5; f->evo_moves = 3; f->gen_voice = 4; f->instr_muted = 2; f->group_muted = 1;
        f->song = BB_NAV_SONG_F_ROWS | BB_NAV_SONG_F_UNDO | BB_NAV_SONG_F_DILLO;
        f->home_take = 5; f->home_coll = 1;
    } else if (which == 2) { /* stopped, a delete waiting, redo, last bank */
        f->section_count = 14; f->section = 12; f->list_count = 16; f->list_cursor = 0; f->list_playing = 0;
        f->step_bank = 1; f->step_banks = 2; f->steps = 0xffff; f->evo_parts = 2; f->evo_moves = 8;
        f->song = BB_NAV_SONG_F_ROWS | BB_NAV_SONG_F_REDO | BB_NAV_SONG_F_DELETE; f->swing_on = 1; f->evolve_on = 1;
        f->home_take = 4; f->home_coll = 2;   /* PRESETS: A and B available, Dillo selected */
    }
}
static void state(bb_nav_state *s, unsigned page, unsigned bank, unsigned row, unsigned keys, unsigned mode)
{
    bb_nav_init(s, BB_NAV_STUDIO, 600);
    bb_nav_set_features(s, BB_NAV_F_TAP | BB_NAV_F_SCALE | BB_NAV_F_THROW | BB_NAV_F_STEP_PITCH);
    bb_nav_set_instrument(s, row < 2 ? BB_NAV_INSTR_DRUM : BB_NAV_INSTR_DILLO, row < 2 ? 2 : 1);
    bb_nav_set_focus(s, (uint8_t)row);
    s->page = (uint8_t)page; s->bank = (uint8_t)bank; s->play_keys = (uint8_t)keys; s->gen_mode = (uint8_t)mode;
    s->sec_count = 14;
}
static bb_nav_out scan(bb_nav_state *s, const bb_nav_facts *f, uint32_t pressed, uint32_t held, uint32_t keys,
                       uint32_t keys_held, int enc, int16_t d)
{
    bb_nav_in in; bb_nav_out o;
    memset(&in, 0, sizeof in); memset(&o, 0, sizeof o);
    now_ms += 10;
    in.pressed = pressed; in.held = held; in.keys = keys; in.keys_held = keys_held; in.now_ms = now_ms; in.facts = *f;
    if (enc >= 0) in.enc[enc] = d;
    bb_nav_input(s, &in, &o);
    return o;
}
static unsigned banks_of(unsigned page, unsigned row)
{
    if (page == BB_NAV_FX) return 5;
    if (page == BB_NAV_SOUND && row < 2) return 2;
    if (page == BB_NAV_GLOBAL) return 3;
    if (page == BB_NAV_SONG) return 2;
    if (page == BB_NAV_STUDIO) return 2;   /* 2.1 P3b: CONDUCT, SONGS */
#if BB_WITH_USER_KITS
    if (page == BB_NAV_KIT && row == 0) return 3;
#endif
    return 1;
}

/* a-h for one state */
static void map_one(const bb_nav_state *s0, const bb_nav_facts *f, int rec, const char *tag)
{
    bb_nav_state s = *s0;
    bb_nav_led_out l, lh;
    bb_nav_help_row rows[BB_NAV_HELP_ROWS];
    unsigned n, k, b, i;
    char leg[160];
    s.rec_down = (uint8_t)rec;
    bb_nav_leds(&s, f, 0, &l);
    /* a + f: keys */
    for (k = 0; k < BB_NAV_KEYS; k++) {
        uint8_t a, r = bb_nav_key_role(&s, (uint8_t)k, rec, &a);
        if (r == BB_NAV_K_NONE) CHECK(l.key[k] == BB_NAV_LED_DARK, "%s key %u lit with no role", tag, k);
        if (l.key[k] != BB_NAV_LED_DARK) CHECK(r != BB_NAV_K_NONE, "%s key %u lit, no role", tag, k);
        if (r != BB_NAV_K_NONE && r != BB_NAV_K_ROOT)
            CHECK(bb_nav_key_help(r, a) != 0 && bb_nav_key_word(&s, (uint8_t)k, rec)[0], "%s key %u role %u: no text / word", tag, k, r);
    }
    /* a: buttons */
    for (b = 0; b < BB_NAV_BUTTONS; b++) {
        uint8_t r = bb_nav_button_role(&s, f, (uint8_t)b, b == BB_NAV_B_REC ? 0 : rec);
        if (r == BB_NAV_BR_NONE && !(rec && b == BB_NAV_B_PLAY))
            CHECK(l.button[b] == BB_NAV_LED_DARK, "%s button %u lit with no role", tag, b);
        if (r != BB_NAV_BR_NONE) CHECK(bb_nav_button_help(r) != 0, "%s button %u role %u: no help text", tag, b, r);
    }
    /* c: the legend names exactly the keys with a word, which are the keys with a role */
    (void)bb_nav_legend(&s, leg, sizeof leg, 1);
    for (k = 0; k < BB_NAV_KEYS; k++) {
        const char *w = bb_nav_key_word(&s, (uint8_t)k, rec);
        uint8_t a, r = bb_nav_key_role(&s, (uint8_t)k, rec, &a);
        CHECK((w[0] != 0) == (r != BB_NAV_K_NONE), "%s key %u: word '%s' vs role %u", tag, k, w, r);
        if (w[0]) CHECK(strstr(leg, w) != NULL, "%s legend '%s' lacks '%s'", tag, leg, w);
    }
    if (!leg[0]) for (k = 0; k < BB_NAV_KEYS; k++) CHECK(!bb_nav_key_word(&s, (uint8_t)k, rec)[0], "%s empty legend", tag);
    /* d: HELP rows */
    n = bb_nav_help(&s, f, rec, rows, 0, BB_NAV_HELP_ROWS, 0, NULL);
    {
        bb_nav_help_row all[64];
        unsigned m = n < 64 ? n : 64;
        uint32_t covered = 0;
        uint32_t bcov = 0;
        int knobs = 0;
        CHECK(n <= 64, "%s %u help rows", tag, n);
        (void)bb_nav_help(&s, f, rec, all, 0, m, 0, NULL);
        for (i = 0; i < m; i++) {
            if (all[i].kind == BB_NAV_HR_KEYS) for (k = all[i].a; k <= all[i].b; k++) covered |= 1u << k;
            if (all[i].kind == BB_NAV_HR_BUTTON) { bcov |= 1u << all[i].a; if (all[i].a == BB_NAV_B_OCTDN) bcov |= 1u << BB_NAV_B_OCTUP; }
            if (all[i].kind == BB_NAV_HR_KNOBS) knobs = 1;
            CHECK(all[i].text != 0 || all[i].kind == BB_NAV_HR_KNOBS, "%s row %u has no text", tag, i);
        }
        for (k = 0; k < BB_NAV_KEYS; k++) {
            uint8_t a, r = bb_nav_key_role(&s, (uint8_t)k, rec, &a), p = bb_nav_key_role(&s, (uint8_t)k, 0, &a);
            if (r != BB_NAV_K_NONE && r != BB_NAV_K_ROOT && (!rec || r != p || bb_nav_key_is_sound(r)))
                CHECK(covered >> k & 1u, "%s help misses key %u (role %u)", tag, k, r);
        }
        for (b = 0; b < BB_NAV_BUTTONS; b++) {
            uint8_t r = bb_nav_button_role(&s, f, (uint8_t)b, b == BB_NAV_B_REC ? 0 : rec);
            if (rec && (b == BB_NAV_B_REC || r == bb_nav_button_role(&s, f, (uint8_t)b, 0))) continue;
            if (r != BB_NAV_BR_NONE && l.button[b] != BB_NAV_LED_DARK)
                CHECK(bcov >> b & 1u, "%s help misses lit button %u (role %u)", tag, b, r);
        }
        if (!rec) {
            uint8_t ro[4]; const char *lab[4]; int any = 0;
            bb_nav_knobs(&s, ro, lab);
            for (k = 0; k < 4; k++) any |= lab[k][0] != 0;
            CHECK(knobs == any, "%s knob row %d vs labels %d", tag, knobs, any);
        }
    }
    /* e: HELP keeps the lights */
    s.help = 1; bb_nav_leds(&s, f, 0, &lh); s.help = 0;
    for (k = 0; k < BB_NAV_KEYS; k++) CHECK(lh.key[k] == l.key[k], "%s help changed key %u's light", tag, k);
    for (b = 0; b < BB_NAV_BUTTONS; b++)
        if (b != BB_NAV_B_HOME) CHECK(lh.button[b] == l.button[b], "%s help changed button %u's light", tag, b);
    CHECK(lh.button[BB_NAV_B_HOME] == BB_NAV_LED_LIT, "%s HOME not bright under help", tag);
    /* b + g: dark controls never act; the input dry-run agrees */
    for (k = 0; k < BB_NAV_KEYS; k++) {
        bb_nav_state t = s; bb_nav_out o;
        uint32_t held = rec ? BIT(BB_NAV_B_REC) : 0;
        if (l.key[k] != BB_NAV_LED_DARK) continue;
        if (rec) (void)scan(&t, f, BIT(BB_NAV_B_REC), held, 0, 0, -1, 0), t.rec_used = 1;
        o = scan(&t, f, 0, held, BIT(k), 0, -1, 0);
        CHECK(o.kev_count == 0 && o.actions == 0 && t.page == s.page && t.bank == s.bank && t.play_keys == s.play_keys,
              "%s dark key %u acted (events %u actions %x page %u bank %u)", tag, k, o.kev_count, o.actions, t.page, t.bank);
    }
    for (b = 0; b < BB_NAV_BUTTONS; b++) {
        bb_nav_state t = s; bb_nav_out o;
        int effect;
        if (b == BB_NAV_B_HOME || b == BB_NAV_B_REC || b == BB_NAV_B_PLAY) continue;
        t.home_down = t.home_used = t.help = 0; t.throw_on = 0; t.keys_active = 0; t.rec_used = 1;
        o = scan(&t, f, BIT(b), BIT(b) | (rec ? BIT(BB_NAV_B_REC) : 0), 0, 0, -1, 0);
        effect = o.actions || o.page_changed || o.oct || t.bank != s.bank || t.play_keys != s.play_keys;
        if (effect) CHECK(l.button[b] != BB_NAV_LED_DARK, "%s button %u acts but is dark", tag, b);
        if (l.button[b] == BB_NAV_LED_DARK) CHECK(!effect, "%s dark button %u acted (actions %x)", tag, b, o.actions);
    }
}

static void maps(void)
{
    char tag[96];
    for (unsigned row = 0; row < 3; row++)
        for (unsigned page = 0; page < BB_NAV_PAGES; page++)
            for (unsigned keys = 0; keys < (page == BB_NAV_PLAY ? 2u : 1u); keys++)
                for (unsigned bank = 0; bank < banks_of(page, row); bank++)
                    for (unsigned mode = 0; mode < (page == BB_NAV_EVOLVE ? 3u : 1u); mode++)
                        for (int fs = 0; fs < 3; fs++)
                            for (int rec = 0; rec < 2; rec++) {
                                bb_nav_state s; bb_nav_facts f;
                                state(&s, page, bank, row, keys, mode);
                                facts_set(&f, fs); f.gen_mode = (uint8_t)mode;
                                snprintf(tag, sizeof tag, "%s/b%u/r%u/k%u/m%u/f%d/rec%d", PG[page], bank, row, keys, mode, fs, rec);
                                map_one(&s, &f, rec, tag);
                            }
}

static void behaviours(void)
{
    bb_nav_state s; bb_nav_facts f; bb_nav_out o;
    const uint32_t LFO = BIT(BB_NAV_B_LFO), EDIT = BIT(BB_NAV_B_EDIT), HOME = BIT(BB_NAV_B_HOME), REC = BIT(BB_NAV_B_REC);
    facts_set(&f, 0);
    /* 8.1.12 modes: LFO from PLAY opens EVOLVE (mode unchanged), then each LFO asks for the next mode, no music */
    state(&s, BB_NAV_PLAY, 0, 0, 0, 0);
    o = scan(&s, &f, LFO, LFO, 0, 0, -1, 0); (void)scan(&s, &f, 0, 0, 0, 0, -1, 0);
    CHECK(s.page == BB_NAV_EVOLVE && (o.actions & BB_NAV_A_EVO_OPEN) && !(o.actions & BB_NAV_A_GEN_MODE), "LFO from PLAY: open EVOLVE only");
    for (unsigned m = 0; m < 3; m++) {
        static const char *const cap[3] = {"CAPTURE", "APPLY", "APPLY"};
        f.gen_mode = (uint8_t)m;
        o = scan(&s, &f, 0, 0, 0, 0, -1, 0);
        CHECK(!strcmp(bb_nav_edit_label(&s, &f), cap[m]), "mode %u: EDIT reads %s", m, bb_nav_edit_label(&s, &f));
        o = scan(&s, &f, LFO, LFO, 0, 0, -1, 0); (void)scan(&s, &f, 0, 0, 0, 0, -1, 0);
        CHECK(o.actions == BB_NAV_A_GEN_MODE && s.page == BB_NAV_EVOLVE, "mode %u: LFO = next mode only (actions %x)", m, o.actions);
        o = scan(&s, &f, 0, 0, BIT(BB_NAV_BLACK(3)), 0, -1, 0);
        CHECK(o.kev_count == 1 && o.kev[0].role == BB_NAV_K_GEN_MODE, "mode %u: black 3 = the same mode step", m);
        (void)scan(&s, &f, 0, 0, 0, 0, -1, 0);
        o = scan(&s, &f, EDIT, EDIT, 0, 0, -1, 0); (void)scan(&s, &f, 0, 0, 0, 0, -1, 0);
        CHECK(o.actions == BB_NAV_A_EVO_CAPTURE, "mode %u: EDIT captures / applies", m);
    }
    /* EVOLVE keys light what is selected */
    {
        bb_nav_led_out l;
        state(&s, BB_NAV_EVOLVE, 0, 0, 0, BB_NAV_MODE_EVOLVE); facts_set(&f, 0); f.evo_parts = 2; f.evo_moves = 4;
        bb_nav_leds(&s, &f, 0, &l);
        CHECK(l.key[0] == BB_NAV_LED_DIM && l.key[1] == BB_NAV_LED_LIT && l.key[2] == BB_NAV_LED_DIM, "EVOLVE parts lights");
        CHECK(l.key[6] == BB_NAV_LED_DIM && l.key[8] == BB_NAV_LED_LIT && l.key[3] == BB_NAV_LED_DARK && l.key[10] == BB_NAV_LED_DARK,
              "EVOLVE attribute lights / white 4-6, 11-16 off");
        s.gen_mode = BB_NAV_MODE_EUCLID; f.gen_mode = BB_NAV_MODE_EUCLID; f.gen_voice = 5; bb_nav_leds(&s, &f, 0, &l);
        CHECK(l.key[5] == BB_NAV_LED_LIT && l.key[15] == BB_NAV_LED_DIM, "EUCLID lanes light");
        s.gen_mode = BB_NAV_MODE_RANDOM; f.gen_mode = BB_NAV_MODE_RANDOM; bb_nav_leds(&s, &f, 0, &l);
        CHECK(l.key[0] == BB_NAV_LED_DARK, "RANDOM: white keys off");
        s.rec_down = 1; f.steps = 0x0004; bb_nav_leds(&s, &f, 0, &l);
        CHECK(l.key[2] == BB_NAV_LED_LIT && l.key[3] == BB_NAV_LED_DIM, "REC: locked steps bright");
    }
    /* 8.1.13 FX: white 1-5 choose the tab; EDIT does nothing; FX reopens on the last tab; knobs per tab unchanged */
    {
        static const char *const K[5][4] = {{"TIME MS", "FDBK %", "HEADS", "ECHO WET"}, {"TONE %", "DRIVE %", "WEAR %", "ECHO WET"},
                                            {"SIZE %", "DAMP %", "SPR WET", "ECHO WET"}, {"MOTION %", "WEAR %", "FDBK %", "ECHO WET"},
                                            {"ECHO", "SPRING", "LEVEL", "DRIVE"}};
        state(&s, BB_NAV_STUDIO, 0, 0, 0, 0); facts_set(&f, 0);
        o = scan(&s, &f, BIT(BB_NAV_B_FX), BIT(BB_NAV_B_FX), 0, 0, -1, 0); (void)scan(&s, &f, 0, 0, 0, 0, -1, 0);
        CHECK(s.page == BB_NAV_FX && s.bank == 0, "FX opens on ECHO the first time");
        for (unsigned t = 0; t < 5; t++) {
            uint8_t r[4]; const char *lab[4];
            o = scan(&s, &f, 0, 0, BIT(t), 0, -1, 0); (void)scan(&s, &f, 0, 0, 0, 0, -1, 0);
            CHECK(s.bank == t && o.kev_count == 0, "FX white %u -> tab %u (bank %u)", t + 1, t, s.bank);
            bb_nav_knobs(&s, r, lab);
            for (int k = 0; k < 4; k++) CHECK(!strcmp(lab[k], K[t][k]), "FX tab %u K%d '%s'", t, k + 1, lab[k]);
            o = scan(&s, &f, EDIT, EDIT, 0, 0, -1, 0); (void)scan(&s, &f, 0, 0, 0, 0, -1, 0);
            CHECK(o.actions == 0 && s.bank == t, "FX EDIT does nothing");
        }
        CHECK(!strcmp(bb_nav_bank_name(&s), "SENDS"), "FX tab 5 SENDS");
        s.bank = 1; s.fx_tab = 1;
        (void)scan(&s, &f, HOME, HOME, 0, 0, -1, 0); (void)scan(&s, &f, 0, 0, 0, 0, -1, 0);
        (void)scan(&s, &f, BIT(BB_NAV_B_FX), BIT(BB_NAV_B_FX), 0, 0, -1, 0);
        CHECK(s.page == BB_NAV_FX && s.bank == 1 && !strcmp(bb_nav_bank_name(&s), "TAPE DRIVE"), "FX reopens on TAPE DRIVE");
    }
    /* GLOBAL: white 1 / 2 open MIDI / CLOCK (bb_nav banks), black 1 and HOME go back; white 4 asks for SONG; white 5 drums */
    {
        state(&s, BB_NAV_GLOBAL, 0, 0, 0, 0); facts_set(&f, 0);
        o = scan(&s, &f, 0, 0, BIT(1), 0, -1, 0); (void)scan(&s, &f, 0, 0, 0, 0, -1, 0);
        CHECK(s.bank == BB_NAV_GLOBAL_CLOCK, "GLOBAL white 2 opens MIDI CLOCK");
        { uint8_t r[4]; const char *lab[4]; bb_nav_knobs(&s, r, lab); CHECK(!strcmp(lab[0], "CLOCK IN"), "CLOCK knobs"); }
        o = scan(&s, &f, EDIT, EDIT, 0, 0, -1, 0); (void)scan(&s, &f, 0, 0, 0, 0, -1, 0);
        CHECK(o.actions == BB_NAV_A_GLOBALS_SAVE, "EDIT on MIDI saves globals");
        o = scan(&s, &f, BIT(BB_NAV_B_FX), BIT(BB_NAV_B_FX), 0, 0, -1, 0); (void)scan(&s, &f, 0, 0, 0, 0, -1, 0);
        CHECK(s.page == BB_NAV_GLOBAL && o.actions == 0, "FX does nothing on MIDI (dark)");
        o = scan(&s, &f, 0, 0, BIT(BB_NAV_BLACK(1)), 0, -1, 0); (void)scan(&s, &f, 0, 0, 0, 0, -1, 0);
        CHECK(s.bank == BB_NAV_GLOBAL_MAIN, "MIDI black 1: back to GLOBAL");
        (void)scan(&s, &f, 0, 0, BIT(0), 0, -1, 0); (void)scan(&s, &f, 0, 0, 0, 0, -1, 0);
        (void)scan(&s, &f, HOME, HOME, 0, 0, -1, 0); (void)scan(&s, &f, 0, 0, 0, 0, -1, 0);
        CHECK(s.page == BB_NAV_GLOBAL && s.bank == BB_NAV_GLOBAL_MAIN, "HOME on MIDI: back to GLOBAL");
        o = scan(&s, &f, 0, 0, BIT(3), 0, -1, 0);
        CHECK(o.kev_count == 1 && o.kev[0].role == BB_NAV_K_SUBPAGE && o.kev[0].arg == 3, "GLOBAL white 4 asks for SONG");
        (void)scan(&s, &f, 0, 0, 0, 0, -1, 0);
        o = scan(&s, &f, 0, 0, BIT(4), 0, -1, 0); (void)scan(&s, &f, 0, 0, 0, 0, -1, 0);
        CHECK(o.kev_count == 1 && o.kev[0].arg == 4, "GLOBAL white 5 (drums) opens the Dillo kit view");
        state(&s, BB_NAV_GLOBAL, 0, 2, 0, 0);
        o = scan(&s, &f, 0, 0, BIT(4), 0, -1, 0);
        CHECK(o.kev_count == 0, "GLOBAL white 5 does nothing with Dillo in focus");
    }
    /* SONG: keys, EDIT, REC tap records, REC hold is help (no record), HOME back one level then STUDIO */
    {
        state(&s, BB_NAV_SONG, 0, 0, 0, 0); facts_set(&f, 1);
        o = scan(&s, &f, 0, 0, BIT(1), 0, -1, 0); (void)scan(&s, &f, 0, 0, 0, 0, -1, 0);
        CHECK(s.bank == BB_NAV_SONG_ADD && o.kev_count == 1 && o.kev[0].role == BB_NAV_K_SONG_ADD && o.message == BB_NAV_MSG_SONG_ADD,
              "SONG white 2: ADD with the prompt");
        { uint8_t r[4]; const char *lab[4]; bb_nav_knobs(&s, r, lab); CHECK(!strcmp(lab[0], "TARGET") && !strcmp(lab[3], "BARS"), "ADD knobs"); }
        o = scan(&s, &f, 0, 0, 0, 0, BB_NAV_E_ALGO, 2);
        CHECK(o.enc[BB_NAV_E_ALGO].target == BB_NAV_TO_SONG_BANK && s.page == BB_NAV_SONG, "ALGO on ADD moves the source bank");
        o = scan(&s, &f, 0, 0, 0, 0, BB_NAV_E_PRESET, 1);
        CHECK(o.enc[BB_NAV_E_PRESET].target == BB_NAV_TO_NONE && s.page == BB_NAV_SONG, "PRESETS does nothing on SONG");
        o = scan(&s, &f, EDIT, EDIT, 0, 0, -1, 0); (void)scan(&s, &f, 0, 0, 0, 0, -1, 0);
        CHECK(o.actions == BB_NAV_A_SONG_EDIT, "EDIT on ADD adds");
        (void)scan(&s, &f, HOME, HOME, 0, 0, -1, 0); (void)scan(&s, &f, 0, 0, 0, 0, -1, 0);
        CHECK(s.page == BB_NAV_SONG && s.bank == BB_NAV_SONG_ROWS, "HOME on ADD: back to the rows");
        o = scan(&s, &f, REC, REC, 0, 0, -1, 0); o = scan(&s, &f, 0, 0, 0, 0, -1, 0);
        CHECK(o.actions == BB_NAV_A_RECORD, "SONG: a REC tap records a take");
        (void)scan(&s, &f, REC, REC, 0, 0, -1, 0);
        for (int t = 0; t < 8; t++) { now_ms += 90; o = scan(&s, &f, 0, REC, 0, 0, -1, 0); }
        CHECK(o.help, "SONG: REC held shows the REC help");
        o = scan(&s, &f, 0, 0, 0, 0, -1, 0);
        CHECK(!(o.actions & BB_NAV_A_RECORD), "SONG: REC held for help does not record");
        o = scan(&s, &f, 0, 0, BIT(BB_NAV_BLACK(2)), 0, -1, 0);
        CHECK(o.kev_count == 0, "SONG black 2 (REDO) is dark with nothing to redo and does nothing");
        f.song |= BB_NAV_SONG_F_REDO; (void)scan(&s, &f, 0, 0, 0, 0, -1, 0);
        o = scan(&s, &f, 0, 0, BIT(BB_NAV_BLACK(2)), 0, -1, 0);
        CHECK(o.kev_count == 1 && o.kev[0].role == BB_NAV_K_SONG_REDO, "SONG black 2 redoes");
        (void)scan(&s, &f, 0, 0, 0, 0, -1, 0);
        (void)scan(&s, &f, BIT(BB_NAV_B_GLO), BIT(BB_NAV_B_GLO), 0, 0, -1, 0);
        CHECK(s.page == BB_NAV_STUDIO, "GLO leaves SONG for STUDIO");
    }
    /* HELP: lights stay; press to explain; OCT pages; PLAY still works; nothing acts */
    {
        bb_nav_led_out l0, l1;
        state(&s, BB_NAV_PLAY, 0, 0, 0, 0); facts_set(&f, 1);
        bb_nav_leds(&s, &f, 0, &l0);
        o = scan(&s, &f, HOME, HOME, 0, 0, -1, 0);
        for (int t = 0; t < 8; t++) { now_ms += 90; o = scan(&s, &f, 0, HOME, 0, 0, -1, 0); CHECK(!o.actions && !o.kev_count, "help entry acts"); }
        CHECK(s.help, "HOME held: help");
        bb_nav_leds(&s, &f, 0, &l1);
        CHECK(!memcmp(l0.key, l1.key, sizeof l0.key), "help keeps the key lights");
        o = scan(&s, &f, 0, HOME, BIT(BB_NAV_BLACK(2)), BIT(BB_NAV_BLACK(2)), -1, 0);
        CHECK(!o.kev_count && !o.actions && s.explain == (BB_NAV_EX_KEY | BB_NAV_BLACK(2)), "explain a key, no event");
        {
            int found = -1; bb_nav_help_row r;
            (void)bb_nav_help(&s, &f, 0, NULL, 0, 0, s.explain, &found);
            CHECK(found >= 0, "the explained key has a row");
            if (found >= 0) { (void)bb_nav_help(&s, &f, 0, &r, (unsigned)found, 1, 0, NULL);
                CHECK(r.kind == BB_NAV_HR_KEYS && r.a <= BB_NAV_BLACK(2) && r.b >= BB_NAV_BLACK(2) && r.role == BB_NAV_K_GROUP_MUTE, "explained row is B1-4 MUTE"); }
        }
        o = scan(&s, &f, BIT(BB_NAV_B_SEQ), HOME | BIT(BB_NAV_B_SEQ), 0, 0, -1, 0);
        CHECK(!o.actions && s.page == BB_NAV_PLAY && !s.play_keys && s.explain == (BB_NAV_EX_BUTTON | BB_NAV_B_SEQ), "explain SEQ, no page change");
        o = scan(&s, &f, 0, HOME, 0, 0, BB_NAV_E_K2, 3);
        CHECK(o.enc[BB_NAV_E_K2].target == BB_NAV_TO_NONE && s.explain == (BB_NAV_EX_ENCODER | BB_NAV_E_K2), "explain K2, no route");
        {
            unsigned n = bb_nav_help(&s, &f, 0, NULL, 0, 0, 0, NULL), pages = (n + BB_NAV_HELP_PAGE - 1) / BB_NAV_HELP_PAGE;
            unsigned was = s.help_page;
            o = scan(&s, &f, BIT(BB_NAV_B_OCTUP), HOME | BIT(BB_NAV_B_OCTUP), 0, 0, -1, 0);
            CHECK(s.help_page == (was + 1 < pages ? was + 1 : was), "OCT+ turns the help page (%u rows, %u -> %u)", n, was, s.help_page);
            o = scan(&s, &f, BIT(BB_NAV_B_OCTDN), HOME | BIT(BB_NAV_B_OCTDN), 0, 0, -1, 0);
            CHECK(s.help_page == (was + 1 < pages ? was : was ? was - 1 : 0), "OCT- turns it back");
            CHECK(!o.oct && !o.actions, "OCT in help does nothing else");
        }
        o = scan(&s, &f, BIT(BB_NAV_B_PLAY), HOME | BIT(BB_NAV_B_PLAY), 0, 0, -1, 0);
        CHECK(o.actions == BB_NAV_A_TRANSPORT, "PLAY works under help");
        o = scan(&s, &f, 0, 0, 0, 0, -1, 0);
        CHECK(!s.help && !o.actions && !o.kev_count && s.page == BB_NAV_PLAY && !s.explain, "leaving help acts on nothing");
    }
    /* REC map: LEDs and legend follow REC */
    {
        bb_nav_led_out l; char leg[96];
        state(&s, BB_NAV_STUDIO, 0, 0, 0, 0); facts_set(&f, 0); f.instr_solo = 2;
        (void)scan(&s, &f, REC, REC, 0, 0, -1, 0);
        bb_nav_leds(&s, &f, 0, &l); (void)bb_nav_legend(&s, leg, sizeof leg, 1);
        /* RC2 (Joe 10:32): PERFORM's instruments are black B1-B3 (white 1-16 are the sections) */
        CHECK(l.key[17] == BB_NAV_LED_LIT && l.key[16] == BB_NAV_LED_DIM && strstr(leg, "SOLO"), "REC: PERFORM B1-B3 show SOLO (%s)", leg);
        (void)scan(&s, &f, 0, 0, 0, 0, -1, 0);
        (void)bb_nav_legend(&s, leg, sizeof leg, 1);
        CHECK(strstr(leg, "MUTE") && !strstr(leg, "SOLO"), "REC up: MUTE again");
    }
}

static void perform_edit_routes(void)
{
    for(unsigned row=0;row<3;row++)for(int mode=0;mode<3;mode++) {
        bb_nav_state s;bb_nav_facts f;bb_nav_out o;
        state(&s,BB_NAV_STUDIO,BB_NAV_HOME_CONDUCT,row,BB_NAV_PLAY_LIVE,0);facts_set(&f,mode);
        o=scan(&s,&f,BIT(BB_NAV_B_SEQ),BIT(BB_NAV_B_SEQ),0,0,-1,0);
        CHECK(s.page==BB_NAV_PLAY&&s.play_keys==BB_NAV_PLAY_STEPS,"PERFORM SEQ enters steps row %u",row);
        CHECK(!strcmp(bb_nav_page_name(&s),row<2?"DRUM STEPS":"DILLO STEPS"),"step title matches instrument");
        CHECK(!(o.actions&(BB_NAV_A_TRANSPORT|BB_NAV_A_PATTERN_CONFIRM|BB_NAV_A_RECORD)),"opening steps does not load, record or change transport");
        (void)scan(&s,&f,0,0,0,0,-1,0);
        o=scan(&s,&f,BIT(BB_NAV_B_ARP),BIT(BB_NAV_B_ARP),0,0,-1,0);
        CHECK(s.page==BB_NAV_PLAY&&s.play_keys==BB_NAV_PLAY_LIVE,"ARP is one press from steps to live play");
        CHECK(!strcmp(bb_nav_page_name(&s),row<2?"DRUM PADS":"DILLO KEYS"),"live title matches instrument");
        CHECK(!(o.actions&(BB_NAV_A_TRANSPORT|BB_NAV_A_PATTERN_CONFIRM|BB_NAV_A_RECORD)),"opening live play does not load, record or change transport");
    }
}

int main(void)
{
    maps();
    behaviours();
    perform_edit_routes();
    printf("nav p1: %lu checks, %lu failures\n", checks, failures);
    return failures != 0;
}
