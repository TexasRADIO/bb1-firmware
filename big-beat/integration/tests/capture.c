/* SPDX-License-Identifier: GPL-3.0-only
 * Route-matrix captures: renders every player-facing navigation route of the CANDIDATE source with
 * its real gfx.c, fonts, art and bb_ui_v3 module, knob labels from its navigation model, plus the
 * notice, fault, KIT/PATTERN pending and HELP overlays. Every capture runs the firmware's layout
 * lint (text inside its band, inside the margin, not cut, no overlaps). Writes raw RGB565 frames
 * (name\0 + 240*240*2 bytes each) to argv[1]; tests/png.py turns them into PNGs + a contact sheet.
 * Host-only: example data, not device screenshots. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "bb_nav.h"

static uint16_t host_screen[240 * 240];
static void lcd_sync(void) {}
static void lcd_blit(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint16_t *px)
{
    for (uint32_t j = 0; j < h; j++)
        for (uint32_t i = 0; i < w; i++)
            if (x + i < 240u && y + j < 240u)
                host_screen[(y + j) * 240u + x + i] = (uint16_t)((px[j * w + i] >> 8) | (px[j * w + i] << 8));
}
static void host_text_hook(int32_t x0, int32_t y0, int32_t x1, int32_t y1, const char *s, uint32_t flags);
#define GFX_HOOK_TEXT(x0, y0, x1, y1, s, flags) host_text_hook((x0), (y0), (x1), (y1), (s), (flags))
#define GFX_HOOK_BLIT(x, y, r0) ((void)0)
#define GFX_HOOK_BEGIN() ((void)0)
#define GFX_HOOK_PIXELS(n) ((void)0)
static uint32_t host_layer;
#define BB_UI_HOOK_LAYER(n) (host_layer = (n))
/* 2.2: every key a playing page draws as a button, and whether it looks lit (bb_ui_play.c) */
static uint32_t hk_seen, hk_bright;
static void host_key(uint32_t k, int bright) { hk_seen |= 1u << k; if (bright) hk_bright |= 1u << k; }
#define BB_UI_HOOK_KEY(k, bright) host_key((k), (bright))
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wunused-const-variable"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wsign-compare"
#include "gfx.c"
#include "bb_armadillo.h"
#pragma GCC diagnostic pop
#define BB_UI_FIRMWARE 1
#define BB_UI_HELP 1
#define BB_UI_ALL_PAGES 1
#define BB_UI_HOME 1
#ifndef BB_UI_BACKDROP
#define BB_UI_BACKDROP 1                     /* as bb_ui_v3_firmware.inc: the product layout */
#endif
#include "bb_ui_v3/bb_ui.c"
#include "bb_ui_v3_route.h"
#include "bb_factory.h"                     /* RC2: every catalogue section name on the HOME lanes */

typedef struct { int16_t x0, y0, x1, y1; uint8_t flags, layer; char s[40]; } htext;
static htext texts[256]; static unsigned ntext, lint_on, failures, known, perf_on, perf_agree;
static uint8_t bb_v3_route_page;   /* the page of the frame being shot */
static void host_text_hook(int32_t x0, int32_t y0, int32_t x1, int32_t y1, const char *s, uint32_t flags)
{
    if (!lint_on || ntext >= 256u) return;
    x0 += g_cx; x1 += g_cx; y0 += g_cy; y1 += g_cy;
    for (unsigned i = 0; i < ntext; i++)
        if (texts[i].x0 == x0 && texts[i].x1 == x1 && !strncmp(texts[i].s, s, 39) &&
            y0 <= texts[i].y1 + 1 && y1 + 1 >= texts[i].y0) {          /* the same text split across strips */
            if (y0 < texts[i].y0) texts[i].y0 = (int16_t)y0;
            if (y1 > texts[i].y1) texts[i].y1 = (int16_t)y1;
            return;
        }
    htext *t = &texts[ntext++];
    t->x0 = (int16_t)x0; t->y0 = (int16_t)y0; t->x1 = (int16_t)x1; t->y1 = (int16_t)y1;
    t->flags = (uint8_t)flags; t->layer = (uint8_t)host_layer;
    strncpy(t->s, s, 39); t->s[39] = 0;
}
#define FAIL(...) do { failures++; printf("FAIL "); printf(__VA_ARGS__); printf("\n"); } while (0)
static void lint(const char *what)
{
    for (unsigned i = 0; i < ntext; i++) {
        const htext *t = &texts[i];
        int band0 = t->y1 <= 23, band1 = t->y0 >= 24 && t->y1 <= 198, band2 = t->y0 >= 199;
        if (!(t->x0 >= 0 && t->x1 <= 240 && t->y0 >= 0 && t->y1 <= 240)) FAIL("%s: \"%s\" leaves the screen", what, t->s);
        if ((t->flags & 1u) && !(t->flags & 8u)) FAIL("%s: label \"%s\" is cut", what, t->s);
        if (!(band0 || band1 || band2 || (t->flags & 4u))) FAIL("%s: \"%s\" crosses a rule (y %d..%d)", what, t->s, t->y0, t->y1);
        if (!(t->x0 >= 4 || (t->flags & 4u))) FAIL("%s: \"%s\" outside the margin", what, t->s);
        for (unsigned j = i + 1u; j < ntext; j++) {
            const htext *v = &texts[j];
            if (t->layer == v->layer && t->x0 < v->x1 && v->x0 < t->x1 && t->y0 < v->y1 && v->y0 < t->y1) {
                /* KNOWN: approved boot42 HOME lane (PR #29): the grey section line touches the pattern
                 * name by ~1 px. Reported, not failed; Joe passed this layout on the device. */
                /* r9.9: keyed on geometry, not on the old "HOOK 2/4" placeholder: on the device this grey line is the
                 * kit name ("808 Tight Hats", "DrumTraks"), whose descender meets the anti-aliased top of the
                 * pattern name at one pixel. Box overlap of at most 3 rows, grey line above; anything else fails. */
                /* 1.5 (P7): the kit row now clears the pattern name, so this overlap fails like any other. */
                FAIL("%s: \"%s\" overlaps \"%s\" (y %d..%d vs %d..%d, x %d..%d vs %d..%d)", what, t->s, v->s, t->y0, t->y1, v->y0, v->y1, t->x0, t->x1, v->x0, v->x1);
            }
        }
    }
}

static FILE *out; static unsigned shots;
static bb_ui u; static bb_ui_in in;
/* Compare bounded incremental painting with a fresh full render at the same instant. */
static void incremental_equal(const char *name)
{
    static uint16_t incremental[240*240];
    unsigned n;
    for(n=0;n<64;n++)if(!bb_ui_frame(&u,&in)->waiting)break;
    if(n==64)FAIL("%s: incremental painting did not settle",name);
    memcpy(incremental,host_screen,sizeof incremental);
    bb_ui exact=u; bb_ui_invalidate(&exact);
    memset(host_screen,0x5a,sizeof host_screen);
    for(n=0;n<64;n++)if(!bb_ui_frame(&exact,&in)->waiting)break;
    unsigned different=0;
    for(unsigned i=0;i<240u*240u;i++)different+=incremental[i]!=host_screen[i];
    if(different)FAIL("%s: %u incremental pixels differ from full paint",name,different);
    memcpy(host_screen,incremental,sizeof incremental);
}
static char ktext[4][16];
static char legend[64];
/* 2.0 D4: the key legend exactly as the firmware makes it (bb_v3_common): the whole line, or black keys only when the
 * whole line is wider than the screen. It shows only after BB_EV_LEGEND, but every route's legend is checked here:
 * it must fit the 224 px band and must not be truncated by the 64-byte buffer (the same text with a large buffer). */
static void set_legend(const bb_nav_state *s)
{
    char big[160];
    int whites = 1;
    (void)bb_nav_legend(s, legend, sizeof legend, 1);
    if (text_w(&AF_S, legend) > 224) { (void)bb_nav_legend(s, legend, sizeof legend, 0); whites = 0; }
    (void)bb_nav_legend(s, big, sizeof big, whites);
    if (strcmp(big, legend)) FAIL("legend cut by its buffer: \"%s\" / \"%s\"", legend, big);
    if (text_w(&AF_S, legend) > 224) FAIL("legend wider than the band: \"%s\"", legend);
    in.legend = legend;
}
/* RC2 r2: a name's variant marks in order (digit runs, lone capitals), which an abbreviation must keep */
static void marks(const char *s, char *o, size_t cap)
{
    size_t m = 0;
    for (size_t i = 0; s[i] && m + 2 < cap; i++) {
        int alone = s[i] >= 'A' && s[i] <= 'Z' && (i == 0 || strchr(" /+-", s[i - 1])) && (!s[i + 1] || strchr(" /+-", s[i + 1]));
        if ((s[i] >= '0' && s[i] <= '9') || alone) o[m++] = s[i];
        else if (m && o[m - 1] != ',') o[m++] = ',';
    }
    while (m && o[m - 1] == ',') m--;
    o[m] = 0;
}
static char perf_st[144];
static void shot(const char *name)
{
    if (perf_on) { bb_ui_perform_status(perf_st, sizeof perf_st, &in); in.perf_status = perf_st; }   /* as bb_v3_home */
    lint_on = 1; ntext = 0;
    bb_ui_invalidate(&u);
    (void)bb_ui_frame(&u, &in);
    lint_on = 0; lint(name);
    if (perf_on && !in.notice && !in.help && bb_v3_route_page == BB_UI_STUDIO) {   /* RC2 r2: the status line agrees with the buttons */
        const char *p = 0, *q = 0, *oth = 0; unsigned f = 0, qparts = 0; char want[96] = "", got[96] = "";
        for (unsigned r = 0; r < 3; r++) {
            if (in.lane[r].flags & BB_UI_LN_FOCUS) f = r;
            if (in.lane[r].flags & BB_UI_LN_NEXT) qparts |= 1u << r;
        }
        for (unsigned k = 0; k < 16 && in.tile_state[k] != BB_UI_TILE_NONE && in.tile[k]; k++) {
            if (in.tile_state[k] == BB_UI_TILE_PLAYING) p = in.tile[k];
            if (in.tile_state[k] == BB_UI_TILE_QUEUED) q = in.tile[k];
        }
        if (!p && in.lane[f].name && in.lane[f].name[0]) p = in.lane[f].name;
        for (unsigned r = 0; r < 3; r++)
            if (r != f && p && in.lane[r].name && in.lane[r].name[0] && strcmp(in.lane[r].name, p)) oth = oth && strcmp(oth, in.lane[r].name) ? "vary" : in.lane[r].name;
        if (q) {
            const char *prefix = qparts == 7u ? "ALL > " : qparts == 1u ? "A > " : qparts == 2u ? "B > " :
                                 qparts == 4u ? "D > " : qparts == 3u ? "A+B > " : qparts == 5u ? "A+D > " : "B+D > ";
            snprintf(want, sizeof want, "%s%s%s", qparts ? prefix : "", q,
                     in.running ? " NEXT \xB7 AGAIN CANCELS" : " NEXT");
        }
        else if (p && oth) snprintf(want, sizeof want, "%s %s \xB7 others %s", f == 0 ? "KIT A" : f == 1 ? "KIT B" : "DILLO", p, oth);
        else if (p) snprintf(want, sizeof want, "%s%s%s", in.running ? "Playing " : "Ready: ", p, in.running ? "" : " \xB7 PLAY starts");
        for (unsigned i = 0; i < ntext; i++) if (texts[i].y0 >= 206 && texts[i].y0 < 224 && texts[i].x0 < 20) snprintf(got, sizeof got, "%s", texts[i].s);
        if (getenv("CAP_ST")) for (unsigned i = 0; i < ntext; i++) if (texts[i].y0 > 190) printf("  st %s %d,%d..%d,%d \"%s\"\n", name, texts[i].x0, texts[i].y0, texts[i].x1, texts[i].y1, texts[i].s);
        if (strncmp(want, got, 39)) FAIL("%s: status \"%s\", the buttons show \"%s\"", name, got, want);
        else perf_agree++;
    }
    for (unsigned i = 0; i < ntext && in.footer; i++)        /* RC2: the chooser / PERFORM footer line is never cut */
        if (texts[i].y0 >= 218 && (texts[i].flags & 1u)) FAIL("%s: footer \"%s\" is cut", name, texts[i].s);
    fwrite(name, 1, strlen(name) + 1, out);
    fwrite(host_screen, 2, 240 * 240, out);
    shots++;
}

/* 2.1 P1: what bb_ui_v3_firmware.inc fills from bb_nav: the generated HELP rows (bb_nav_help), the FX / EVOLVE tabs */
static bb_nav_facts pf;
static void real_leds(const bb_nav_state *s);
static void play_example(const bb_nav_state *s, uint8_t row, uint8_t page);
static bb_nav_help_row hnav[BB_UI_HELP_ROWS];
static bb_ui_help_row hrow[BB_UI_HELP_ROWS];
static char hkeys[BB_UI_HELP_ROWS][8], hknobs[40], hnote[48];
static const char *tabs[5];
static void keyname(char *o, unsigned a, unsigned b)
{
    unsigned base = a < 16u ? 0u : 16u;
    if (b > a) snprintf(o, 8, "%s%u-%u", base ? "B" : "", a - base + 1u, b - base + 1u);
    else snprintf(o, 8, "%s%u", base ? "B" : "", a - base + 1u);
}
static void help_fill(const bb_nav_state *s0, int rec, unsigned page)
{
    bb_nav_state sf = *s0, *s = &sf;   /* the firmware's feature set (bb_nav_firmware.inc BB_NAV_FEATURES) */
    bb_nav_set_features(s, BB_NAV_F_TAP | BB_NAV_F_SCALE | BB_NAV_F_THROW | BB_NAV_F_STEP_PITCH);
    static const uint8_t KCB[BB_NAV_BUTTONS] = {KC_FX, KC_SCL, KC_ENV, KC_LFO, KC_EDIT, KC_GLO, KC_HOME, KC_SAVE, KC_ARP, KC_SEQ,
                                                 KC_PLAY, KC_REC, KC_OCTDN, KC_OCTUP};
    static const uint8_t KCE[3] = {KC_SELECT, KC_ALGO, KC_PRESETS};
    int found = -1;
    unsigned total = bb_nav_help(s, &pf, rec, 0, 0, 0, s->explain, &found), i, n = 0;
    uint8_t roles[4]; const char *lab[4];
    in.help_pages = (uint8_t)((total + BB_UI_HELP_ROWS - 1u) / BB_UI_HELP_ROWS);
    in.help_page = (uint8_t)page;
    (void)bb_nav_help(s, &pf, rec, hnav, page * BB_UI_HELP_ROWS, BB_UI_HELP_ROWS, 0, 0);
    in.help_rows = (uint8_t)(total - page * BB_UI_HELP_ROWS > BB_UI_HELP_ROWS ? BB_UI_HELP_ROWS : total - page * BB_UI_HELP_ROWS);
    bb_nav_knobs(s, roles, lab);
    hknobs[0] = 0;
    for (i = 0; i < 4u; i++) if (lab[i][0]) n += (unsigned)snprintf(hknobs + n, sizeof hknobs - n, "%s%s", n ? " " : "", lab[i]);
    for (i = 0; i < in.help_rows; i++) {
        const bb_nav_help_row *r = &hnav[i]; bb_ui_help_row *u = &hrow[i];
        u->kc = u->kc2 = BB_UI_NO; u->rec = r->rec; u->keys = 0; u->text = bb_nav_help_text(r->text);
        if (r->kind == BB_NAV_HR_KEYS) { keyname(hkeys[i], r->a, r->b); u->keys = hkeys[i]; }
        else if (r->kind == BB_NAV_HR_HELD_K4) { u->kc = KC_HOLD; u->kc2 = KC_K4; }
        else if (r->kind == BB_NAV_HR_BUTTON) { u->kc = r->a == BB_NAV_B_OCTDN ? KC_OCTPM : KCB[r->a]; if (r->a == BB_NAV_B_REC) u->rec = 0; }
        else if (r->kind == BB_NAV_HR_KNOBS) { u->kc = KC_K14; u->keys = hknobs; }
        else u->kc = KCE[r->a < 3u ? r->a : 0];
    }
    in.help_row = hrow; in.help_sel = BB_UI_NO; in.help_note = 0;
    in.help_word = bb_nav_help_word(s);        /* RC1: as bb_v3_help_fill */
    if (s->explain && found >= 0 && (unsigned)found / BB_UI_HELP_ROWS == page) {
        in.help_sel = (uint8_t)((unsigned)found % BB_UI_HELP_ROWS);
        snprintf(hnote, sizeof hnote, "%s: %s", hkeys[in.help_sel][0] ? hkeys[in.help_sel] : "K", hrow[in.help_sel].text);
        in.help_note = hnote;
    }
}
static void tabs_fill(const bb_nav_state *s)
{
    in.tab = 0; in.tabs = 0; in.tab_note = 0;
    if (s->page == BB_NAV_FX) {
        bb_nav_state t = *s;
        for (unsigned i = 0; i < 5u; i++) { t.bank = (uint8_t)i; tabs[i] = bb_nav_bank_name(&t); }
        in.tab = tabs; in.tabs = 5; in.tab_cur = s->bank; in.tab_note = s->bank == 1u ? "ECHO ONLY \xB7 DRY DRIVE: SOUND" : 0;
    } else if (s->page == BB_NAV_EVOLVE) {
        for (unsigned i = 0; i < BB_NAV_MODES; i++) tabs[i] = bb_nav_mode_name((uint8_t)i);
        in.tab = tabs; in.tabs = BB_NAV_MODES; in.tab_cur = s->gen_mode; in.tab_note = "LFO OR B3";
    }
}
static const char *const ITEMS[] = {"Planet Rock", "Blue Monday", "Egypt, Egypt", "Tainted Love", "Numbers"};
static const char *const SECS[] = {"INTRO", "VERSE", "HOOK", "BREAK", "OUTRO"};
static void base(const bb_nav_state *s, uint8_t row, uint8_t page)
{
    uint8_t roles[4]; const char *lab[4];
    memset(&in, 0, sizeof in); bb_v3_route_page = page; perf_on = 0;
    in.now_ms = 100000; in.page = page; in.instr = row < 2 ? row : BB_UI_INST_DILLO;
    in.instr_name = row == 0 ? "KIT A" : row == 1 ? "KIT B" : "DILLO";
    static char pname[24];                     /* RC1: the firmware's own status text (bb_v3_common), not the bare page */
    (void)bb_nav_status_name(s, pname, sizeof pname); in.page_name = pname;
    in.running = 1; in.beat = 1; in.ticks = 37; in.bpm_x10 = 1200; in.level = 120;
    in.events = BB_EV_INPUT;                   /* a button was just used: HOME, not attract */
    bb_nav_knobs(s, roles, lab);
    set_legend(s);
    for (int k = 0; k < 4; k++) {
        snprintf(ktext[k], sizeof ktext[k], "%d", 40 + 15 * k);
        in.knob[k].label = lab[k][0] ? lab[k] : 0; in.knob[k].text = ktext[k];
        in.knob[k].bar = (uint8_t)(80 + 40 * k); in.knob[k].flags = lab[k][0] ? 0 : BB_UI_KN_OFF;
    }
    for (int k = 0; k < 16; k++) in.key_led[k] = k % 4 == 0 ? BB_UI_LED_LIT : BB_UI_LED_DIM;
    in.knob_turning = BB_UI_NO;
    /* page data (example content) */
    in.lane[0].name = "KIT A"; in.lane[0].pattern = "Planet Rock"; in.lane[0].part = "HOOK 2/4"; in.lane[0].level = 180;
    in.lane[1].name = "KIT B"; in.lane[1].pattern = "Off"; in.lane[1].flags = BB_UI_LN_OFF;
    in.lane[2].name = "DILLO"; in.lane[2].pattern = "Rubber Acid"; in.lane[2].part = "A"; in.lane[2].level = 140;
    in.pattern_name = "Planet Rock"; in.section_name = "HOOK"; in.bar = 2; in.bars = 4;
    for (int g = 0; g < 4; g++) { in.gsteps[g] = (uint16_t)(0x8888u >> g | 0x0101u << g); in.group_level[g] = 200; in.group_out[g] = 120; }
    in.out_peak[0] = in.out_peak[1] = 150; in.dsp = 160;
    in.voice_name = "KICK"; in.voice = 0; in.banks = 2;
    for (int v = 0; v < 11; v++) in.vsteps[v] = (uint16_t)(0x1111u << (v % 4)); in.vacc[0] = 0x0101;
    in.patch = "Rubber Acid"; in.scale = "A MIN";
    for (int k = 0; k < 16; k++) in.note[k] = (int8_t)(k % 3 ? (k * 5) % 12 : -1);
    in.cutoff = 140; in.reso = 120; in.envmod = 90;
    in.echo_ms = 375; in.repeats = 150; in.heads = 5; in.banks_fx = 5; in.bank_fx = s->bank;
    tabs_fill(s);
    memset(&pf, 0, sizeof pf); pf.section = 1; pf.section_count = 6; pf.list_count = 9; pf.list_playing = 1;
    pf.list_cursor = 3; pf.kit_slot = pf.kit_slot_queued = pf.section_queued = pf.list_queued = BB_UI_NO;
    pf.step_banks = 2; pf.steps = 0x1111; pf.running = 1; pf.evo_parts = 1; pf.evo_moves = 3; pf.gen_mode = s->gen_mode;
    pf.song = BB_NAV_SONG_F_ROWS | BB_NAV_SONG_F_UNDO;
    play_example(s, row, page);
}
/* 2.1 P1 frames: the key map strip is the LEDs from bb_nav (the one table) */
static void real_leds(const bb_nav_state *s)
{
    bb_nav_led_out l;
    bb_nav_leds(s, &pf, 0, &l);
    for (int k = 0; k < 27; k++) in.key_led[k] = l.key[k];
}
/* 2.2 playability: example data for the playing pages (bb_ui_play), the LEDs from bb_nav */
static bb_ui_play play;
static void play_example(const bb_nav_state *s, uint8_t row, uint8_t page)
{
    static const char *const N[11] = {"BASS DRUM", "SNARE", "LOW TOM", "MID TOM", "HIGH TOM", "RIMSHOT", "HAND CLAP",
                                      "COWBELL", "CYMBAL", "OPEN HAT", "CLOSED HAT"};
    static const uint8_t G[11] = {0, 1, 3, 3, 3, 1, 1, 3, 2, 2, 2};
    static const int8_t L[16] = {0, 0, 3, 0, 7, 6, 0, -2, 0, 0, 3, 5, 7, 10, 12, 0};
    uint8_t a;
    memset(&play, 0, sizeof play);
    for (int i = 0; i < 11; i++) { play.pad_name[i] = N[i]; play.pad_group[i] = G[i]; }
    play.pad_lit = 1u | 1u << 6 | 1u << 10;
    in.group_muted = 1u << 2; pf.group_muted = 1u << 2;          /* HATS muted: B3 shows MUTE */
    play.keys = s->play_keys == BB_NAV_PLAY_LIVE;
    for (uint8_t k = 0; k < 27; k++) {
        uint8_t r = bb_nav_key_role(s, k, 0, &a);
        play.key_note[k] = r == BB_NAV_K_NOTE || r == BB_NAV_K_STEP_PITCH ? a : 0;
    }
    play.keys_sounding = 1u << 2;
    for (int k = 0; k < 16; k++) play.step_note[k] = (uint8_t)(45 + L[k]);
    play.step_gate = 0x7FFF; play.step_held = 1u << 5;
    in.note_acc = 0x1111; in.note_slide = 1u << 2 | 1u << 5 | 1u << 13;
    in.ratchet[12] = 3; in.ratchet[4] = BB_UI_RATCHET_MIXED;
    play.echo_ms = 375; play.fx[BB_PL_FDBK] = 170; play.heads = 5; play.fx[BB_PL_WET] = 140; play.fx[BB_PL_TONE] = 128; play.fx[BB_PL_DRIVE] = 190; play.fx[BB_PL_WEAR] = 80;
    play.fx[BB_PL_MOTION] = 150; play.fx[BB_PL_SIZE] = 150; play.fx[BB_PL_DAMP] = 90; play.fx[BB_PL_SPRING_WET] = 128;
    play.send[0][0] = 100; play.send[0][1] = 140; play.send[1][0] = 60; play.send[1][1] = 20;
    play.send[2][0] = 200; play.send[2][1] = 90; play.focus = row;
    in.envmod = 150;
    bb_ui_play_set(&play);
    if (page == BB_UI_PLAY_DRUMS || page == BB_UI_STEPS || page == BB_UI_PLAY_DILLO || page == BB_UI_FX) {
        bb_nav_state f = *s;                                          /* the firmware's features: no FILL, no favourite kits */
        bb_nav_set_features(&f, BB_NAV_F_TAP | BB_NAV_F_SCALE | BB_NAV_F_THROW | BB_NAV_F_STEP_PITCH);
        real_leds(&f);
    }
}
static const char *const SONGS10[] = {"Planet Rock", "Blue Monday", "Sexual Healing", "In The Air Tonight", "Voodoo Ray",
                                      "Paul Revere", "Clear", "Egypt, Egypt", "It's Yours", "Posse On Broadway"};
static const char *const KITS10[] = {"808", "LinnDrum", "DrumTraks", "DMX", "808 Tight Hats", "LM-1", "CR-78", "Simmons SDS-V",
                                     "9 Oh 9", "TR-707"};
static const char *const DILLO10[] = {"Rubber Acid", "Squelch", "Sub Line", "Hoover", "Glass Lead", "Wobble", "Detroit Pad",
                                      "Moog Low", "Square Bass", "Bell"};
/* RC2 (Joe 10:09): a chooser page is the list: ten rows, the playing one marked, white keys, one footer line */
static void browse(uint8_t page)
{
    const char *const *it = page == BB_UI_KIT ? (in.instr == BB_UI_INST_DILLO ? DILLO10 : KITS10) : SONGS10;
    in.items = 10; for (int i = 0; i < 10; i++) { in.item[i] = it[i]; in.item_key[i] = in.instr == BB_UI_INST_DILLO && page == BB_UI_KIT ? 0 : (uint8_t)(i + 1); }
    in.cursor = 2; in.playing_item = 0; in.queued_item = BB_UI_NO;
    in.footer = page == BB_UI_KIT ? (in.instr == BB_UI_INST_DILLO ? "SOUNDS 3/24 \xB7 PRESETS \xB7 EDIT keep" :
                                     "KITS 3/40 \xB7 keys 1-16 \xB7 EDIT choose") : "SONGS 3/67 \xB7 keys 1-16 \xB7 EDIT choose";
}
/* RC2 (Joe 10:17-10:36): PERFORM, with n sections on the page (names from a song) */
static const char *const SECS16[] = {"Intro", "Break", "Verse", "Chorus", "Main", "Fill 1", "Fill 2", "Bridge", "Drop", "Outro",
                                     "Verse 2", "Break 2", "Main 2", "Fill 3", "Hook", "Ending"};
static void perform(unsigned n, const char *const *names, unsigned playing, unsigned queued, unsigned page)
{
    in.perf_song = in.instr == BB_UI_INST_DILLO ? "RUBBER ACID / EGYPT, EGYPT" :
                   in.instr == BB_UI_INST_B ? "DRUMTRAKS / EGYPT, EGYPT" : "LINNDRUM / EGYPT, EGYPT";
    in.tile_page = (uint8_t)page;
    for (unsigned k = 0; k < 16; k++) {
        in.tile[k] = k < n ? names[k] : 0;
        in.tile_state[k] = (uint8_t)(k >= n ? BB_UI_TILE_NONE : k == playing ? BB_UI_TILE_PLAYING : k == queued ? BB_UI_TILE_QUEUED : BB_UI_TILE_HALF);
    }
    perf_on = 1;   /* RC2 r2: the status line is bb_ui_perform_status of these buttons and lanes (shot() fills it) */
    in.footer = "ARP PLAY \xB7 SEQ STEPS \xB7 EDIT SONGS";
    for (unsigned r = 0; r < 3; r++) { in.lane[r].name = playing < n ? names[playing] : ""; in.lane[r].flags = (uint8_t)(r == 0 ? BB_UI_LN_FOCUS : 0); }
    if (queued != BB_UI_NO)
        for (unsigned r = 0; r < 3; r++) in.lane[r].flags |= BB_UI_LN_NEXT;
}

int main(int argc, char **argv)
{
    if (argc > 2 && !strcmp(argv[1], "-w")) {                 /* text widths in AF_S (footer budget 224) */
        for (int i = 2; i < argc; i++) printf("%3d %s\n", (int)text_w(&AF_S, argv[i]), argv[i]);
        return 0;
    }
    out = fopen(argc > 1 ? argv[1] : "captures.raw", "wb");
    static const char *const PN[] = {"studio", "play", "kit", "pattern", "sound", "fx", "save", "evolve", "global", "song"};
    static const char *const RN[] = {"a", "b", "dillo"};
    bb_ui_init(&u, 0, 1);
    {
        bb_nav_state s; bb_nav_init(&s,BB_NAV_STUDIO,600); s.bank=BB_NAV_HOME_SONGS;
        base(&s,0,BB_UI_PATTERN); browse(BB_UI_PATTERN); in.welcome=1; in.page_name="CONDUCTOR";
        static const char *const collections[]={"SONGS","ERAS","PRESETS"};
        in.tab=collections;in.tabs=3;in.tab_cur=0;
        in.footer="SONGS 1 of 67 - PLAY: all three"; in.running=0; in.sections=8;
        shot("welcome-song");
        in.notice="BUSY - RETRY"; shot("welcome-refused");
        in.notice=0; in.cursor=0; shot("welcome-first-song");
        in.events=0; in.knob_turning=BB_UI_NO;
        incremental_equal("welcome settled");
        in.cursor=2; incremental_equal("welcome cursor");
        in.sections=1; incremental_equal("welcome section count");
        in.notice="BUSY - RETRY"; incremental_equal("welcome refused");
        in.notice=0; incremental_equal("welcome notice cleared");
        in.welcome=0; incremental_equal("welcome to list");
        in.welcome=1; incremental_equal("list to welcome");
        in.help=1; help_fill(&s,0,0); shot("welcome-help");
    }
    {   /* 2.1 instrument-first HOME: prove the actual landing states, not merely
         * the deeper PLAY/PERFORM pages. SELECT must reveal three visibly
         * different instruments and the written destination name must agree. */
        bb_nav_state s; uint16_t landing[3][240 * 240];

        bb_nav_init(&s, BB_NAV_STUDIO, 600);
        base(&s, 0, BB_UI_STUDIO); perform(8, SECS16, 4, BB_UI_NO, 0);
        if (strcmp(in.page_name, "CONDUCTOR")) FAIL("HOME Conductor title is \"%s\"", in.page_name);
        shot("home-conductor"); memcpy(landing[0], host_screen, sizeof landing[0]);

        bb_nav_init(&s, BB_NAV_PLAY, 600);
        bb_nav_set_instrument(&s, BB_NAV_INSTR_DRUM, 2); bb_nav_set_focus(&s, 0);
        base(&s, 0, BB_UI_PLAY_DRUMS);
        if (strcmp(in.page_name, "BIG BEAT")) FAIL("HOME Big Beat title is \"%s\"", in.page_name);
        shot("home-big-beat"); memcpy(landing[1], host_screen, sizeof landing[1]);

        bb_nav_init(&s, BB_NAV_PLAY, 600);
        bb_nav_set_instrument(&s, BB_NAV_INSTR_DILLO, 1); bb_nav_set_focus(&s, 2);
        base(&s, 2, BB_UI_PLAY_DILLO);
        if (strcmp(in.page_name, "DILLO")) FAIL("HOME Dillo title is \"%s\"", in.page_name);
        shot("home-dillo"); memcpy(landing[2], host_screen, sizeof landing[2]);

        if (!memcmp(landing[0], landing[1], sizeof landing[0]) ||
            !memcmp(landing[0], landing[2], sizeof landing[0]) ||
            !memcmp(landing[1], landing[2], sizeof landing[0]))
            FAIL("HOME destinations are not three visibly distinct frames");
    }

    for (uint8_t row = 0; row < 3; row++)
        for (uint8_t page = 0; page < 9; page++)
            for (uint8_t keys = 0; keys < 2; keys++) {
                if (keys && page != BB_NAV_PLAY) continue;
                uint8_t banks = page == BB_NAV_FX ? 5 : (page == BB_NAV_SOUND && row < 2) || page == BB_NAV_STUDIO ? 2 : 1;   /* 2.1 P3b: HOME CONDUCT, SONGS */
                for (uint8_t bank = 0; bank < banks; bank++) {
                    bb_nav_state s; bb_nav_out o; char name[64];
                    bb_nav_init(&s, BB_NAV_STUDIO, 600);
                    bb_nav_set_instrument(&s, row < 2 ? BB_NAV_INSTR_DRUM : BB_NAV_INSTR_DILLO, row < 2 ? 2 : 1);
                    bb_nav_set_focus(&s, row);
                    memset(&o, 0, sizeof o); bb_nav_goto(&s, page, &o);
                    s.bank = bank; s.play_keys = keys;
                    uint8_t v3 = bb_v3_route(&s, 0);
                    if (v3 == BB_UI_NO) { FAIL("%s routes to legacy", PN[page]); continue; }
                    base(&s, row, v3);
                    if (v3 == BB_UI_KIT || v3 == BB_UI_PATTERN) browse(v3);
                    if (page == BB_NAV_STUDIO && bank == BB_NAV_HOME_SONGS) {   /* RC2: the collections as tabs */
                        static const char *const coll[3] = {"SONGS", "ERAS", "PRESETS"};
                        in.tab = coll; in.tabs = 3; in.tab_cur = 0; in.footer = "SONGS 1/67 · PLAY plays it on all three";
                    }
                    if (v3 == BB_UI_STUDIO) perform(8, SECS16, 4, BB_UI_NO, 0);
                    if (v3 == BB_UI_KNOBS) { in.title = page == BB_NAV_SAVE ? "SCENE 2" : page == BB_NAV_GLOBAL ? "GLOBAL" :
                                                     page == BB_NAV_EVOLVE ? "EVOLVE" :
                                                     page == BB_NAV_SOUND ? (row == 2 ? "Rubber Acid" : "KICK") : 0;
                                             in.subtitle = page == BB_NAV_EVOLVE ? "EDIT: CAPTURE" : page == BB_NAV_FX ? 0 : "EDIT: STORE"; }
                    snprintf(name, sizeof name, "%s-%s%s%s%s", PN[page], RN[row], keys ? "-steps" : "",
                             banks > 1 && *bb_nav_bank_name(&s) ? "-" : "", banks > 1 ? bb_nav_bank_name(&s) : "");
                    for (char *c = name; *c; c++) if (*c >= 'A' && *c <= 'Z') *c = (char)(*c + 32);
                    shot(name);
                    if (row == 0 && keys == 0 && bank == 0) {           /* the overlays once per page */
                        in.help = 1; help_fill(&s, 0, 0); snprintf(name, sizeof name, "%s-help", PN[page]); shot(name); in.help = 0;
                        in.notice = "BUSY / SELECT AGAIN"; snprintf(name, sizeof name, "%s-notice", PN[page]); shot(name); in.notice = 0;
                    }
                    if (bank == 0 && row != 1 && legend[0]) {               /* 2.0 D4: the transient key legend */
                        uint32_t ev = in.events;
                        in.events |= BB_EV_LEGEND;
                        snprintf(name, sizeof name, "%s-%s%s-legend", PN[page], RN[row], keys ? "-steps" : ""); shot(name);
                        in.events = ev;
                    }
                }
            }
    /* RC1 (P4 manual lane): each HELP titles its own page. HOME/SONGS (the PATTERN list layout) said HELP \xB7 PATTERN,
     * the Dillo PATCH page (the KIT list) HELP \xB7 KIT, the kit builder's BUILD and SAVE AS HELP \xB7 KIT. */
    {
        static const struct { uint8_t page, bank, row; const char *name, *want; } H[] = {
            {BB_NAV_STUDIO, BB_NAV_HOME_SONGS, 0, "studio-songs-help", "HELP \xB7 SONGS"},
            {BB_NAV_STUDIO, BB_NAV_HOME_CONDUCT, 0, "studio-conduct-help", "HELP \xB7 PERFORM"},
            {BB_NAV_KIT, 0, 2, "kit-dillo-help", "HELP \xB7 PATCH"},
            {BB_NAV_KIT, 0, 0, "kit-a-help", "HELP \xB7 KIT"},
#if BB_WITH_USER_KITS
            {BB_NAV_KIT, BB_NAV_KIT_BUILD, 0, "kit-build-help", "HELP \xB7 BUILD"},
            {BB_NAV_KIT, BB_NAV_KIT_SAVE_AS, 0, "kit-save-as-help", "HELP \xB7 SAVE AS"},
#endif
        };
        for (unsigned i = 0; i < sizeof H / sizeof H[0]; i++) {
            bb_nav_state s; bb_nav_out o; bb_nav_init(&s, BB_NAV_STUDIO, 600);
            bb_nav_set_instrument(&s, H[i].row < 2 ? BB_NAV_INSTR_DRUM : BB_NAV_INSTR_DILLO, H[i].row < 2 ? 2 : 1);
            bb_nav_set_focus(&s, H[i].row); memset(&o, 0, sizeof o); bb_nav_goto(&s, H[i].page, &o); s.bank = H[i].bank;
            uint8_t v3 = bb_v3_route(&s, 0);
            base(&s, H[i].row, v3); if (v3 == BB_UI_KIT || v3 == BB_UI_PATTERN) browse(v3);
            if (v3 == BB_UI_STUDIO) perform(8, SECS16, 4, BB_UI_NO, 0);
            in.help = 1; help_fill(&s, 0, 0); shot(H[i].name); in.help = 0;
            unsigned seen = 0;
            for (unsigned t = 0; t < ntext; t++) if (!strncmp(texts[t].s, "HELP \xB7 ", 7)) { seen++; if (strcmp(texts[t].s, H[i].want)) FAIL("%s: title \"%s\", want \"%s\"", H[i].name, texts[t].s, H[i].want); }
            if (seen != 1) FAIL("%s: %u help titles", H[i].name, seen);
        }
    }
    /* loading states and the fault */
    {
        bb_nav_state s; bb_nav_out o; bb_nav_init(&s, BB_NAV_STUDIO, 600); bb_nav_set_focus(&s, 0);
        memset(&o, 0, sizeof o); bb_nav_goto(&s, BB_NAV_KIT, &o);
        base(&s, 0, BB_UI_KIT); browse(BB_UI_KIT); in.queued_item = 2; in.loading = 1; in.progress = 140; shot("kit-a-queued");
        in.title = "SOUND LIST NOT READY"; in.subtitle = 0; in.items = 0; in.loading = 0; shot("kit-a-not-ready");
        memset(&o, 0, sizeof o); bb_nav_goto(&s, BB_NAV_PATTERN, &o);
        base(&s, 0, BB_UI_PATTERN); browse(BB_UI_PATTERN); in.subtitle = "NEXT KIT: DrumTraks"; in.queued_item = 1; shot("pattern-a-next-kit");
        base(&s, 1, BB_UI_PATTERN); browse(BB_UI_PATTERN); in.title = "Egypt, Egypt"; in.subtitle = "SONG \xB7 Part 9  SUBST ZAP>HT +1"; shot("pattern-b-substitution");
        in.subtitle = "NEXT KIT: DrumTraks  SUBST MC>MT +2"; shot("pattern-b-next-kit-substitution");
        in.title = "PATTERNS NOT READY"; in.subtitle = 0; in.items = 0; in.sections = 0; shot("pattern-a-not-ready");
        base(&s, 0, BB_UI_PLAY_DRUMS); in.fault = 1; in.fault_text = "Audio fault: load blocked"; shot("play-a-fault");
    }
    {   /* r9.10 brand type: the knob pop-up's large value (Picnic 26) on its own line under the label, a value
         * the face lacks a glyph for ("1/8": whole string falls back to the system face), and MUTE. */
        bb_nav_state s; bb_nav_out o; bb_nav_init(&s, BB_NAV_STUDIO, 600); bb_nav_set_focus(&s, 0);
        memset(&o, 0, sizeof o); bb_nav_goto(&s, BB_NAV_PLAY, &o);
        static const char *const VAL[] = {"115", "1/8", "0"};
        for (unsigned i = 0; i < 3; i++) {
            char nm[40]; base(&s, 0, BB_UI_PLAY_DRUMS);
            snprintf(ktext[1], sizeof ktext[1], "%s", VAL[i]); in.knob[1].text = ktext[1];
            if (i == 2) in.knob[1].flags = BB_UI_KN_MUTE;
            in.knob_turning = 1; in.events |= BB_EV_KNOB; in.ev_knob = 1;
            snprintf(nm, sizeof nm, "play-a-popup-%u", i); shot(nm);
        }
        in.knob_turning = BB_UI_NO;
    }
    {   /* 2.0 D4. (1) A turned knob shows NAME + value for about 1 s on every page: the pop-up now also on the KNOBS
         * pages (SOUND) and, below, SONG. (2) The generator's own knobs on EVOLVE (EUCLID, RANDOM). (3) Every new
         * QUEUED / APPLIED notice. (4) The transient legend on the kit builder's pages, and HOME keeping the quick
         * start and the Dillo NEXT line (no legend over them). */
        bb_nav_state s; bb_nav_out o; char nm[48];
        bb_nav_init(&s, BB_NAV_STUDIO, 600); bb_nav_set_focus(&s, 0);
        memset(&o, 0, sizeof o); bb_nav_goto(&s, BB_NAV_SOUND, &o);
        base(&s, 0, BB_UI_KNOBS); in.title = "KICK"; in.subtitle = "EDIT: STORE";
        in.knob_turning = 1; in.events |= BB_EV_KNOB; in.ev_knob = 1; shot("sound-a-turn");
        in.knob_turning = BB_UI_NO;
        memset(&o, 0, sizeof o); bb_nav_goto(&s, BB_NAV_EVOLVE, &o);
        static const char *const EU[4] = {"HITS", "STEPS", "ROTATE", "ACCENTS"}, *const RA[4] = {"DENSITY", "ACCENT", "ANCHOR", "SEED"};
        static const char *const EUV[4] = {"5", "16", "0", "2"}, *const RAV[4] = {"70", "30", "100", "9999"};
        for (unsigned g = 0; g < 2; g++) {
            base(&s, 0, BB_UI_KNOBS); in.title = g ? "RANDOM" : "EUCLID"; in.subtitle = "EDIT: APPLY";
            for (int k = 0; k < 4; k++) { in.knob[k].label = g ? RA[k] : EU[k]; in.knob[k].text = g ? RAV[k] : EUV[k]; in.knob[k].flags = 0; }
            in.knob_turning = 3; in.events |= BB_EV_KNOB; in.ev_knob = 3;
            snprintf(nm, sizeof nm, "evolve-a-%s-turn", g ? "random" : "euclid"); shot(nm);
        }
        in.knob_turning = BB_UI_NO;
        static const uint16_t NOTE[] = {TX_NEXT_SECTION_QUEUED, TX_SECTION_APPLIED, TX_VARIATION_QUEUED_B2_RETURN,
                                        TX_VARIATION_APPLIED, TX_KIT_QUEUED, TX_KIT_APPLIED, TX_SOUND_QUEUED, TX_SOUND_APPLIED};
        static const char *const NOTE_N[] = {"section-queued", "section-applied", "variation-queued", "variation-applied",
                                             "kit-queued", "kit-applied", "sound-queued", "sound-applied"};
        static const uint8_t NOTE_PAGE[] = {BB_NAV_STUDIO, BB_NAV_STUDIO, BB_NAV_EVOLVE, BB_NAV_EVOLVE, BB_NAV_PATTERN,
                                            BB_NAV_PLAY, BB_NAV_PATTERN, BB_NAV_PATTERN};
        for (unsigned i = 0; i < sizeof NOTE / sizeof NOTE[0]; i++) {
            uint8_t row = i >= 6 ? 2 : 0, v3;
            bb_nav_init(&s, BB_NAV_STUDIO, 600);
            bb_nav_set_instrument(&s, row < 2 ? BB_NAV_INSTR_DRUM : BB_NAV_INSTR_DILLO, row < 2 ? 2 : 1); bb_nav_set_focus(&s, row);
            memset(&o, 0, sizeof o); bb_nav_goto(&s, NOTE_PAGE[i], &o);
            v3 = bb_v3_route(&s, 0); base(&s, row, v3);
            if (v3 == BB_UI_PATTERN) browse(v3);
            if (v3 == BB_UI_KNOBS) { in.title = "EVOLVE  DRUM A"; in.subtitle = "EDIT: CAPTURE"; }
            in.notice = bb_tx(NOTE[i]);
            snprintf(nm, sizeof nm, "notice-%s", NOTE_N[i]); shot(nm); in.notice = 0;
        }
#if defined(BB_WITH_USER_KITS) && BB_WITH_USER_KITS
        for (uint8_t b = BB_NAV_KIT_BUILD; b <= BB_NAV_KIT_SAVE_AS; b++) {
            bb_nav_init(&s, BB_NAV_STUDIO, 600); bb_nav_set_focus(&s, 0);
            memset(&o, 0, sizeof o); bb_nav_goto(&s, BB_NAV_KIT, &o); s.bank = b;
            base(&s, 0, BB_UI_KIT); browse(BB_UI_KIT); in.page_name = bb_nav_page_name(&s);
            in.title = b == BB_NAV_KIT_BUILD ? "USER 1" : "SAVE AS"; in.subtitle = "EDIT: SAVE AS";
            in.playing_item = BB_UI_NO;
            in.events |= BB_EV_LEGEND; shot(b == BB_NAV_KIT_BUILD ? "kit-a-build-legend" : "kit-a-save-as-legend");
        }
#endif
        bb_nav_init(&s, BB_NAV_STUDIO, 600); bb_nav_set_focus(&s, 0);
        /* HOME leaves the band to the Dillo NEXT line and the quick start even when the legend is due */
        base(&s, 0, BB_UI_STUDIO); in.lane[2].next = "Acid Rain"; in.events |= BB_EV_LEGEND; shot("studio-a-dillo-next-legend");
        base(&s, 0, BB_UI_STUDIO); in.quick_start = 1; in.running = 0; in.events |= BB_EV_LEGEND; shot("studio-a-quick-start-legend");
        {   /* SONG (bb_arranger_draw.inc): its title line is its key legend; BARS turned */
            static const char *const ROWS[] = {"01 INTRO", "02 VERSE", "03 HOOK"}, *const L[4] = {"ROW", "REPEATS", "BARS", "LOOP"},
                              *const V[4] = {"2", "1", "4", "ON"};
            base(&s, 0, BB_UI_ARRANGE); in.page_name = "SONG"; in.legend = 0;
            in.pattern_name = "VERSE"; in.next_name = "HOOK"; in.subtitle = "RAM PROJECT / ADD A SECTION";
            in.title = "KEY 1 ADD 3 COPY 4 DELETE"; in.items = 3;
            for (int i = 0; i < 3; i++) in.item[i] = ROWS[i];
            in.cursor = 1; in.playing_item = 1; in.queued_item = 2;
            for (int k = 0; k < 4; k++) { in.knob[k].label = L[k]; in.knob[k].text = V[k]; in.knob[k].flags = 0; }
            in.knob_turning = 2; in.events |= BB_EV_KNOB; in.ev_knob = 2; shot("song-turn"); in.knob_turning = BB_UI_NO;
        }
    }
    {   /* 2.1 Live Play P1: the new and changed screens, key strip = bb_nav's LEDs */
        bb_nav_state s; bb_nav_out o; char nm[48]; uint8_t v3;
        for (uint8_t m = 0; m < BB_NAV_MODES; m++) {                       /* EVOLVE: the mode is the title, tabs */
            static const char *const cap[3] = {"EDIT: CAPTURE", "EDIT: APPLY", "EDIT: APPLY"};
            bb_nav_init(&s, BB_NAV_STUDIO, 600); bb_nav_set_focus(&s, 0); memset(&o, 0, sizeof o); bb_nav_goto(&s, BB_NAV_EVOLVE, &o);
            s.gen_mode = m; base(&s, 0, BB_UI_KNOBS); real_leds(&s);
            in.title = bb_nav_mode_name(m); in.subtitle = cap[m];
            snprintf(nm, sizeof nm, "p1-evolve-%s", m == 0 ? "evolve" : m == 1 ? "euclid" : "random"); shot(nm);
        }
        for (uint8_t t = 0; t < 5; t++) {                                  /* FX: the five tabs on white 1-5 */
            bb_nav_init(&s, BB_NAV_STUDIO, 600); bb_nav_set_focus(&s, 0); memset(&o, 0, sizeof o); bb_nav_goto(&s, BB_NAV_FX, &o);
            s.bank = t; v3 = bb_v3_route(&s, 0); base(&s, 0, v3); real_leds(&s);
            if (v3 == BB_UI_KNOBS) { in.title = 0; in.subtitle = 0; }
            snprintf(nm, sizeof nm, "p1-fx-tab-%u", t + 1u); shot(nm);
        }
        for (uint8_t b = 0; b < 3; b++) {                                  /* GLOBAL, MIDI CHANNELS, MIDI CLOCK */
            static const char *const T[3] = {"GLOBAL", "MIDI CHANNELS", "MIDI CLOCK"};
            char leg[64];
            bb_nav_init(&s, BB_NAV_STUDIO, 600); bb_nav_set_focus(&s, 0); memset(&o, 0, sizeof o); bb_nav_goto(&s, BB_NAV_GLOBAL, &o);
            s.bank = b; base(&s, 0, BB_UI_KNOBS); real_leds(&s);
            (void)bb_nav_legend(&s, leg, sizeof leg, 1); in.title = T[b]; in.subtitle = leg;
            snprintf(nm, sizeof nm, "p1-global-%s", b == 0 ? "main" : b == 1 ? "midi" : "clock"); shot(nm);
        }
        for (uint8_t b = 0; b < 2; b++) {                                  /* SONG rows and ADD: the title is the legend */
            static const char *const ROWS[] = {"01 INTRO", "02 VERSE", "03 HOOK"};
            static char leg[64];
            bb_nav_init(&s, BB_NAV_STUDIO, 600); bb_nav_set_focus(&s, 0); memset(&o, 0, sizeof o); bb_nav_goto(&s, BB_NAV_SONG, &o);
            s.bank = b; base(&s, 0, BB_UI_ARRANGE); real_leds(&s); in.legend = 0;
            in.page_name = b ? "SONG / ADD" : "SONG \xB7 HOME";
            (void)bb_nav_legend(&s, leg, sizeof leg, 1);
            if (text_w(&AF_S, leg) > 224) (void)bb_nav_legend(&s, leg, sizeof leg, 2);
            if (text_w(&AF_S, leg) > 224) (void)bb_nav_legend(&s, leg, sizeof leg, 3);
            in.title = leg; in.pattern_name = "VERSE"; in.next_name = "HOOK"; in.subtitle = "RAM PROJECT / ADD A SECTION";
            in.items = b ? 2 : 3; for (int i = 0; i < 3; i++) in.item[i] = ROWS[i];
            in.cursor = 1; in.playing_item = b ? BB_UI_NO : 1; in.queued_item = BB_UI_NO;
            snprintf(nm, sizeof nm, "p1-song-%s", b ? "add" : "rows"); shot(nm);
            in.help = 1; help_fill(&s, 0, 0); snprintf(nm, sizeof nm, "p1-song-%s-help", b ? "add" : "rows"); shot(nm); in.help = 0;
        }
        {   /* HELP: press to explain (black 2 on PLAY drums), its second page, and REC held (the REC chords) */
            bb_nav_init(&s, BB_NAV_STUDIO, 600); bb_nav_set_focus(&s, 0); memset(&o, 0, sizeof o); bb_nav_goto(&s, BB_NAV_PLAY, &o);
            base(&s, 0, BB_UI_PLAY_DRUMS); in.help = 1;
            s.help = 1; s.explain = BB_NAV_EX_KEY | BB_NAV_BLACK(2); help_fill(&s, 0, 0); shot("p1-help-play-explain-b2");
            s.explain = 0; help_fill(&s, 0, 1); shot("p1-help-play-page-2");
            help_fill(&s, 0, 2); shot("p1-help-play-page-3");
            s.help = 0; help_fill(&s, 1, 0); shot("p1-help-play-rec");
            bb_nav_goto(&s, BB_NAV_GLOBAL, &o); s.bank = BB_NAV_GLOBAL_MIDI; base(&s, 0, BB_UI_KNOBS); in.help = 1;
            help_fill(&s, 0, 0); shot("p1-help-midi");
            bb_nav_goto(&s, BB_NAV_EVOLVE, &o); s.gen_mode = BB_NAV_MODE_EUCLID; base(&s, 0, BB_UI_KNOBS); in.help = 1;
            pf.gen_mode = BB_NAV_MODE_EUCLID; help_fill(&s, 0, 0); shot("p1-help-euclid");
            bb_nav_goto(&s, BB_NAV_FX, &o); s.bank = 1; base(&s, 0, BB_UI_FX); in.help = 1;
            help_fill(&s, 0, 0); shot("p1-help-fx");
            in.help = 0;
        }
    }
    {   /* STUDIO with real kit names (r9.9, #4 6471457725): a sampled kit reads as on the KIT page, and the
         * longest native and sample names must fit the lane next to the section name. */
        static const char *const KITS[][2] = {{"LinnDrum", "DrumTraks"}, {"DMX", "808 Tight Hats"}, {"808 Reference", "DMX"}};
        bb_nav_state s; bb_nav_init(&s, BB_NAV_STUDIO, 600);
        for (unsigned i = 0; i < 3; i++) {
            char nm[40]; base(&s, 0, BB_UI_STUDIO);
            in.lane[0].name = "Main 1"; in.lane[0].pattern = "Synth Pop"; in.lane[0].part = KITS[i][0];
            in.lane[1].name = "Main 2"; in.lane[1].pattern = "Electro Boogie"; in.lane[1].part = KITS[i][1]; in.lane[1].flags = 0;
            in.lane[2].name = "Main 1"; in.lane[2].pattern = "Synth Pop"; in.lane[2].part = "Open Circuit";
            snprintf(nm, sizeof nm, "studio-kits-%u", i); shot(nm);
        }
    }
    {   /* RC2: PERFORM's buttons (Joe 10:35/10:36). Every section name the catalogue has (ERAS, SONGS with the preset
         * shelves, ACID: 744) in a button of each layout (4 tall, 8, 16 narrow, any position): every text it draws lies inside
         * its button (no cut glyph, upright or on its side) and is not empty. The lights: for every song and section page,
         * the white keys lit (not dark) are exactly the buttons drawn. */
        bb_nav_state s; bb_nav_init(&s, BB_NAV_STUDIO, 600);
        unsigned names = 0, side = 0, abbr = 0, pages = 0;
        static unsigned size_hist[4];
        if (!bb_factory_init()) FAIL("catalogue not ready");
        for (unsigned g = 0; g < BB_BROWSER_GROUPS; g++) {
            bb_browser v; if (!bb_browser_init(&v, &bb_factory_catalogue) || !bb_browser_group(&v, g)) continue;
            for (unsigned k = 0; k < (unsigned)bb_browser_bank_count(&v); k++) {
                bb_browser c = v; if (k) bb_browser_move_bank(&c, (int)k);
                unsigned np = (unsigned)bb_browser_part_count(&c);
                for (unsigned p = 0; p * 16u < np; p++) {           /* the lights agree with the buttons */
                    bb_nav_state t; bb_nav_init(&t, BB_NAV_STUDIO, 600); t.bank = BB_NAV_HOME_CONDUCT;
                    bb_nav_facts f; memset(&f, 0, sizeof f);
                    f.kit_slot = f.kit_slot_queued = f.section_queued = f.section = f.list_cursor = f.list_playing = f.list_queued = BB_NAV_NONE_IDX;
                    f.section_count = (uint8_t)np; t.sec_count = (uint8_t)np; t.sec_page = (uint8_t)p;
                    bb_nav_led_out l; bb_nav_leds(&t, &f, 0, &l);
                    unsigned lit = 0, want = np - p * 16u > 16u ? 16u : np - p * 16u;
                    for (unsigned w = 0; w < 16u; w++) lit += l.key[w] != BB_NAV_LED_DARK;
                    if (lit != want) FAIL("lights: bank %u/%u page %u: %u keys lit, %u buttons", g, k, p, lit, want);
                    pages++;
                }
                for (unsigned i = 0; i < np; i++) {
                    char t[64] = "", nm[80];
                    c.part_page = (uint16_t)(i / BB_BROWSER_PART_KEYS);
                    if (!bb_browser_part_title(&c, i % BB_BROWSER_PART_KEYS, t, sizeof t) || !t[0]) { FAIL("section %u/%u/%u has no name", g, k, i); continue; }
                    names++;
                    for (unsigned lay = 0; lay < 3; lay++) {
                        static const unsigned N[3] = {4, 8, 16};
                        unsigned n = N[lay], pos = i % n;
                        const char *nm16[16]; for (unsigned q = 0; q < 16; q++) nm16[q] = SECS16[q];
                        nm16[pos] = t;
                        bb_nav_state z; bb_nav_init(&z, BB_NAV_STUDIO, 600); base(&z, 0, BB_UI_STUDIO); perform(n, nm16, pos, BB_UI_NO, 0);
                        snprintf(nm, sizeof nm, "perform %u %u/%u/%u \"%s\"", n, g, k, i, t);
                        lint_on = 1; ntext = 0; bb_ui_invalidate(&u); (void)bb_ui_frame(&u, &in); lint_on = 0; lint(nm);
                        int32_t bx, by, bw, bh; pf_key_box(pos, n, &bx, &by, &bw, &bh);
                        unsigned inside = 0, kn = (unsigned)(16u * 0u + pos + 1u), sd = 0;
                        char knum[4], joined[96] = "";
                        snprintf(knum, sizeof knum, "%u", kn);
                        for (unsigned x = 0; x < ntext; x++) {
                            const htext *h = &texts[x];
                            if (h->x0 < bx || h->x1 > bx + bw || h->y0 < by || h->y1 > by + bh) continue;
                            if (!strcmp(h->s, knum) && h->y0 < by + 15) continue;     /* the key number */
                            inside++;
                            if (h->flags & 64u) sd = 1;                 /* pf_side: on its side */
                            if (joined[0]) strncat(joined, " ", sizeof joined - strlen(joined) - 1);
                            strncat(joined, h->s, sizeof joined - strlen(joined) - 1);
                        }
                        if (sd && n <= 8u) FAIL("%s: on its side outside the 9-16 layout", nm);
                        side += sd;
                        if (!sd && n > 8u) FAIL("%s: upright in the 9-16 layout (\"%s\")", nm, joined);   /* RC2 r2: a row reads alike */
                        {   /* the marks survive (2 B/Fox 1 A keeps 2, 1, A) */
                            char m0[40], m1[40]; marks(t, m0, sizeof m0); marks(joined, m1, sizeof m1);
                            const char *q0 = m0, *q1 = m1; int ok = 1;   /* m0's marks, in order, among m1's */
                            while (*q0 && ok) {
                                size_t l0 = strcspn(q0, ","); int hit = 0;
                                while (*q1 && !hit) { size_t l1 = strcspn(q1, ","); hit = l0 == l1 && !strncmp(q0, q1, l0); q1 += l1 + (q1[l1] == ','); }
                                ok = hit; q0 += l0 + (q0[l0] == ',');
                            }
                            if (!ok) FAIL("%s: drawn \"%s\" loses a mark (%s / %s)", nm, joined, m0, m1);
                        }
                        if (strcmp(joined, t)) { abbr++; if (getenv("CAP_ABBR")) printf("ABBR %u \"%s\" -> \"%s\"\n", n, t, joined); }
                        for (unsigned x = 0; x < ntext; x++) {           /* a text that starts in the button must end in it */
                            const htext *h = &texts[x];
                            if (h->x0 >= bx && h->x0 < bx + bw && h->y0 >= by && h->y0 < by + bh &&
                                (h->x1 > bx + bw || h->y1 > by + bh)) FAIL("%s: \"%s\" leaves its button", nm, h->s);
                        }
                        if (!inside) {
                            FAIL("%s: no name drawn in the button (box %d,%d %dx%d)", nm, bx, by, bw, bh);
                            for (unsigned x = 0; x < ntext && getenv("CAP_DEBUG"); x++)
                                printf("   text \"%s\" %d,%d..%d,%d\n", texts[x].s, texts[x].x0, texts[x].y0, texts[x].x1, texts[x].y1);
                        }
                        size_hist[lay] += inside;
                    }
                }
            }
        }
        printf("perform buttons: %u names x 3 layouts, %u on their side, %u abbreviated; lights = buttons on %u song pages\n",
               names, side, abbr, pages);
        if (names < 700u) FAIL("perform: only %u names", names);
    }
    {   /* RC2 frames: PERFORM in each state, the chooser pages, the power-on list and list -> PLAY -> PERFORM */
        bb_nav_state s; char nm[48];
        static const char *const CR78[11] = {"Rock 1", "Rock 2", "Disco 1", "Disco 2", "Waltz", "Shuffle", "Slow Rock", "Swing",
                                            "Foxtrot", "Tango", "Bossa Nova"};
        static const char *const TR66L[6] = {"4 Beat / Swing 3 A", "4 Beat / Swing 3 B", "4 Beat / Shuffle A", "4 Beat / Shuffle B",
                                             "4 Beat / Bass-Snare A", "4 Beat / Bass-Snare B"};
        static const unsigned SIZES[] = {1, 4, 5, 8, 9, 11, 16};
        bb_nav_init(&s, BB_NAV_STUDIO, 600);
        for (unsigned i = 0; i < sizeof SIZES / sizeof SIZES[0]; i++) {
            base(&s, 0, BB_UI_STUDIO); perform(SIZES[i], SECS16, SIZES[i] > 2 ? 2 : 0, BB_UI_NO, 0);
            snprintf(nm, sizeof nm, "rc2-perform-%u", SIZES[i]); shot(nm);
        }
        base(&s, 0, BB_UI_STUDIO); perform(8, SECS16, 4, 7, 0); shot("rc2-perform-queued");
        in.notice = "CUE CANCELLED"; perform(8, SECS16, 4, BB_UI_NO, 0); shot("rc2-perform-cancelled"); in.notice = 0;
        in.notice = "TOO LATE TO CANCEL"; shot("rc2-perform-too-late"); in.notice = 0;
        in.notice = "BUSY - RETRY"; shot("rc2-perform-busy"); in.notice = 0;
        perform(8, SECS16, 4, BB_UI_NO, 0); in.lane[1].flags = BB_UI_LN_MUTED; shot("rc2-perform-muted-b");
        perform(8, SECS16, 2, BB_UI_NO, 0); in.lane[1].name = in.lane[2].name = "Main"; shot("rc2-perform-mixed");
        perform(8, SECS16, 2, BB_UI_NO, 0); in.lane[1].name = "Main"; in.lane[2].name = "Bridge"; shot("rc2-perform-vary");
        perform(8, SECS16, 2, BB_UI_NO, 0); in.running = 0; shot("rc2-perform-stopped"); in.running = 1;
        perform(8, SECS16, 4, BB_UI_NO, 0); in.lane[0].flags = 0; in.lane[2].flags = BB_UI_LN_FOCUS; in.instr = BB_UI_INST_DILLO; shot("rc2-perform-dillo-focus");
        base(&s, 0, BB_UI_STUDIO); perform(11, CR78, 0, 2, 0); in.perf_song = "Roland CR-78"; shot("rc2-perform-cr78");
        base(&s, 0, BB_UI_STUDIO); perform(6, TR66L, 1, BB_UI_NO, 3); in.perf_song = "Roland TR-66"; shot("rc2-perform-tr66-page4");
        /* power-on: the song list, PLAY prompts; then PLAY: PERFORM playing */
        {
            static const char *const coll[3] = {"SONGS", "ERAS", "PRESETS"};
            bb_nav_out o; bb_nav_init(&s, BB_NAV_STUDIO, 600); s.bank = BB_NAV_HOME_SONGS;
            base(&s, 0, BB_UI_PATTERN); browse(BB_UI_PATTERN); in.running = 0; in.cursor = 0; in.playing_item = BB_UI_NO;
            in.tab = coll; in.tabs = 3; in.tab_cur = 0; in.footer = "SONGS 1/67 \xB7 PLAY plays it on all three"; shot("rc2-power-on");
            in.tab_cur = 2; in.items = 4; in.item[0] = "Roland CR-8000"; in.item[1] = "Roland CR-78"; in.item[2] = "Roland TR-66"; in.item[3] = "Roland TR-77";
            in.cursor = 1; in.footer = "PRESETS 2/4 \xB7 PLAY plays it on all three"; shot("rc2-presets-list");
            in.tab_cur = 1; in.items = 10; for (int i = 0; i < 10; i++) in.item[i] = SONGS10[i]; in.footer = "ERAS 3/21 \xB7 PLAY plays it on all three"; shot("rc2-eras-list");
            memset(&o, 0, sizeof o); s.bank = BB_NAV_HOME_CONDUCT;
            base(&s, 0, BB_UI_STUDIO); perform(8, SECS16, 0, BB_UI_NO, 0); in.perf_song = "Planet Rock"; shot("rc2-play-to-perform");
        }
        /* the choosers: KIT, PATTERN for A / B / Dillo; a knob turned (the band) */
        for (uint8_t row = 0; row < 3; row++)
            for (uint8_t pg = 0; pg < 2; pg++) {
                bb_nav_out o; bb_nav_init(&s, BB_NAV_STUDIO, 600);
                bb_nav_set_instrument(&s, row < 2 ? BB_NAV_INSTR_DRUM : BB_NAV_INSTR_DILLO, row < 2 ? 2 : 1); bb_nav_set_focus(&s, row);
                memset(&o, 0, sizeof o); bb_nav_goto(&s, pg ? BB_NAV_PATTERN : BB_NAV_KIT, &o);
                base(&s, row, pg ? BB_UI_PATTERN : BB_UI_KIT); browse(pg ? BB_UI_PATTERN : BB_UI_KIT);
                snprintf(nm, sizeof nm, "rc2-%s-%s", pg ? "pattern" : "kit", row == 0 ? "a" : row == 1 ? "b" : "dillo"); shot(nm);
            }
        base(&s, 0, BB_UI_PATTERN); browse(BB_UI_PATTERN); in.knob_turning = 1; in.events |= BB_EV_KNOB; in.ev_knob = 1; shot("rc2-pattern-knob");
        in.knob_turning = BB_UI_NO;
        base(&s, 0, BB_UI_PATTERN); browse(BB_UI_PATTERN); in.notice = "PATTERN APPLIED"; shot("rc2-pattern-notice"); in.notice = 0;
    }
    {   /* ATTRACT (30 s idle on STUDIO, playing): the tower is drawn whole at every level; only the rays
         * reach with the level (Joe, #4 6465654550: the tower's lower part was missing). The tower's
         * lattice between its legs, y 30..70, must light the same pixels at level 0 as at level 255. */
        bb_nav_state s; bb_nav_init(&s, BB_NAV_STUDIO, 600);
        static const uint8_t LV[] = {0, 120, 255};
        unsigned lit[3];
        for (unsigned i = 0; i < 3; i++) {
            char nm[32]; base(&s, 0, BB_UI_STUDIO); in.events = 0; in.level = LV[i];
            u.idle_t = in.now_ms - (BB_UI_ATTRACT_MS + 1000u); snprintf(nm, sizeof nm, "attract-level-%u", LV[i]); shot(nm);
            lit[i] = 0;
            for (int y = 30; y < 70; y++) for (int x = 118 - (y - 20) * 3 / 5; x <= 125 + (y - 20) * 11 / 20; x++) lit[i] += host_screen[y * 240 + x] != 0;
        }
        if (!lit[0] || lit[0] != lit[2] || lit[1] != lit[2]) FAIL("attract: tower pixels depend on level (%u / %u / %u)", lit[0], lit[1], lit[2]);
    }
    {   /* 2.2 playability: on every playing page, a key is drawn as a button exactly when its LED is not dark, and drawn
         * lit exactly when its LED is lit (bb_ui_play.c pl_fill; the LEDs from bb_nav_leds with the firmware's features).
         * Pages x parts x states: group mutes, kills, REC held (the solo map), step banks, set steps, FX tabs, scale lock. */
        unsigned checks = 0, bad = 0;
        static const uint8_t PG[7][2] = {{BB_NAV_PLAY, 0}, {BB_NAV_PLAY, 1}, {BB_NAV_FX, 0}, {BB_NAV_FX, 1}, {BB_NAV_FX, 2},
                                         {BB_NAV_FX, 3}, {BB_NAV_FX, 4}};
        for (uint8_t row = 0; row < 3; row++)
            for (unsigned g = 0; g < 7; g++)
                for (unsigned v = 0; v < 6; v++) {
                    bb_nav_state s, f; bb_nav_out o; bb_nav_led_out l;
                    bb_nav_init(&s, BB_NAV_STUDIO, 600);
                    bb_nav_set_instrument(&s, row < 2 ? BB_NAV_INSTR_DRUM : BB_NAV_INSTR_DILLO, row < 2 ? 2 : 1);
                    bb_nav_set_focus(&s, row); memset(&o, 0, sizeof o); bb_nav_goto(&s, PG[g][0], &o);
                    if (PG[g][0] == BB_NAV_PLAY) s.play_keys = PG[g][1]; else s.bank = PG[g][1];
                    s.rec_down = v == 3;
                    if (v == 4 && row == 2) { s.scale = 1; s.root = 9; }
                    base(&s, row, bb_v3_route(&s, 0));
                    pf.group_muted = (uint8_t)(v * 5u & 15u); pf.instr_muted = (uint8_t)(v & 7u);
                    pf.step_banks = (uint8_t)(1u + v % 3u); pf.step_bank = (uint8_t)(v % 3u ? 1u : 0u);
                    pf.steps = (uint16_t)(0x1111u << (v & 3u) | v);
                    in.group_muted = pf.group_muted; play.instr_muted = pf.instr_muted;
                    f = s; bb_nav_set_features(&f, BB_NAV_F_TAP | BB_NAV_F_SCALE | BB_NAV_F_THROW | BB_NAV_F_STEP_PITCH);
                    bb_nav_leds(&f, &pf, 0, &l);
                    uint32_t want = 0, lit = 0;
                    for (unsigned k = 0; k < 27; k++) {
                        in.key_led[k] = l.key[k];
                        if (l.key[k] != BB_UI_LED_DARK) want |= 1u << k;
                        if (l.key[k] == BB_UI_LED_LIT) lit |= 1u << k;
                    }
                    in.accent = s.rec_down; play.pad_lit = 0;              /* at rest: no flash */
                    hk_seen = hk_bright = 0;
                    bb_ui_invalidate(&u); (void)bb_ui_frame(&u, &in);
                    checks++;
                    if (hk_seen != want || hk_bright != lit) {
                        bad++;
                        FAIL("2.2 keys vs LEDs: row %u page %u/%u state %u: drawn %07x want %07x, lit %07x want %07x", row, PG[g][0],
                             PG[g][1], v, hk_seen, want, hk_bright, lit);
                    }
                }
        printf("2.2 key/LED agreement: %u screens, %u disagree\n", checks, bad);
    }
    {   /* 2.2 rules drawn from tables, checked against navigation and pixels: (1) every black key, on the playing pages and
         * in the key strip, sits after the white key a semitone below it (the FM-1 runs F..G: bb_nav's nav_black_semi);
         * (2) step keys in the 808 position colours; (3) pads in family colours; (4) a pad flashes in full colour unless
         * its group is muted. */
        bb_nav_state s; bb_nav_out o; unsigned n = 0, bad = 0; uint8_t a, b;
        bb_nav_init(&s, BB_NAV_STUDIO, 600); bb_nav_set_instrument(&s, BB_NAV_INSTR_DILLO, 1); bb_nav_set_focus(&s, 2);
        memset(&o, 0, sizeof o); bb_nav_goto(&s, BB_NAV_PLAY, &o);
        for (unsigned j = 0; j < 11; j++, n += 2) {
            (void)bb_nav_key_role(&s, (uint8_t)(16 + j), 0, &b); (void)bb_nav_key_role(&s, PL_AFTER[j], 0, &a);
            if (b != a + 1) { bad++; FAIL("2.2: black key %u (note %u) drawn after white key %u (note %u)", j + 1, b, PL_AFTER[j] + 1, a); }
            if (black_x(j) != 8 + 14 * PL_AFTER[j] + 9) { bad++; FAIL("2.2: key strip black key %u at x %d", j + 1, (int)black_x(j)); }
        }
        bb_nav_set_instrument(&s, BB_NAV_INSTR_DRUM, 2); bb_nav_set_focus(&s, 0);
        memset(&o, 0, sizeof o); bb_nav_goto(&s, BB_NAV_PLAY, &o); s.play_keys = BB_NAV_PLAY_STEPS;
        base(&s, 0, BB_UI_STEPS); pf.steps = 0xFFFF; real_leds(&s); in.vsteps[0] = 0; in.vacc[0] = 0; in.running = 0;
        bb_ui_invalidate(&u); (void)bb_ui_frame(&u, &in);
        for (unsigned k = 0; k < 16; k++, n++)
            if (host_screen[(ST_KEY_Y + 30) * 240 + 8 + 14 * k + 6] != PF_GROUP[k >> 2]) { bad++; FAIL("2.2: step key %u not in its 808 colour", k + 1); }
        s.play_keys = BB_NAV_PLAY_LIVE; base(&s, 0, BB_UI_PLAY_DRUMS);
        in.group_muted = 0; play.pad_lit = 1u | 1u << 8; play.pad_group[8] = 2; in.group_muted = 1u << 2;
        bb_ui_invalidate(&u); (void)bb_ui_frame(&u, &in);
        for (unsigned i = 0; i < 11; i++, n++) {
            uint16_t px = host_screen[(PL_PAD_Y(i) + 26) * 240 + PL_PAD_X(i) + 8], fam = PF_GROUP[PL_PAD_FAM[i]];
            uint16_t want = play.pad_group[i] == 2 ? BB_UI_FAINT : i == 0 ? fam : c_scale(fam, 72);   /* 0 flashes; 8 flashes, HATS muted */
            if (px != want) { bad++; FAIL("2.2: pad %u colour %04x want %04x", i + 1, px, want); }
            if ((i == 0) != (host_screen[(PL_PAD_Y(i) + 15) * 240 + PL_PAD_X(i)] == BB_UI_TEXT)) { bad++; FAIL("2.2: pad %u rim wrong (flash, mute)", i + 1); }
        }
        for (int y = 72; y < 196; y += 4, n++)                          /* (5) no artwork behind the pads (x 239: margin) */
            if (host_screen[y * 240 + 239] != BB_UI_BG) { bad++; FAIL("2.2: artwork behind PADS at y %d", y); break; }
        printf("2.2 drawing rules: %u checks, %u failures\n", n, bad);
    }
    fclose(out);
    printf("perform status = the buttons on %u PERFORM frames\n", perf_agree);
    if (perf_agree < 15u) FAIL("perform status: only %u frames checked", perf_agree);
    printf("captures: %u frames, %u lint/route failures\n", shots, failures); (void)known;
    return failures != 0;
}
