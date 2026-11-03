/* SPDX-License-Identifier: GPL-3.0-only
 * BB1 2.2 playability: the playing pages drawn as the instrument (bb_ui_play.h, claude/2.2/playability/SPEC.md).
 * Included by bb_ui.c after its pages (uses its strip-clip wrappers, colours, hashes and element list).
 *
 *   DRUM PADS   (PLAY, ARP):  the black keys as labelled chips, 11 pads in instrument-family colours that flash
 *   DRUM STEPS  (PLAY, SEQ):  B1-B11 chips, the selected voice large, 16 tall 808-coloured step keys, the whole kit
 *   DILLO KEYS  (PLAY, ARP):  the filter you hear (cutoff, resonance, envelope) over the F..G keyboard
 *   DILLO STEPS (PLAY, SEQ):  the 303 sheet: pitch bars, slides, accents, note names, the black keys' notes
 *   FX          (white 1-5):  the tabs as key buttons; each bank drawn from its knob values only
 *
 * A key is drawn exactly when its LED (bb_ui_in.key_led) is not dark and looks bright exactly when its LED is lit:
 * one table for both, so the screen and the LEDs cannot disagree (the capture gate checks it per key). */
#include <stddef.h>
#include "bb_ui_play.h"

/* out of line: the pages and their helpers are each called from one place, but inlined into page_draw (which
 * draw_rect inlines) they cost 144 B more (release recipe, measured) */
#define PL_NOINLINE __attribute__((noinline))
static const bb_ui_play *pl __attribute__((section(".pool")));
void bb_ui_play_set(const bb_ui_play *p) { pl = p; }

/* 808 step-key groups and instrument families (SPEC 1.3, decision 4.1): RC2's PF_GROUP red, orange, yellow, cream */
#define PL_FAM PF_GROUP
/* track family by pad position: kick red; snare, rim, clap orange; toms yellow; cow, cymbal, hats cream */
static const uint8_t PL_PAD_FAM[BB_PL_PADS] = {0, 1, 2, 2, 2, 1, 1, 3, 3, 3, 3};
static const char *const PL_NOTE[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
/* black key j sits after white key PL_AFTER[j]: the FM-1 runs F..G (bb_nav.c nav_black_semi 1 3 5 8 10 13 ...) */
static const uint8_t PL_AFTER[11] = {0, 1, 2, 4, 5, 7, 8, 9, 11, 12, 14};

static uint32_t pl_num(char *b, uint32_t v)   /* 1..99 */
{
    uint32_t n = 0;
    if (v >= 10u) b[n++] = (char)('0' + v / 10u);
    b[n++] = (char)('0' + v % 10u);
    b[n] = 0;
    return n;
}
/* a key's button: lit = the colour, breathing = breathing, else dark; reports it to the host test */
static uint16_t pl_fill(const bb_ui_in *in, uint32_t k, uint16_t c)
{
    uint32_t led = in->key_led[k];
    uint16_t f = led == BB_UI_LED_LIT ? c : led == BB_UI_LED_BREATH ? breath_c(c, in->now_ms) : c_scale(c, 66);
    BB_UI_HOOK_KEY(k, f == c);                                         /* what is drawn: full colour or not */
    return f;
}
static uint32_t pl_on(const bb_ui_in *in, uint32_t k) { return in->key_led[k] != BB_UI_LED_DARK; }
static int32_t pl_mid(const char *s, int32_t x, int32_t w) { return x + (w - text_w(&AF_S, s)) / 2; }
static void pl_tri(int32_t cx, int32_t cy, int32_t dir, uint16_t c)   /* dir: 0 up, 1 down, 2 left, 3 right */
{
    int32_t i;
    for (i = 0; i < 5; i++) {
        int32_t r = dir & 1 ? 4 - i : i;
        if (dir < 2) u_rect(cx - r, cy - 2 + i, 2 * r + 1, 1, c);
        else u_rect(cx - 2 + i, cy - (dir == 3 ? 4 - i : i), 1, 2 * (dir == 3 ? 4 - i : i) + 1, c);
    }
}
/* ------------------------------------------------------------- the black keys as a row of chips
 * B1..B11 at x 5 + 21 j, in the order under the fingers; a dark key leaves its place empty */
#define PL_CX(j) (5 + 21 * (int32_t)(j))
#define PL_CW 20
static void pl_head(int32_t j0, int32_t j1, int32_t y, const char *w)   /* a function word over a run of chips */
{
    int32_t x = PL_CX(j0), e = PL_CX(j1) + PL_CW, t = text_w(&AF_S, w);
    if (x + (e - x - t) / 2 < 4) return;
    u_text(x + (e - x - t) / 2, y, &AF_S, w, BB_UI_GREY, BB_UI_BG);
}

/* ------------------------------------------------------------- DRUM PADS (ARP)
 * y 24 the black keys' functions, 38-91 their chips (MUTE red), 95-195 the eleven pads */
static const char *const PL_GROUP[4] = {"KICK", "SN/CL", "HATS", "PERC"};
static const char *const PL_KILL[4] = {"A", "B", "DILLO", "DRUMS"};
#define PL_CHIP_Y 38
#define PL_CHIP_H 54
#define PL_PAD_X(i) (4 + 58 * (int32_t)((i) & 3u))
#define PL_PAD_Y(i) (95 + 34 * (int32_t)((i) >> 2))
static uint32_t pl_killed(uint32_t j)   /* B8-B11: that part is cut */
{
    return j < 3u ? pl->instr_muted >> j & 1u : (pl->instr_muted & 3u) == 3u;
}
static PL_NOINLINE void pl_pads_chip(const bb_ui_in *in, uint32_t j)
{
    int32_t x = PL_CX(j), y = PL_CHIP_Y;
    uint32_t k = 16u + j, red = j < 4u ? in->group_muted >> j & 1u : j >= 7u ? pl_killed(j - 7u) : 0u;
    uint16_t f;
    const char *w = j < 4u ? (red && !in->accent ? "MUTE" : PL_GROUP[j]) : j == 4u ? "FILL" : j >= 7u ? PL_KILL[j - 7u] : 0;
    if (!pl_on(in, k)) return;
    f = pl_fill(in, k, inst_c(in));
    if (red) f = BB_UI_RED;
    u_rbox(x, y, PL_CW, PL_CHIP_H, 3, f, BB_UI_BG);
    if (w) pf_side(x + (PL_CW - (int32_t)AF_S.h + 3) / 2, y + (PL_CHIP_H + text_w(&AF_S, w)) / 2, w,   /* RC2: on its side */
                   f == BB_UI_RED || in->key_led[k] == BB_UI_LED_LIT ? BB_UI_BG : BB_UI_TEXT, f);
    else pl_tri(x + PL_CW / 2, y + PL_CHIP_H / 2, j == 5u ? 2 : 3, in->key_led[k] == BB_UI_LED_LIT ? BB_UI_BG : BB_UI_TEXT);
}
static PL_NOINLINE void pl_pad(const bb_ui_in *in, uint32_t i)
{
    int32_t x = PL_PAD_X(i), y = PL_PAD_Y(i);
    uint32_t g = pl->pad_group[i], muted = g < 4u && (in->group_muted >> g & 1u), lit = (pl->pad_lit >> i & 1u) && !muted;
    uint16_t fam = PL_FAM[PL_PAD_FAM[i]], f = muted ? BB_UI_FAINT : lit ? fam : c_scale(fam, 72);
    if (!pl_on(in, i)) return;
    BB_UI_HOOK_KEY(i, f == fam);
    if (lit) {                                                        /* it sounds: full colour, a white rim */
        u_rbox(x, y, 57, 31, 4, BB_UI_TEXT, BB_UI_BG);
        u_rbox(x + 2, y + 2, 53, 27, 3, f, BB_UI_TEXT);
    } else u_rbox(x, y, 57, 31, 4, f, BB_UI_BG);
    (void)pf_flat(pl->pad_name[i] ? pl->pad_name[i] : "", &AF_S, x + 1, y, 55, 31, lit ? BB_UI_BG : muted ? BB_UI_GREY : BB_UI_TEXT,
                  f, 1);                                              /* RC2: one line, or two at the space */
}
static PL_NOINLINE void page_pads(const bb_ui_in *in)
{
    uint32_t j;
    if (pl_on(in, 16) || pl_on(in, 19)) pl_head(0, 3, 24, in->accent ? "SOLO" : "MUTE");
    if (pl_on(in, 21) || pl_on(in, 22)) pl_head(5, 6, 24, "SECTION");
    if (pl_on(in, 23) || pl_on(in, 26)) pl_head(7, 10, 24, "KILL");
    for (j = 0; j < 11u; j++) pl_pads_chip(in, j);
    for (j = 0; j < BB_PL_PADS; j++) pl_pad(in, j);
    if (!pl_on(in, 11)) {                                             /* the free place: what REC does here */
        u_text(PL_PAD_X(11) + 4, PL_PAD_Y(11) + 2, &AF_S, "REC + PAD", BB_UI_GREY, BB_UI_BG);
        u_text(PL_PAD_X(11) + 4, PL_PAD_Y(11) + 14, &AF_S, "= ACCENT", BB_UI_GREY, BB_UI_BG);
    }
}

/* ------------------------------------------------------------- DRUM STEPS (SEQ)
 * y 25-44 B1-B11 chips, 46 what they do, 60 the voice, 80-125 the 16 step keys, 128 positions, 146-193 the kit */
#define ST_KEY_Y 80
#define ST_KEY_H 46
#define ST_GRID_Y 150
static PL_NOINLINE void page_dsteps(const bb_ui_in *in)
{
    uint32_t j, k, v, nb = pl_on(in, 25) || pl_on(in, 26);
    int32_t head = in->running && in->step < 16u ? (int32_t)in->step : -1;
    char b[12];
    uint16_t st = in->voice < 11u ? in->vsteps[in->voice] : 0u, ac = in->voice < 11u ? in->vacc[in->voice] : 0u;
    for (j = 0; j < 11u; j++) {                                       /* B1-B8 voices, B9 accent, B10-11 bank */
        uint32_t key = 16u + j;
        uint16_t c = j < 8u ? PL_FAM[PL_PAD_FAM[j]] : inst_c(in), f;
        if (!pl_on(in, key)) continue;
        f = pl_fill(in, key, c);
        b[0] = 'B';
        (void)pl_num(b + 1, j + 1u);
        u_rbox(PL_CX(j), 25, PL_CW, 19, 3, f, BB_UI_BG);
        u_text(pl_mid(b, PL_CX(j), PL_CW), 27, &AF_S, b, in->key_led[key] == BB_UI_LED_LIT ? BB_UI_BG : BB_UI_TEXT, f);
    }
    pl_head(0, 7, 45, "VOICE");
    if (pl_on(in, 24)) {
        if (nb) u_text(PL_CX(9) - 2 - text_w(&AF_S, "ACCENT"), 45, &AF_S, "ACCENT", BB_UI_GREY, BB_UI_BG);
        else pl_head(8, 8, 45, "ACCENT");
    }
    if (nb) u_text_r(236, 45, &AF_S, "BANK", BB_UI_GREY, BB_UI_BG);
    u_name(8, 59, &AF_M, in->voice_name ? in->voice_name : "", BB_UI_TEXT, BB_UI_BG, 150);
    if (in->banks > 1u) {
        b[0] = (char)('1' + in->bank % 9u); b[1] = ' '; b[2] = '/'; b[3] = ' '; b[4] = (char)('0' + in->banks % 10u); b[5] = 0;
        u_text_r(232, 62, &AF_S, b, BB_UI_GREY, BB_UI_BG);
    }
    for (k = 0; k < 16u; k++) {                                       /* the 16 white keys: 808 position colours */
        int32_t x = 8 + 14 * (int32_t)k;
        uint16_t c = PL_FAM[k >> 2];
        if (!pl_on(in, k)) continue;
        uint16_t f = pl_fill(in, k, c);
        u_rect(x, ST_KEY_Y, 13, ST_KEY_H, f);
        if ((st >> k & 1u) && (ac >> k & 1u))
            pl_tri(x + 6, ST_KEY_Y + 6, 0, BB_UI_BG);                 /* accent */
        if ((st >> k & 1u) && in->ratchet[k] == BB_UI_RATCHET_MIXED)
            u_rect(x + 3, ST_KEY_Y + ST_KEY_H / 2, 7, 1, BB_UI_BG);   /* copies disagree: half a split */
        else if ((st >> k & 1u) && in->ratchet[k] > 1u)
            for (v = 1; v < in->ratchet[k] && v < 4u; v++)            /* ratchet: the key split in x2..x4 */
                u_rect(x, ST_KEY_Y + 12 + (int32_t)((ST_KEY_H - 12) * v / in->ratchet[k]), 13, 1, BB_UI_BG);
        if ((int32_t)k == head) u_frame(x - 1, ST_KEY_Y - 2, 15, ST_KEY_H + 4, BB_UI_TEXT, 1);
        if (!(k & 3u)) u_text(x + 3 - (k >= 9u ? 3 : 0), 128, &AF_S, k == 0 ? "1" : k == 4 ? "5" : k == 8 ? "9" : "13", BB_UI_GREY, BB_UI_BG);
    }
    for (v = 0; v < 11u; v++)                                         /* the whole kit, every lane, this bank */
        for (k = 0; k < 16u; k++) {
            uint16_t c = PL_FAM[PL_PAD_FAM[v]];
            uint16_t col = in->vacc[v] >> k & 1u ? c : in->vsteps[v] >> k & 1u ? c_scale(c, 150) : BB_UI_FAINT;
            if (v != in->voice && col != BB_UI_FAINT) col = c_scale(col, 150);
            u_rect(8 + 14 * (int32_t)k, ST_GRID_Y + 4 * (int32_t)v, 13, 3, col);
        }
    if (in->voice < 11u) u_rect(4, ST_GRID_Y + 4 * (int32_t)in->voice, 2, 3, BB_UI_TEXT);
    if (head >= 0) u_rect(8 + 14 * head, ST_GRID_Y - 4, 13, 2, BB_UI_TEXT);
}

/* ------------------------------------------------------------- DILLO: the patch line shared by KEYS and STEPS */
static void pl_dillo_top(const bb_ui_in *in, const char *info, uint16_t ic)
{
    u_brand(8, NOW_CAP_Y, NM_NOW, in->patch, BB_UI_TEXT, BB_UI_BG, 224, 0);
    u_text_fit(8, 44, &AF_S, info, ic, BB_UI_BG, 224);
}
static void pl_cat(char *b, uint32_t *n, const char *s) { while (*s && *n < 46u) b[(*n)++] = *s++; b[*n] = 0; }

/* DILLO KEYS: the filter you hear over the keyboard. Curves from the knob values: CUTOFF the corner, RESO the peak,
 * ENV MOD how far each note opens it (the faint curve), as page_dillo's lp_gain. */
#define KB_Y 124
static PL_NOINLINE void pl_curve(uint32_t fc, uint32_t q16, uint16_t c)
{
    int32_t s, px = 0, py = 0;
    for (s = 0; s <= 112; s += 2) {
        uint32_t g = lp_gain((uint32_t)s * 255u / 112u, fc, q16);
        int32_t x = 8 + 2 * s, y;
        if (g > 820u) g = 820u;
        y = 116 - (int32_t)(g * 13u / 256u);
        if (s) u_line(px, py, x, y, c, 2);
        px = x; py = y;
    }
}
static uint32_t pl_fc(uint32_t v) { return 26u + v * 204u / 255u; }
static PL_NOINLINE void page_dkeys(const bb_ui_in *in)
{
    uint32_t k, fc = pl_fc(in->cutoff), q16 = 12u + (uint32_t)in->reso * 84u / 255u, j;
    char b[48];
    uint32_t n = 0;
    pl_cat(b, &n, in->scale && in->scale[0] ? in->scale : "CHROMATIC");
    for (k = 0; k < 27u; k++)
        if (pl->keys_sounding >> k & 1u) { pl_cat(b, &n, "  \xB7  PLAYING "); pl_cat(b, &n, PL_NOTE[pl->key_note[k] % 12u]); break; }
    pl_dillo_top(in, b, BB_UI_GREY);
    u_rect(8, 117, 224, 1, BB_UI_FAINT);
    pl_curve(pl_fc(in->cutoff + ((255u - in->cutoff) * in->envmod >> 8)), q16, c_scale(BB_UI_DILLO, 90));
    pl_curve(fc, q16, BB_UI_DILLO);
    j = 8u + fc * 224u / 255u;                                         /* the corner the knob sets */
    u_text((int32_t)(j > 190u ? 190u : j < 30u ? 8u : j - 22u), 58, &AF_S, "CUTOFF", BB_UI_DILLO, BB_UI_BG);
    for (k = 0; k < 16u; k++) {                                       /* white keys F..G */
        int32_t x = 8 + 14 * (int32_t)k;
        uint32_t note = pl->key_note[k] % 12u, sound = pl->keys_sounding >> k & 1u;
        uint16_t f;
        char c[2] = {PL_NOTE[note][0], 0};
        if (!pl_on(in, k)) continue;
        f = pl_fill(in, k, BB_UI_TEXT);
        if (in->key_led[k] != BB_UI_LED_LIT) f = c_scale(BB_UI_TEXT, 176);   /* a key you can play: light; the root: white */
        if (sound) f = BB_UI_DILLO;
        u_rect(x, KB_Y, 13, 72, f);
        u_text(pl_mid(c, x, 13), 170, &AF_S, c, BB_UI_BG, f);
        if (PL_NOTE[note][1]) u_text(pl_mid("#", x, 13), 181, &AF_S, "#", BB_UI_BG, f);
    }
    for (j = 0; j < 11u; j++) {                                       /* black keys between them, as on the FM-1 */
        int32_t x = 8 + 14 * (PL_AFTER[j] + 1) - 5;
        uint32_t key = 16u + j;
        uint16_t f;
        if (!pl_on(in, key)) continue;
        f = pl_fill(in, key, 0x632Cu);
        if (pl->keys_sounding >> key & 1u) f = BB_UI_DILLO;
        u_rect(x - 1, KB_Y, 11, 44, BB_UI_BG);
        u_rect(x, KB_Y, 9, 43, f);
    }
}

/* DILLO STEPS: the 303 sheet. Per step the note bar at its pitch, an accent bar, slide arcs to the next note; the
 * selected (held) step white, the playhead pink; the note name and its octave mark under it; the black keys below
 * with the note each sets. */
#define SH_Y 62
#define SH_H 58
static PL_NOINLINE void page_dsheet(const bb_ui_in *in)
{
    uint32_t k, lo = 255, hi = 0, mid;
    int32_t head = in->running ? (int32_t)in->step : -1, sel = -1;
    char b[48];
    uint32_t n = 0;
    for (k = 0; k < 16u; k++) {
        if (pl->step_held >> k & 1u) sel = (int32_t)k;
        if (pl->step_gate >> k & 1u) { lo = pl->step_note[k] < lo ? pl->step_note[k] : lo; hi = pl->step_note[k] > hi ? pl->step_note[k] : hi; }
    }
    if (sel >= 0) {
        pl_cat(b, &n, "STEP ");
        n += pl_num(b + n, (uint32_t)sel + 1u);
        if (pl->step_gate >> sel & 1u) { pl_cat(b, &n, "  \xB7  "); pl_cat(b, &n, PL_NOTE[pl->step_note[sel] % 12u]); }
    } else pl_cat(b, &n, "PRESS A STEP TO EDIT IT");
    pl_dillo_top(in, b, sel >= 0 ? BB_UI_DILLO : BB_UI_GREY);
    {   /* octave marks: against the octave most steps sit in (C-based octaves, as a 303 sheet) */
        uint8_t cnt[11] = {0};
        mid = 0;
        for (k = 0; k < 16u; k++)
            if (pl->step_gate >> k & 1u && pl->step_note[k] / 12u < 11u && ++cnt[pl->step_note[k] / 12u] > cnt[mid])
                mid = pl->step_note[k] / 12u;
    }
    for (k = 0; k < 16u; k++) {
        int32_t x = 8 + 14 * (int32_t)k;
        uint32_t g = pl->step_gate >> k & 1u, note = pl->step_note[k];
        uint16_t f;
        if (!pl_on(in, k)) continue;
        f = pl_fill(in, k, c_scale(BB_UI_DILLO, 70));
        if (in->key_led[k] != BB_UI_LED_LIT) f = c_scale(BB_UI_DILLO, (k & 3u) ? 14 : 24);
        u_rect(x, SH_Y, 13, SH_H, f);
        if (g) {
            int32_t y = SH_Y + SH_H - 12 - (hi > lo ? (int32_t)((note - lo) * (SH_H - 18) / (hi - lo)) : (SH_H - 18) / 2);
            u_rect(x + 1, y, 11, 5, (int32_t)k == sel ? BB_UI_TEXT : BB_UI_DILLO);
            if (in->note_acc >> k & 1u) u_rect(x + 1, SH_Y + SH_H - 4, 11, 3, 0xFDFBu);
            if ((in->note_slide >> k & 1u) && k < 15u && (pl->step_gate >> (k + 1u) & 1u)) {   /* slide: an arc */
                u_line(x + 6, SH_Y - 2, x + 13, SH_Y - 7, BB_UI_DILLO, 2);
                u_line(x + 13, SH_Y - 7, x + 20, SH_Y - 2, BB_UI_DILLO, 2);
            }
            {
                char c[2] = {PL_NOTE[note % 12u][0], 0};
                uint16_t t = (int32_t)k == sel ? BB_UI_TEXT : BB_UI_GREY;
                u_text(pl_mid(c, x, 13), 123, &AF_S, c, t, BB_UI_BG);
                if (PL_NOTE[note % 12u][1]) u_text(pl_mid("#", x, 13), 133, &AF_S, "#", t, BB_UI_BG);
                if (note / 12u != mid) pl_tri(x + 6, 148, note / 12u > mid ? 0 : 1, t);
            }
        }
        if ((int32_t)k == sel) u_frame(x - 1, SH_Y - 1, 15, SH_H + 2, BB_UI_TEXT, 1);
        else if ((int32_t)k == head) u_frame(x - 1, SH_Y - 1, 15, SH_H + 2, BB_UI_DILLO, 1);
    }
    if (pl_on(in, 16)) u_text(8, 155, &AF_S, "BLACK KEYS: THE NOTE OF THE STEP", BB_UI_GREY, BB_UI_BG);
    for (k = 0; k < 11u; k++) {
        uint32_t key = 16u + k;
        const char *nm = PL_NOTE[pl->key_note[key] % 12u];
        uint16_t f;
        if (!pl_on(in, key)) continue;
        f = pl_fill(in, key, BB_UI_DILLO);
        u_rbox(PL_CX(k), 172, PL_CW, 22, 3, f, BB_UI_BG);
        if (sel >= 0 && (pl->step_gate >> sel & 1u) && pl->step_note[sel] == pl->key_note[key])
            u_frame(PL_CX(k), 172, PL_CW, 22, BB_UI_TEXT, 1);           /* the note the step has */
        u_text(pl_mid(nm, PL_CX(k), PL_CW), 175, &AF_S, nm, in->key_led[key] == BB_UI_LED_LIT ? BB_UI_BG : BB_UI_TEXT, f);
    }
}

/* ------------------------------------------------------------- FX: what each bank does to sound (knob values only) */
static const int16_t PL_SIN[17] = {0, 25, 50, 74, 98, 121, 142, 162, 181, 198, 213, 226, 237, 245, 251, 255, 256};
static int32_t pl_sin(uint32_t p)             /* p: 0..255 one turn -> -256..256 */
{
    uint32_t q = p & 63u, i, f;
    int32_t a;
    if (p & 64u) q = 64u - q;
    i = q >> 2; f = q & 3u;
    a = PL_SIN[i] + (i < 16u ? (PL_SIN[i + 1u] - PL_SIN[i]) * (int32_t)f / 4 : 0);
    return p & 128u ? -a : a;
}
#define FX_Y 76
#define FX_B 194
static PL_NOINLINE void pl_fx_echo(const bb_ui_in *in, uint16_t c)
{
    /* bb_echo: head h reads at TIME x (h+1)/3; the recording loops through head 1 with FEEDBACK. A hit at x 12
     * comes back at every third of TIME m with sum over the heads that are on of FDBK^(m - h - 1). */
    uint32_t beat = in->bpm_x10 >= 200u ? 600000u / in->bpm_x10 : 500u, m, i, q = pl->echo_ms ? pl->echo_ms : 10u;
    int32_t span = 216, x0 = 12, H = FX_B - FX_Y - 26;
    char d[2] = {0, 0};
    for (i = 0; i < 4u; i++) {                                         /* one bar: four beats */
        int32_t x = x0 + (int32_t)(i * (uint32_t)span / 4u);
        d[0] = (char)('1' + i);
        u_rect(x, FX_Y + 16, 1, FX_B - FX_Y - 16, BB_UI_FAINT);
        u_text(x + 3, FX_Y, &AF_S, d, BB_UI_GREY, BB_UI_BG);
    }
    u_rect(x0, FX_B - H, 5, H, BB_UI_TEXT);                            /* the hit */
    for (m = 1; m < 40u; m++) {
        int32_t x = x0 + (int32_t)(m * q / 3u * (uint32_t)span / (4u * beat)), h;
        uint32_t a = 0, hd, r;
        if (x > x0 + span - 4) break;
        for (hd = 0; hd < 3u; hd++)
            if ((pl->heads >> hd & 1u) && m >= hd + 1u) {
                uint32_t g = 256u;
                for (r = hd + 1u; r < m; r++) g = g * pl->fx[BB_PL_FDBK] / 255u;
                a += g;
            }
        if (a > 256u) a = 256u;
        h = (int32_t)(a * (uint32_t)H * (64u + pl->fx[BB_PL_WET] * 3u / 4u) / 65536u);
        if (h > 1) u_rect(x, FX_B - h, 5, h, m % 3u ? c_scale(c, 150) : c);
    }
}
/* TAPE DRIVE, SPRING's tail and MOTION: one sweep across x 12..228, y from the knob values only.
 * TAPE: the recording curve record = x / (1 + DRIVE |x|), then the TONE low-pass, WEAR the wow (grey: clean).
 * SPRING: the tail after a hit, DAMP how fast it dies, SIZE how long it rings, WET how loud.
 * MOTION: the pitch the echoes drift by, MOTION how far, WEAR how unsteady. */
static PL_NOINLINE void pl_fx_wave(uint32_t tab, uint16_t c)
{
    const uint8_t *f = pl->fx;
    int32_t i, y0 = tab == 2u ? 152 : 135, A = tab == 3u ? 6 + f[BB_PL_MOTION] * 42 / 255 : 44, k = f[BB_PL_DRIVE] * 6,
            env = 30 + f[BB_PL_SPRING_WET] / 8, lp = 0, py = y0, pg = y0;
    for (i = 0; i <= 216; i += 2) {
        int32_t s, y, g = y0;
        if (tab == 1u) {
            int32_t ph = i * 512 / 216 + pl_sin((uint32_t)i * 3u) * f[BB_PL_WEAR] / 2560;
            s = pl_sin((uint32_t)ph & 255u);
            g = y0 - s * A / 256;
            lp += (s * (256 + k) / (256 + k * (s < 0 ? -s : s) / 256) - lp) * (32 + f[BB_PL_TONE] * 7 / 8) / 256;
            s = lp;
        } else if (tab == 2u) {
            s = pl_sin((uint32_t)(i * 9) & 255u) * env / 44;
            if (!(i & 7)) env = env * (250 - f[BB_PL_DAMP] / 8 + f[BB_PL_SIZE] / 40) / 256;
        } else
            s = pl_sin((uint32_t)(i * 2) & 255u) * (256 - f[BB_PL_WEAR] / 2) / 256 + pl_sin((uint32_t)(i * 7 + 40) & 255u) * f[BB_PL_WEAR] / 512;
        y = y0 - s * A / 256;
        if (i) {
            if (tab == 1u) u_line(10 + i, pg, 12 + i, g, BB_UI_GREY, 1);
            u_line(10 + i, py, 12 + i, y, c, tab == 2u ? 1 : 2);
        }
        py = y; pg = g;
    }
}
static PL_NOINLINE void pl_fx_sends(const bb_ui_in *in)
{
    static const char *const P[3] = {"KIT A", "KIT B", "DILLO"}, *const E[2] = {"ECHO", "SPRING"};
    uint32_t p, e;
    for (p = 0; p < 3u; p++)
        for (e = 0; e < 2u; e++) {
            int32_t t = pl->send[p][e] * 6 / 255;
            if (t) u_line(70, 92 + 38 * (int32_t)p, 164, 108 + 48 * (int32_t)e, INST_C[p], t);
        }
    for (p = 0; p < 3u; p++) {
        int32_t y = 80 + 38 * (int32_t)p;
        u_rbox(8, y, 62, 25, 4, c_scale(INST_C[p], 70), BB_UI_BG);
        if (p == pl->focus) u_frame(8, y, 62, 25, BB_UI_TEXT, 1);
        u_text(pl_mid(P[p], 8, 62), y + 5, &AF_S, P[p], BB_UI_TEXT, c_scale(INST_C[p], 70));
    }
    for (e = 0; e < 2u; e++) {
        int32_t y = 96 + 48 * (int32_t)e;
        u_rbox(164, y, 68, 25, 4, 0x2124u, BB_UI_BG);
        u_text(pl_mid(E[e], 164, 68), y + 5, &AF_S, E[e], inst_c(in), 0x2124u);
    }
}
static PL_NOINLINE void page_fx5(const bb_ui_in *in)
{
    uint32_t i, n;
    uint16_t c = inst_c(in);
    char w[12], d[2] = {0, 0};
    for (i = 0; in->tab && i < in->tabs && i < 5u; i++) {             /* the tabs are white keys 1-5 */
        const char *t = in->tab[i] ? in->tab[i] : "";
        int32_t x = 5 + 46 * (int32_t)i;
        uint16_t f, ink;
        if (!pl_on(in, i)) continue;
        f = pl_fill(in, i, c);
        ink = in->key_led[i] == BB_UI_LED_LIT ? BB_UI_BG : BB_UI_TEXT;
        for (n = 0; t[n] && t[n] != ' ' && n + 1u < sizeof w; n++) w[n] = t[n];
        w[n] = 0;
        d[0] = (char)('1' + i);
        u_rbox(x, 25, 44, 30, 4, f, BB_UI_BG);
        u_text(pl_mid(d, x, 44), 25, &AF_S, d, ink, f);
        u_text(pl_mid(w, x, 44), 38, &AF_S, w, ink, f);
    }
    if (in->tab && in->tab_cur < in->tabs)
        u_name(8, 56, &AF_M, in->tab[in->tab_cur], BB_UI_TEXT, BB_UI_BG, 224);
    if (in->tab_note) u_text_fit(8, FX_Y, &AF_S, in->tab_note, BB_UI_GREY, BB_UI_BG, 224);   /* TAPE: echo only */
    if (in->tab_cur == 0u) pl_fx_echo(in, c);
    else if (in->tab_cur == 4u) pl_fx_sends(in);
    else {
        if (in->tab_cur == 1u) u_text(12, FX_B - 12, &AF_S, "GREY CLEAN  \xB7  COLOUR THE TAPE", BB_UI_GREY, BB_UI_BG);
        if (in->tab_cur == 2u) {                                       /* the tank: SIZE its length and coils */
            int32_t len = 60 + pl->fx[BB_PL_SIZE] * 150 / 255, turns = 2 * (6 + pl->fx[BB_PL_SIZE] / 20), i;
            u_rect(10, 82, 4, 18, BB_UI_TEXT);
            u_rect(14 + len, 82, 4, 18, BB_UI_TEXT);
            for (i = 1; i <= turns; i++)
                u_line(14 + len * (i - 1) / turns, i & 1 ? 98 : 83, 14 + len * i / turns, i & 1 ? 83 : 98, c, 1);
        }
        if (in->tab_cur == 3u) {
            u_rect(12, 135, 216, 1, BB_UI_FAINT);
            u_text(12, FX_Y + 2, &AF_S, "+ PITCH", BB_UI_GREY, BB_UI_BG);
            u_text(12, FX_B - 12, &AF_S, "- PITCH", BB_UI_GREY, BB_UI_BG);
        }
        pl_fx_wave(in->tab_cur, c);
    }
}

/* ------------------------------------------------------------- dispatch and elements */
static PL_NOINLINE int pl_draw(const bb_ui_in *in, uint32_t page)
{
    if (!pl_owns(page)) return 0;
    if (!pl) return 1;
    switch (page) {
    case BB_UI_PLAY_DRUMS: page_pads(in); break;
    case BB_UI_STEPS: page_dsteps(in); break;
    case BB_UI_FX: page_fx5(in); break;
    default: if (pl->keys) page_dkeys(in); else page_dsheet(in); break;
    }
    return 1;
}
/* elements: one hash of everything the page shows that does not move each step (the LEDs, the play state, the
 * page's inputs), plus a per-element bit for what does (a pad's flash, the playhead, the sounding keys) */
static uint32_t pl_mem(uint32_t h, const void *p, uint32_t n)
{
    const uint8_t *b = (const uint8_t *)p;
    while (n--) h = h_u(h, *b++);
    return h;
}
static PL_NOINLINE int pl_els(els_t *l, const bb_ui_in *in, uint32_t page)
{
    uint32_t i, h, k = 0;
    int32_t head = in->running ? (int32_t)in->step : -1;
    if (!pl_owns(page)) return 0;
    if (!pl) return 1;
    for (i = 0; i < 27u; i++) k |= in->key_led[i] == BB_UI_LED_BREATH;
    h = pl_mem(pl_mem(h_u(H0, k ? breath_q(in->now_ms) : 99u), in->key_led, 27), &pl->pad_name,
               sizeof *pl - offsetof(bb_ui_play, pad_name));
    h = pl_mem(h_u(h, in->instr | (uint32_t)in->accent << 4 | (uint32_t)in->group_muted << 8 | (uint32_t)in->voice << 16 |
                      (uint32_t)in->bank << 24), in->vsteps, sizeof in->vsteps + sizeof in->vacc);
    h = pl_mem(h_u(h, in->banks | (uint32_t)in->tab_cur << 8 | (uint32_t)in->bpm_x10 << 16), in->ratchet, sizeof in->ratchet);
    h = h_u(h, in->note_acc | (uint32_t)in->note_slide << 16);
    h = h_u(h_s(h_s(h_s(h_s(h, in->voice_name), in->patch), in->scale), in->tab_note),
            in->cutoff | (uint32_t)in->reso << 8 | (uint32_t)in->envmod << 16);
    switch (page) {
    case BB_UI_PLAY_DRUMS:
        for (i = 0; i < 12u; i++) el(l, PL_PAD_X(i), PL_PAD_Y(i), 57, 31, h_u(h, i | (uint32_t)(pl->pad_lit >> i & 1u) << 8));
        el(l, 0, 24, 240, 70, h);
        break;
    case BB_UI_STEPS:
        for (k = 0; k < 16u; k++) el(l, 7 + 14 * (int32_t)k, ST_KEY_Y - 2, 15, ST_KEY_H + 4, h_u(h, k | ((int32_t)k == head) << 8));
        el(l, 0, ST_GRID_Y - 4, 240, 48, h_u(h, (uint32_t)head));
        el(l, 0, 24, 240, 54, h);
        break;
    case BB_UI_PLAY_DILLO:
        if (pl->keys) {
            el(l, 0, 24, 240, 34, h_u(h, pl->keys_sounding));
            el(l, 0, 56, 240, 64, h);
            el(l, 0, KB_Y, 240, 72, h_u(h, pl->keys_sounding));
        } else {
            for (k = 0; k < 16u; k++) el(l, 7 + 14 * (int32_t)k, 52, 15, 100, h_u(h, k | ((int32_t)k == head) << 8));
            el(l, 0, 24, 240, 28, h);
            el(l, 0, 152, 240, 44, h);
        }
        break;
    default:
        el(l, 0, 24, 240, 50, h);
        el(l, 0, FX_Y - 2, 240, FX_B - FX_Y + 4, h);
        break;
    }
    return 1;
}
