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

typedef struct { int16_t x0, y0, x1, y1; uint8_t flags, layer; char s[40]; } htext;
static htext texts[256]; static unsigned ntext, lint_on, failures, known;
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
static char ktext[4][16];
static void shot(const char *name)
{
    lint_on = 1; ntext = 0;
    bb_ui_invalidate(&u);
    (void)bb_ui_frame(&u, &in);
    lint_on = 0; lint(name);
    fwrite(name, 1, strlen(name) + 1, out);
    fwrite(host_screen, 2, 240 * 240, out);
    shots++;
}

static const char *const ITEMS[] = {"Planet Rock", "Blue Monday", "Egypt, Egypt", "Tainted Love", "Numbers"};
static const char *const SECS[] = {"INTRO", "VERSE", "HOOK", "BREAK", "OUTRO"};
static void base(const bb_nav_state *s, uint8_t row, uint8_t page)
{
    uint8_t roles[4]; const char *lab[4];
    memset(&in, 0, sizeof in);
    in.now_ms = 100000; in.page = page; in.instr = row < 2 ? row : BB_UI_INST_DILLO;
    in.instr_name = row == 0 ? "KIT A" : row == 1 ? "KIT B" : "DILLO";
    in.page_name = bb_nav_page_name(s);
    in.running = 1; in.beat = 1; in.ticks = 37; in.bpm_x10 = 1200; in.level = 120;
    in.events = BB_EV_INPUT;                   /* a button was just used: HOME, not attract */
    bb_nav_knobs(s, roles, lab);
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
}
static void browse(uint8_t page)
{
    in.title = page == BB_UI_KIT ? "DrumTraks" : "Planet Rock";
    in.subtitle = page == BB_UI_KIT ? "LOADED 808" : "SONG \xB7 Part 1";
    in.items = 5; for (int i = 0; i < 5; i++) in.item[i] = page == BB_UI_KIT ? (const char *[]){"808", "LinnDrum", "DrumTraks", "DMX", "808 (edited)"}[i] : ITEMS[i];
    in.cursor = 2; in.playing_item = 0; in.queued_item = BB_UI_NO;
    in.sections = 5; for (int i = 0; i < 5; i++) { in.sec_name[i] = SECS[i]; in.sec_bars[i] = 1; }
    in.sec_cur = 2; in.sec_next = BB_UI_NO;
}

int main(int argc, char **argv)
{
    out = fopen(argc > 1 ? argv[1] : "captures.raw", "wb");
    static const char *const PN[] = {"studio", "play", "kit", "pattern", "sound", "fx", "save", "evolve", "global"};
    static const char *const RN[] = {"a", "b", "dillo"};
    bb_ui_init(&u, 0, 1);
    for (uint8_t row = 0; row < 3; row++)
        for (uint8_t page = 0; page < 9; page++)
            for (uint8_t keys = 0; keys < 2; keys++) {
                if (keys && page != BB_NAV_PLAY) continue;
                uint8_t banks = page == BB_NAV_FX ? 5 : page == BB_NAV_SOUND && row < 2 ? 2 : 1;
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
                    if (v3 == BB_UI_KNOBS) { in.title = page == BB_NAV_SAVE ? "SCENE 2" : page == BB_NAV_GLOBAL ? "GLOBAL" :
                                                     page == BB_NAV_EVOLVE ? (row == 2 ? "EVOLVE  DILLO" : "EVOLVE  DRUM A") :
                                                     page == BB_NAV_SOUND ? (row == 2 ? "Rubber Acid" : "KICK") : bb_nav_bank_name(&s);
                                             in.subtitle = "EDIT: STORE"; }
                    snprintf(name, sizeof name, "%s-%s%s%s%s", PN[page], RN[row], keys ? "-steps" : "",
                             banks > 1 ? "-" : "", banks > 1 ? (bb_nav_bank_name(&s) ? bb_nav_bank_name(&s) : "bank") : "");
                    for (char *c = name; *c; c++) if (*c >= 'A' && *c <= 'Z') *c = (char)(*c + 32);
                    shot(name);
                    if (row == 0 && keys == 0 && bank == 0) {           /* the overlays once per page */
                        in.help = 1; snprintf(name, sizeof name, "%s-help", PN[page]); shot(name); in.help = 0;
                        in.notice = "BUSY / SELECT AGAIN"; snprintf(name, sizeof name, "%s-notice", PN[page]); shot(name); in.notice = 0;
                    }
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
    fclose(out);
    printf("captures: %u frames, %u lint/route failures\n", shots, failures); (void)known;
    return failures != 0;
}
