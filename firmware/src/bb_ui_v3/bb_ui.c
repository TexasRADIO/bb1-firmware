/* SPDX-License-Identifier: GPL-3.0-only
 * Big Beat UI module v3: shared frame, the small Armadillo and the tower rings, brand type for
 * names, pages. See bb_ui.h and README.md.
 *
 * Unity-build unit: needs, before it, the firmware's gfx.c (cv_begin, cv_rect, cv_text_on,
 * cv_rrect, cv_line_t, cv_keycap, text_w, AF_S / AF_M, lcd_sync / lcd_blit, RGB) and
 * build/gen/bb_armadillo.h (bb_armadillo[7200]). Host: host/gfx_host.c does exactly that.
 *
 * Drawing model: a page is drawn by ONE function (page_draw) that draws everything in
 * screen coordinates; the wrappers below translate to the current strip and drop what
 * lies outside it. A redraw of any rectangle = page_draw clipped to it, so a partial
 * redraw always matches a full one (the tests check this pixel for pixel).
 * What to redraw: each page lists ELEMENTS (rectangle + hash of the state shown in it).
 * An element whose hash differs from what is on the screen is dirty; dirty elements
 * are drawn in list order (most important first) until the pixel budget is spent. */
#include "bb_ui.h"
#include "bb/bb_text_block.h"
/* size switches (README "Size"): each 0 drops a feature and its data */
#ifndef BB_UI_HELP
#define BB_UI_HELP 1            /* the HELP overlay (hold HOME) */
#endif
#ifndef BB_UI_ATTRACT_BALL
#define BB_UI_ATTRACT_BALL 1    /* the full-size ball with BIG BEAT in attract mode while stopped (else the mini) */
#endif
#ifndef BB_UI_ALL_PAGES
#define BB_UI_ALL_PAGES 1       /* 0: only KIT, PATTERN, PLAY drums and PLAY Dillo (the firmware integration's four) */
#endif
#ifndef BB_UI_BACKDROP
#define BB_UI_BACKDROP 0        /* the large Armadillo behind every page: 1 layout C, 2 layout A, 0 the corner mini */
#endif
#ifndef BB_UI_HOME
#define BB_UI_HOME 0            /* targeted two-instrument HOME without enabling the other full pages */
#endif
#ifndef BB_UI_PLAY
#define BB_UI_PLAY 1            /* 2.2: PADS, STEPS, Dillo KEYS / STEPS and FX drawn as the instrument (bb_ui_play.c) */
#endif
#ifndef BB_UI_POPUP_BIG
#define BB_UI_POPUP_BIG 0       /* 1: the knob pop-up's value in AF_L 28 px (v2); 0: AF_M + a bar, so AF_L can go */
#endif
#if defined(BB_UI_FIRMWARE) && BB_UI_FIRMWARE
#include "bb_ui_art.h"              /* firmware build: generated into build/gen by tools/gen_ui_v3.py */
#include "bb_ui_brand_fonts.h"
#else
#include "gen/bb_ui_art.h"
#include "gen/bb_ui_brand_fonts.h"
#endif
#ifndef BB_UI_HOOK_LAYER
#define BB_UI_HOOK_LAYER(n) ((void)0)       /* host lint: overlays (pop-up, toast) may cover text */
#endif

/* ------------------------------------------------------------------ strip clip --- */
static int32_t g_cx, g_cy, g_cw, g_ch;        /* screen rect of the canvas being drawn */

static int u_vis(int32_t x, int32_t y, int32_t w, int32_t h)
{
    return w > 0 && h > 0 && x < g_cx + g_cw && x + w > g_cx && y < g_cy + g_ch && y + h > g_cy;
}
static void u_rect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t c)
{
    if (u_vis(x, y, w, h))
        cv_rect(x - g_cx, y - g_cy, w, h, c);
}
static void u_frame(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t c, int32_t t)
{
    u_rect(x, y, w, t, c);
    u_rect(x, y + h - t, w, t, c);
    u_rect(x, y + t, t, h - 2 * t, c);
    u_rect(x + w - t, y + t, t, h - 2 * t, c);
}
static void u_px(int32_t x, int32_t y, uint16_t c)
{
    if (x >= g_cx && x < g_cx + g_cw && y >= g_cy && y < g_cy + g_ch)
        cv_px[(uint32_t)(y - g_cy) * cv_w + (uint32_t)(x - g_cx)] = swap16(c);
}
/* an anti-aliased disc / rounded box (gfx cv_rrect, corners blended against `under`) */
static void u_rbox(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, uint16_t c, uint16_t under)
{
    if (u_vis(x, y, w, h))
        cv_rrect(x - g_cx, y - g_cy, w, h, -r, c, under);
}
static void u_line(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint16_t c, int32_t t)
{
    int32_t lx = x0 < x1 ? x0 : x1, ly = y0 < y1 ? y0 : y1;
    int32_t hx = x0 < x1 ? x1 : x0, hy = y0 < y1 ? y1 : y0;
    if (u_vis(lx, ly, hx - lx + 1, hy - ly + t))
        cv_line_t(x0 - g_cx, y0 - g_cy, x1 - g_cx, y1 - g_cy, c, t);
}
/* text: y = line top (gfx convention); returns the pen x at the end */
static int32_t u_text(int32_t x, int32_t y, const aafont_t *f, const char *s, uint16_t fg, uint16_t bg)
{
    int32_t w = text_w(f, s);
    if (u_vis(x - 2, y, w + 4, f->h))
        cv_text_on(x - g_cx, y - g_cy, f, s, fg, bg);
    return x + w;
}
static int32_t u_text_r(int32_t xr, int32_t y, const aafont_t *f, const char *s, uint16_t fg, uint16_t bg)
{
    return u_text(xr - text_w(f, s), y, f, s, fg, bg);
}
/* a label or value that must fit (the host lint reports one that is cut) */
static void u_text_fit(int32_t x, int32_t y, const aafont_t *f, const char *s, uint16_t fg, uint16_t bg, int32_t maxw)
{
    if (u_vis(x - 2, y, maxw + 4, f->h))
        cv_text_fit(x - g_cx, y - g_cy, f, s, fg, bg, maxw);
}
/* a name (may be shortened with an ellipsis) */
static void u_name(int32_t x, int32_t y, const aafont_t *f, const char *s, uint16_t fg, uint16_t bg, int32_t maxw)
{
    if (u_vis(x - 2, y, maxw + 4, f->h))
        cv_free_text(x - g_cx, y - g_cy, f, s, fg, bg, maxw);
}

/* ------------------------------------------------------------------ colour --- */
static uint16_t c_scale(uint16_t c, uint32_t k)          /* k: 0..256 */
{
    uint32_t r = ((c >> 11) & 31u) * k >> 8, g = ((c >> 5) & 63u) * k >> 8, b = (c & 31u) * k >> 8;
    return (uint16_t)(r << 11 | g << 5 | b);
}
static uint16_t c_mix(uint16_t a, uint16_t b, uint32_t t)  /* t: 0 = a .. 256 = b */
{
    uint32_t ar = (a >> 11) & 31u, ag = (a >> 5) & 63u, ab = a & 31u;
    uint32_t br = (b >> 11) & 31u, bg = (b >> 5) & 63u, bb = b & 31u;
    uint32_t r = (ar * (256u - t) + br * t + 128u) >> 8, g = (ag * (256u - t) + bg * t + 128u) >> 8;
    uint32_t bl = (ab * (256u - t) + bb * t + 128u) >> 8;
    return (uint16_t)(r << 11 | g << 5 | bl);
}
static const uint16_t INST_C[3] = {BB_UI_KIT_A, BB_UI_KIT_B, BB_UI_DILLO};
static uint16_t inst_c(const bb_ui_in *in) { return INST_C[in->instr < 3u ? in->instr : 0u]; }
/* the LED breath (1.1 s triangle), quantised to 8 steps so it costs ~15 redraws a second */
static uint32_t breath_q(uint32_t now)
{
    uint32_t p = now % 1100u, v = p < 550u ? p * 256u / 550u : (1100u - p) * 256u / 550u;
    return v >> 5;                                          /* 0..8 */
}
static uint16_t breath_c(uint16_t c, uint32_t now) { return c_mix(c_scale(c, 110), c, breath_q(now) * 32u); }

static uint32_t u_isqrt(uint32_t v)
{
    uint32_t r = 0, b = 1u << 30;
    while (b > v)
        b >>= 2;
    while (b) {
        if (v >= r + b) {
            v -= r + b;
            r = (r >> 1) + b;
        } else {
            r >>= 1;
        }
        b >>= 2;
    }
    return r;
}

/* ------------------------------------------------------------------ hashing --- */
static uint32_t h_u(uint32_t h, uint32_t v)
{
    uint32_t i;
    for (i = 0; i < 4u; i++, v >>= 8)
        h = (h ^ (v & 0xFFu)) * 16777619u;
    return h;
}
static uint32_t h_s(uint32_t h, const char *s)
{
    if (!s)
        return h_u(h, 0xA5A5u);
    for (; *s; s++)
        h = (h ^ (uint8_t)*s) * 16777619u;
    return (h ^ 0xFFu) * 16777619u;
}
#define H0 2166136261u

/* ============================================================ brand numbers ===
 * BB_UI_BRAND_TYPE (Joe, 28 Oct: "Picnic + top bar"): the brand face on LARGE type only; small labels, notices
 * and the instrument name keep the system face for legibility. 3 (default): knob values and the pop-up value
 * in Picnic, and the page name and tempo in Picnic 15. 0: all system face (as r9.9); 1: values only;
 * 2: values in Ghost Town (rejected for legibility, kept for comparison). A string with a character the
 * face lacks stays in the system face, so "1/8" or "50%" never mixes faces. Baselines match. */
#ifndef BB_UI_BRAND_TYPE
#define BB_UI_BRAND_TYPE 3
#endif
static int brand_has(const char *has, const char *s)
{
    for (; s && *s; s++) {
        char c = *s >= 'a' && *s <= 'z' ? (char)(*s - 32) : *s;
        const char *h = has;
        while (*h && *h != c) h++;
        if (!*h && c != '-') return 0;
    }
    return 1;
}
/* the face for a value / status string, and the y shift that keeps its baseline where the system face had it */
static const aafont_t *brand_face(int role, const char *s, const aafont_t *sys, int32_t *dy)
{
    *dy = 0;
#if BB_UI_BRAND_TYPE
    const aafont_t *f = 0; const char *has = BF_P17_HAS;
    if (role == 0) f = BB_UI_BRAND_TYPE == 2 ? &BF_G19 : &BF_P17;                 /* knob value, was AF_M */
    else if (role == 1) f = BB_UI_BRAND_TYPE == 2 ? &BF_G26 : &BF_P26;            /* pop-up value, was AF_M */
    else if (role == 2 && BB_UI_BRAND_TYPE == 3) f = &BF_P15;                     /* page name / tempo, was AF_S */
    if (f && brand_has(has, s)) { *dy = (int32_t)sys->asc - (int32_t)f->asc; return f; }
#else
    (void)role; (void)s;
#endif
    return sys;
}

/* ============================================================ names in brand type ===
 * Kit and pattern names (Joe 05:31, v3 §2a): Picnic Filled; if the name is too wide, Ghost Town
 * Fill (condensed); capitals (the faces fold lower case); a glyph the face lacks is in the same
 * table, drawn from Inter Tight and raised to the face's cap band (tools/gen_ui_v3.py).
 * Titles (26 px) may take a second line, full width under the mini, when neither face fits one;
 * anything still too wide is ellipsised (never for a catalogue name: tests/render.c checks all). */
enum { NM_TITLE, NM_NOW, NM_LANE, NM_LIST, NM_ROLES };
static const aafont_t *const NM_FACE[NM_ROLES][2] = {
#if BB_UI_ALL_PAGES
    {&BF_P26, &BF_G26}, {&BF_P19, &BF_G19}, {&BF_P17, &BF_P17}, {&BF_P15, &BF_G15}};
#else
    {&BF_P26, &BF_G26}, {&BF_P19, &BF_G19}, {&BF_P15, &BF_G15}, {&BF_P15, &BF_G15}};   /* (no STUDIO: no 17 px) */
#endif
static const uint8_t NM_CAP_Y[NM_ROLES][2] = {
#if BB_UI_ALL_PAGES
    {BF_P26_CAP_Y, BF_G26_CAP_Y}, {BF_P19_CAP_Y, BF_G19_CAP_Y}, {BF_P17_CAP_Y, BF_P17_CAP_Y}, {BF_P15_CAP_Y, BF_G15_CAP_Y}};
#else
    {BF_P26_CAP_Y, BF_G26_CAP_Y}, {BF_P19_CAP_Y, BF_G19_CAP_Y}, {BF_P15_CAP_Y, BF_G15_CAP_Y}, {BF_P15_CAP_Y, BF_G15_CAP_Y}};
#endif
#define NM_TITLE_LEAD 30        /* second title line: 30 px below the first */
#define NM_MAX 48

typedef struct { uint8_t face, split, cut; } nm_lay;   /* face 0 Picnic / 1 Ghost Town; split: the space ending line 1 (0 = one line) */

static int32_t nm_w(const aafont_t *f, const char *s, uint32_t n)       /* the width of s's first n characters */
{
    char b[NM_MAX];
    uint32_t i;
    for (i = 0; i < n && s[i] && i < NM_MAX - 1u; i++)
        b[i] = s[i];
    b[i] = 0;
    return text_w(f, b);
}
static uint32_t nm_len(const char *s)
{
    uint32_t n = 0;
    while (s[n] && n < NM_MAX - 1u)
        n++;
    return n;
}
/* how s is set in role r: one line within maxw; for titles (maxw2 > 0) else two lines, line 2 within maxw2 */
static nm_lay name_layout(uint32_t r, const char *s, int32_t maxw, int32_t maxw2)
{
    nm_lay L;
    uint32_t n = nm_len(s), face, i;
    L.split = 0;
    L.cut = 0;
    for (face = 0; face < 2u; face++)
        if (text_w(NM_FACE[r][face], s) <= maxw) {
            L.face = (uint8_t)face;
            return L;
        }
    if (maxw2 > 0)
        for (face = 0; face < 2u; face++) {
            const aafont_t *f = NM_FACE[r][face];
            int32_t best = 0x7FFF;
            for (i = 1; i < n; i++)                          /* the space that balances the two lines best */
                if (s[i] == ' ') {
                    int32_t a = nm_w(f, s, i), b = text_w(f, s + i + 1);
                    if (a <= maxw && b <= maxw2 && (a > b ? a : b) < best) {
                        best = a > b ? a : b;
                        L.split = (uint8_t)i;
                    }
                }
            if (L.split) {
                L.face = (uint8_t)face;
                return L;
            }
        }
    L.face = 1;
    L.cut = 1;
    return L;
}
/* draw s in role r with its capitals' top at ycap; returns the lines used */
static uint32_t u_brand(int32_t x, int32_t ycap, uint32_t r, const char *s, uint16_t fg, uint16_t bg, int32_t maxw,
                        int32_t maxw2)
{
    nm_lay L;
    const aafont_t *f;
    int32_t y;
    if (!s)
        s = "";
    L = name_layout(r, s, maxw, maxw2);
    f = NM_FACE[r][L.face];
    y = ycap - NM_CAP_Y[r][L.face];
    if (L.cut) {
        if (u_vis(x - 2, y, maxw + 4, f->h))
            cv_free_text(x - g_cx, y - g_cy, f, s, fg, bg, maxw);
        return 1;
    }
    if (!L.split) {
        u_text(x, y, f, s, fg, bg);
        return 1;
    }
    {
        char a[NM_MAX];
        uint32_t i;
        for (i = 0; i < L.split; i++)
            a[i] = s[i];
        a[L.split] = 0;
        u_text(x, y, f, a, fg, bg);
        u_text(x, y + NM_TITLE_LEAD, f, s + L.split + 1, fg, bg);
    }
    return 2;
}

/* ==================================================== the tower rings (installed) ===
 * A port of the installed playing face (bb_playbook_face.inc bb_pb_motion / bb_pb_art /
 * bb_pb_ring): on every quarter-note tick while playing a ring is born at the tower tip
 * (120, 12); it grows from 6 to 74 px over 900 ms while fading; up to four live at once; five
 * ember sparks ride it along the rays. Stopped: none are born and the live ones go.
 * Here the geometry is scaled to the drawing (size / 240: the mini 46 or 34, attract 240). */
static void rings_motion(bb_ui_rings *g, uint32_t live, uint32_t tick, uint32_t now)
{
    uint32_t i;
    if (!live) {
        for (i = 0; i < 4u; i++) {
            g->wave[i].born = 0;
            g->wave[i].active = 0;
        }
        g->tick = tick;
        g->seen = 1;
        return;
    }
    if (g->seen && tick && tick != g->tick && (tick - 1u) / 4u != (g->tick ? g->tick - 1u : 0u) / 4u) {
        g->wave[g->slot % 4u].born = now;
        g->wave[g->slot % 4u].active = 1;
        g->slot++;
    }
    g->tick = tick;
    g->seen = 1;
    for (i = 0; i < 4u; i++)
        if ((uint32_t)(now - g->wave[i].born) >= BB_UI_RING_MS)
            g->wave[i].active = 0;
}
#ifndef BB_SPARK_EMBER                  /* (the firmware defines the same ember in bb_target_ui.c) */
#define BB_SPARK_EMBER RGB(250, 183, 114)
#endif
static const int8_t RING_RAY[5][2] = {{-29, 8}, {-21, 21}, {0, 30}, {21, 21}, {29, 8}};
typedef struct { int32_t x0, y0, x1, y1; } clip_t;     /* screen rect the rings may ink */
static void ring_px(const clip_t *c, int32_t x, int32_t y, uint16_t ink)
{
    if (x >= c->x0 && x < c->x1 && y >= c->y0 && y < c->y1)
        u_px(x, y, ink);
}
static void ring_circle(const clip_t *c, int32_t x, int32_t y, uint32_t radius, uint16_t ink)
{
    int32_t a = (int32_t)radius, b = 0, error = 1 - (int32_t)radius;
    while (a >= b) {
        ring_px(c, x + a, y + b, ink); ring_px(c, x + b, y + a, ink);
        ring_px(c, x - b, y + a, ink); ring_px(c, x - a, y + b, ink);
        ring_px(c, x - a, y - b, ink); ring_px(c, x - b, y - a, ink);
        ring_px(c, x + b, y - a, ink); ring_px(c, x + a, y - b, ink);
        b++;
        if (error < 0)
            error += 2 * b + 1;
        else {
            a--;
            error += 2 * (b - a) + 1;
        }
    }
}
/* one ring of age `age` ms at the tip (tx, ty), scaled size / 240 */
static void ring_draw(const clip_t *c, int32_t tx, int32_t ty, uint32_t age, int32_t size)
{
    uint32_t r = 6u + age * 68u / 900u, fade = 900u - age, j;
    uint16_t ink = RGB(34u + fade * 120u / 900u, 42u + fade * 69u / 900u, 42u + fade * 24u / 900u);
    if (!u_vis(c->x0, c->y0, c->x1 - c->x0, c->y1 - c->y0))
        return;
    ring_circle(c, tx, ty, r * (uint32_t)size / 240u, ink);
    for (j = 0; j < 5u; j++)
        ring_px(c, tx + RING_RAY[j][0] * (int32_t)(r + 4u) / 30 * size / 240,
                ty + RING_RAY[j][1] * (int32_t)(r + 4u) / 30 * size / 240, BB_SPARK_EMBER);
}
/* the rings as drawn at time t (the mini's clock, so a redraw is a function of the hash) */
static void rings_draw(const bb_ui_rings *g, const clip_t *c, int32_t tx, int32_t ty, int32_t size, uint32_t t)
{
    uint32_t i;
    for (i = 0; i < 4u; i++)
        if (g->wave[i].active && (int32_t)(t - g->wave[i].born) >= 0 && t - g->wave[i].born < BB_UI_RING_MS)
            ring_draw(c, tx, ty, t - g->wave[i].born, size);
}
static uint32_t mini_clock(uint32_t now) { return now - now % BB_UI_MINI_MS; }
static uint32_t rings_hash(const bb_ui_rings *g, uint32_t now)
{
    uint32_t i, h = H0, t = mini_clock(now), any = 0;
    for (i = 0; i < 4u; i++)
        if (g->wave[i].active && (int32_t)(t - g->wave[i].born) >= 0 && t - g->wave[i].born < BB_UI_RING_MS) {
            h = h_u(h, t - g->wave[i].born);
            any = 1;
        }
    return any ? h : 0u;
}

/* ============================================================ the small Armadillo ===
 * v3 §1.3, §4: the transport and beat indicator, top right, made from OUR plate: the silhouette
 * at ~40 % in the instrument colour, the plate's own lines on top (2-bit minis, gen/bb_ui_art.h).
 * Playing: the plate + the tower rings. STOP: the plate re-inks into the ball through a 4x4
 * ordered (Bayer) pattern in 5 steps, then BIG BEAT inks in in 4 (START reverses it). Fault: red. */
static const uint8_t BAYER4[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};
static const uint8_t ROLL_THR[BB_UI_ROLL_STEPS + 1] = {0, 3, 6, 9, 12, 16, 16, 16, 16, 16};
static const uint8_t RLET_THR[BB_UI_ROLL_STEPS + 1] = {0, 0, 0, 0, 0, 0, 4, 8, 12, 16};
static uint32_t roll_step_ms(uint32_t roll) { return roll >= 5u ? 60u : BB_UI_ROLL_MS; }

static uint32_t px2(const uint8_t *d, uint32_t w, uint32_t x, uint32_t y)
{
    uint32_t i = y * w + x;
    return (uint32_t)(d[i >> 2] >> ((i & 3u) * 2u)) & 3u;
}
/* the value (0 none, 1 fill, 2 line, 3 lettering) of a pixel at roll step `roll` */
static uint32_t roll_pick(uint32_t plate, uint32_t ball, uint32_t roll, int32_t x, int32_t y)
{
    uint32_t bay = BAYER4[y & 3][x & 3];
    if (bay >= ROLL_THR[roll])
        return plate;
    if (ball == 3u && bay >= RLET_THR[roll])
        return 1u;                                           /* lettering not inked yet: the shell under it */
    return ball;
}
static uint16_t mini_c(const bb_ui_in *in);
/* the mini (size 46 or 34) at (x0, y0), with its rings clipped to box c */
static void draw_mini(const bb_ui *u, const bb_ui_in *in, int32_t x0, int32_t y0, int32_t size, const clip_t *c)
{
    const uint8_t *pl = size == 46 ? BB_MINI46_PLATE : BB_MINI34_PLATE, *ba = size == 46 ? BB_MINI46_BALL : BB_MINI34_BALL;
    uint16_t line = mini_c(in), fill = c_scale(line, 97), let = in->fault ? c_scale(BB_UI_RED, 230) : BB_UI_TEXT;
    int32_t x, y, xa = x0 < g_cx ? g_cx : x0, xb = x0 + size > g_cx + g_cw ? g_cx + g_cw : x0 + size;
    int32_t ya = y0 < g_cy ? g_cy : y0, yb = y0 + size > g_cy + g_ch ? g_cy + g_ch : y0 + size;
    for (y = ya; y < yb; y++)
        for (x = xa; x < xb; x++) {
            uint32_t v = roll_pick(px2(pl, (uint32_t)size, (uint32_t)(x - x0), (uint32_t)(y - y0)),
                                   px2(ba, (uint32_t)size, (uint32_t)(x - x0), (uint32_t)(y - y0)), u->roll, x - x0, y - y0);
            if (v)
                u_px(x, y, v == 1u ? fill : v == 2u ? line : let);
        }
    if (in->running && !in->fault && !u->roll)
        rings_draw(&u->rings, c, x0 + (size == 46 ? BB_MINI46_TIP_X : BB_MINI34_TIP_X),
                   y0 + (size == 46 ? BB_MINI46_TIP_Y : BB_MINI34_TIP_Y), size, mini_clock(in->now_ms));
}
static uint32_t mini_hash(const bb_ui *u, const bb_ui_in *in)
{
    uint32_t h = h_u(H0, (uint32_t)u->roll | (uint32_t)mini_c(in) << 8);
    return in->running && !in->fault && !u->roll ? h_u(h, rings_hash(&u->rings, in->now_ms)) : h;
}

/* ================================================================ shared frame ===
 * y 0-23 STATUS · y 24-197 STAGE · y 198-239 KNOB BAR (audit §3.2, grid §4.1) */
#define STATUS_TXT_Y 4          /* AF_S line top: capitals on rows 7..15 (centred in 0..23) */
#define KNOB_X(k) (8 + 57 * (k))
#define KNOB_LABEL_Y 200
#define KNOB_VALUE_Y 213
#define KNOB_BAR_Y 235
#define KEYS_Y 181

/* the status bar's instrument: dot + name in the instrument colour, centred on x 104 */
static void draw_status(const bb_ui_in *in, uint32_t now)
{
    uint16_t c = inst_c(in);
    const char *nm = in->instr_name ? in->instr_name : "";
    int32_t w = text_w(&AF_S, nm), x0 = 110 - w / 2, i, pw, d0;
    const char *pn = in->page_name ? in->page_name : "";
    pw = text_w(brand_face(2, pn, &AF_S, &d0), pn);
    if (pw + w > 148 - 22)                                    /* RC1: "FX  TAPE DRIVE" beside "KIT A" would be cut: */
        for (const char *q = pn; *q; q++)                     /* the bank alone ("TAPE DRIVE"); the page shows its tabs */
            if (q[0] == ' ' && q[1] == ' ') { pn = q + 2; pw = text_w(brand_face(2, pn, &AF_S, &d0), pn); break; }
    char bpm[8];
    uint32_t b = in->bpm_x10, n = 0;
    if (x0 < 8 + pw + 20)                                     /* a long page name pushes the instrument right */
        x0 = 8 + pw + 20;
    if (x0 + w > 148)
        x0 = 148 - w;
    if (in->page == BB_UI_PATTERN && in->tabs && !in->welcome) {              /* RC2 (Joe 10:26-10:28): HOME's SONGS / ERAS / PRESETS */
        int32_t x = 8;
        for (i = 0; i < (int32_t)in->tabs && i < 3; i++) {
            if (i == (int32_t)in->tab_cur) u_rect(x - 2, 20, text_w(&AF_S, in->tab[i]) + 4, 2, c);
            x = u_text(x, STATUS_TXT_Y, &AF_S, in->tab[i], i == (int32_t)in->tab_cur ? BB_UI_TEXT : BB_UI_GREY, BB_UI_BG) + 9;
        }
    } else {
        {
            int32_t dy; const aafont_t *pf = brand_face(2, pn, &AF_S, &dy);
            u_text_fit(8, STATUS_TXT_Y + dy, pf, pn, BB_UI_TEXT, BB_UI_BG, x0 - 10 - 8 - 4);
        }
        u_rbox(x0 - 10, 9, 6, 6, 3, in->accent ? BB_UI_RED : c, BB_UI_BG);
        u_text(x0, STATUS_TXT_Y, &AF_S, nm, c, BB_UI_BG);
    }
    for (i = 0; i < 4; i++) {                                 /* beat dots: the universal pulse */
        uint16_t d = !in->running ? BB_UI_FAINT : (uint32_t)i == in->beat ? BB_UI_TEXT : BB_UI_GREY;
        u_rbox(154 + 8 * i, 10, 5, 5, 2, d, BB_UI_BG);
    }
    if (b > 9999u)                                            /* tempo: "120" or "92.5" */
        b = 9999u;
    if (b / 1000u) bpm[n++] = (char)('0' + b / 1000u);
    if (b / 100u) bpm[n++] = (char)('0' + b / 100u % 10u);
    bpm[n++] = (char)('0' + b / 10u % 10u);
    if (b % 10u) {
        bpm[n++] = '.';
        bpm[n++] = (char)('0' + b % 10u);
    }
    bpm[n] = 0;
    {
        int32_t dy; const aafont_t *tf = brand_face(2, bpm, &AF_S, &dy);
        u_text_r(214, STATUS_TXT_Y + dy, tf, bpm, BB_UI_TEXT, BB_UI_BG);
    }
    if (in->running) {                                        /* play: a triangle */
        int32_t r;
        for (r = 0; r < 11; r++) {
            int32_t half = 5 - (r < 5 ? 5 - r : r - 5);
            u_rect(222, 7 + r, half * 9 / 5 + 1, 1, BB_UI_TEXT);
        }
    } else {                                                  /* stop: a square (breathes when start is queued) */
        u_rect(222, 8, 9, 9, (in->queued & BB_UI_Q_START) ? breath_c(BB_UI_TEXT, now) : BB_UI_GREY);
    }
    u_rect(8, 23, 224, 1, BB_UI_FAINT);
}

/* a K1-K4 label that is wider than its column (bb_nav: "TRANSPOS", "MOTION %") is shortened by
 * rule, never ellipsised: drop a trailing " %", then cut to six letters and a full stop */
static const char *short_label(const char *s, char *buf, uint32_t n, int32_t maxw)
{
    uint32_t k = 0;
    if (!s || text_w(&AF_S, s) <= maxw)
        return s;
    while (s[k] && k + 1u < n) { buf[k] = s[k]; k++; }
    buf[k] = 0;
    if (k >= 2u && buf[k - 1u] == '%' && buf[k - 2u] == ' ') { buf[k - 2u] = 0; k -= 2u; }
    if (text_w(&AF_S, buf) > maxw && k > 6u) { buf[6] = '.'; buf[7] = 0; }
    return buf;
}

static void draw_knobs(const bb_ui_in *in, uint32_t pop_k)
{
    char lb[12];
    uint16_t c = inst_c(in);
    uint32_t k;
    u_rect(8, 198, 224, 1, BB_UI_FAINT);
    for (k = 0; k < 4u; k++) {
        const bb_ui_knob *kn = &in->knob[k];
        int32_t x = KNOB_X(k), fill;
        uint32_t mute = kn->flags & BB_UI_KN_MUTE;
        if ((kn->flags & BB_UI_KN_OFF) || !kn->label)
            continue;
        u_text_fit(x, KNOB_LABEL_Y, &AF_S, short_label(kn->label, lb, sizeof lb, 56), k == pop_k ? BB_UI_TEXT : BB_UI_GREY,
                   BB_UI_BG, 56);
        {
            const char *vt = mute ? "MUTE" : kn->text ? kn->text : "";
            int32_t dy; const aafont_t *vf = brand_face(0, vt, &AF_M, &dy);
            if (vf != &AF_M && text_w(vf, vt) > 54) { vf = &AF_M; dy = 0; }
            u_text_fit(x, KNOB_VALUE_Y + dy, vf, vt, mute ? BB_UI_RED : BB_UI_TEXT, BB_UI_BG, 54);
        }
        fill = (int32_t)kn->bar * 50 / 255;
        u_rect(x, KNOB_BAR_Y, 50, 2, BB_UI_FAINT);
        u_rect(x, KNOB_BAR_Y, fill, 2, mute ? c_scale(c, 90) : c);
    }
}

/* the key map strip: 16 white + 11 black keys, mirroring the key LEDs (§4.4) */
static int32_t black_x(uint32_t j)           /* 2.2: black key j sits after white key AFTER[j]: the FM-1 runs F..G */
{
    static const uint8_t AFTER[11] = {0, 1, 2, 4, 5, 7, 8, 9, 11, 12, 14};
    return 8 + 14 * AFTER[j] + 9;
}
static uint16_t key_c(const bb_ui_in *in, uint32_t key, uint32_t now)
{
    uint16_t c = inst_c(in);
    uint32_t led = in->key_led[key];
    if (in->keys_down >> key & 1u)
        return BB_UI_TEXT;
    if (in->keys_red >> key & 1u && led != BB_UI_LED_DARK)
        return BB_UI_RED;
    return led == BB_UI_LED_LIT ? c : led == BB_UI_LED_DIM ? c_scale(c, 80) :
           led == BB_UI_LED_BREATH ? breath_c(c, now) : BB_UI_FAINT;
}
static uint32_t keys_any(const bb_ui_in *in)
{
    uint32_t k, any = in->keys_down;
    for (k = 0; k < 27u; k++)
        any |= in->key_led[k];
    return any;
}
static void draw_keys(const bb_ui_in *in, uint32_t now)
{
    uint32_t i;
    if (!keys_any(in))
        return;                                  /* every key dark: no strip (FX, KNOBS pages) */
    for (i = 0; i < 16u; i++)
        u_rect(8 + 14 * (int32_t)i, KEYS_Y + 6, 13, 9, key_c(in, i, now));
    for (i = 0; i < 11u; i++)
        u_rect(black_x(i), KEYS_Y, 8, 5, key_c(in, 16u + i, now));
}

/* knob pop-up: the turned knob's value over its own column (600 ms). v3 default: label, the value in
 * AF_M and a thick bar, so the 28 px AF_L face is no longer needed (BB_UI_POPUP_BIG=1: v2's AF_L value) */
#define POP_Y 112
#define POP_W 64
#define POP_H 64
static int32_t pop_x(uint32_t k) { return KNOB_X(k) - 4 + (k == 3u ? -8 : 0); }
static void draw_popup(const bb_ui_in *in, uint32_t k)
{
    const bb_ui_knob *kn = &in->knob[k];
    int32_t x = pop_x(k);
    uint16_t c = inst_c(in), panel = 0x0861u;               /* (12, 12, 11) */
    uint32_t mute = kn->flags & BB_UI_KN_MUTE;
    char lb[12];
    u_rbox(x, POP_Y, POP_W, POP_H, 6, c, BB_UI_BG);
    u_rbox(x + 2, POP_Y + 2, POP_W - 4, POP_H - 4, 4, panel, c);
    u_text_fit(x + 8, POP_Y + 6, &AF_S, short_label(kn->label ? kn->label : "", lb, sizeof lb, POP_W - 14), c, panel,
               POP_W - 14);
    if (mute) {
        u_text_fit(x + 8, POP_Y + 30, &AF_M, "MUTE", BB_UI_RED, panel, POP_W - 12);
        return;
    }
#if BB_UI_POPUP_BIG
    u_text_fit(x + 8, POP_Y + 22, &AF_L, kn->text ? kn->text : "", BB_UI_TEXT, panel, POP_W - 12);
#else
    {
        const char *vt = kn->text ? kn->text : "";
        int32_t dy; const aafont_t *vf = brand_face(1, vt, &AF_M, &dy);
        if (vf != &AF_M && text_w(vf, vt) > POP_W - 12) { vf = &AF_M; dy = 0; }
        /* the brand number sits on its own line: its capitals start below the label's (ink rows POP_Y+6..18)
         * and its line ends at the bar (POP_Y+48) */
        u_text_fit(x + 8, vf == &AF_M ? POP_Y + 24 : POP_Y + 48 - (int32_t)vf->h, vf, vt, BB_UI_TEXT, panel, POP_W - 12);
    }
    u_rect(x + 8, POP_Y + 48, POP_W - 16, 6, BB_UI_FAINT);
    u_rect(x + 8, POP_Y + 48, (int32_t)kn->bar * (POP_W - 16) / 255, 6, c);
#endif
}

/* fault: the mini Armadillo in red and one line of plain words (replaces every BUSY / FAULT text) */
#define TOAST_X 24
#define TOAST_Y 64
#define TOAST_W 192
#define TOAST_H 84
static void draw_toast(const bb_ui *u, const bb_ui_in *in)
{
    uint16_t panel = 0x0861u;
    clip_t none = {0, 0, 0, 0};
    u_rbox(TOAST_X, TOAST_Y, TOAST_W, TOAST_H, 6, BB_UI_RED, BB_UI_BG);
    u_rbox(TOAST_X + 2, TOAST_Y + 2, TOAST_W - 4, TOAST_H - 4, 4, panel, BB_UI_RED);
    draw_mini(u, in, TOAST_X + 73, TOAST_Y + 6, 46, &none);
    u_name(TOAST_X + 10, TOAST_Y + 56, &AF_M, in->fault_text ? in->fault_text : "", BB_UI_TEXT, panel, TOAST_W - 20);
}

/* notice: the firmware's current message (BUSY, REFUSED, CANCELLED, TIMED OUT ...) in one line,
 * in the instrument colour, inside the toast slot so its element covers it */
#define NOTICE_Y (TOAST_Y + 24)
#define NOTICE_H 36
static void draw_notice(const bb_ui_in *in)
{
    uint16_t panel = 0x0861u, c = inst_c(in);
    u_rbox(TOAST_X, NOTICE_Y, TOAST_W, NOTICE_H, 6, c, BB_UI_BG);
    u_rbox(TOAST_X + 2, NOTICE_Y + 2, TOAST_W - 4, NOTICE_H - 4, 4, panel, c);
    /* r9: a notice too wide for AF_M ("AUDIO STOPPED: RENDER FAULT") is set in AF_S, whole, not cut */
    int big = text_w(&AF_M, in->notice) <= TOAST_W - 20;
    u_name(TOAST_X + (big ? 10 : 6), NOTICE_Y + (big ? 10 : 12), big ? &AF_M : &AF_S, in->notice, BB_UI_TEXT, panel,
           TOAST_W - (big ? 20 : 12));
}

/* =================================================================== pages === */
static uint32_t flash_level(const bb_ui *u, uint32_t g, uint32_t now)
{
    uint32_t t = u->flash_t[g], age;
    if (!t)
        return 0;
    age = now - (t & ~1u);
    return age < BB_UI_FLASH_MS ? 1u : 0u;
}
#if BB_UI_ALL_PAGES
static uint32_t beat_age(const bb_ui *u, uint32_t now) { return u->beat_t ? now - (u->beat_t & ~1u) : 0xFFFFu; }
static uint32_t beat_ms(const bb_ui_in *in) { return in->bpm_x10 >= 200u ? 600000u / in->bpm_x10 : 3000u; }
#endif
static uint16_t mini_c(const bb_ui_in *in) { return in->fault ? BB_UI_RED : inst_c(in); }

/* where the mini sits: 46 px on PLAY drums, 34 px on STUDIO / KIT / PATTERN / Dillo (v3 §3) */
#define MINI46_X 190
#define MINI46_Y 25
#define MINI34_X 204
#define MINI34_Y 25
static const clip_t MINI46_BOX = {188, 24, 240, 72};     /* the mini's region (its rings stay inside) */
static const clip_t MINI34_BOX = {200, 24, 240, 60};

/* "HOOK · BAR 3/8" */
static void bar_text(char *b, uint32_t n, const bb_ui_in *in)
{
    uint32_t k = 0;
    const char *s = in->section_name ? in->section_name : "";
    while (*s && k + 1u < n) b[k++] = *s++;
    if (in->bar && in->bars && k + 16u < n) {
        static const char mid[] = "  \xB7  BAR ";
        const char *m = in->section_name && in->section_name[0] ? mid : mid + 5;
        while (*m) b[k++] = *m++;
        if (in->bar >= 10u) b[k++] = (char)('0' + in->bar / 10u % 10u);
        b[k++] = (char)('0' + in->bar % 10u);
        b[k++] = '/';
        if (in->bars >= 10u) b[k++] = (char)('0' + in->bars / 10u % 10u);
        b[k++] = (char)('0' + in->bars % 10u);
    }
    b[k] = 0;
}

/* ---------------------------------------------------------------- PLAY drums (v3 §2)
 * y 24-71 now row: pattern name, SECTION · BAR n/N, NEXT (breathing) | the mini + rings
 * y 72-180 four group lanes: 16 steps, the playhead box, live group level; muted: red name, dim steps
 * y 182-196 master: OUT L / R (the published output peak), DSP (audio block time / budget) */
#define NOW_CAP_Y 30
#define NOW_INFO_Y 50
#define NEXT_X 116
#define LANES_Y 72
#define LANE_H 27
#define CELL_X(s) (50 + 10 * (s))
#define GMETER_X 214
#define MASTER_Y 182
static const char *const GROUP_NAME[4] = {"KICK", "SN/CL", "HATS", "PERC"};
#if !BB_UI_PLAY
static void page_play_drums(const bb_ui *u, const bb_ui_in *in, uint32_t now)
{
    uint16_t c = inst_c(in), dimred = c_mix(BB_UI_BG, BB_UI_RED, 100);
    uint32_t g, s;
    char b[40];
    u_brand(8, NOW_CAP_Y, NM_NOW, in->pattern_name, BB_UI_TEXT, BB_UI_BG, MINI46_X - 8 - 6, 0);
    bar_text(b, sizeof b, in);
    u_text_fit(8, NOW_INFO_Y, &AF_S, b, BB_UI_GREY, BB_UI_BG, NEXT_X - 8 - 6);
    if (in->next_name && in->next_name[0]) {
        uint32_t k = 0;
        const char *p = in->next_name;
        b[k++] = 'N'; b[k++] = 'E'; b[k++] = 'X'; b[k++] = 'T'; b[k++] = ' '; b[k++] = ' ';
        while (*p && k + 1u < sizeof b) b[k++] = *p++;
        b[k] = 0;
        u_text_fit(NEXT_X, NOW_INFO_Y, &AF_S, b, breath_c(c, now), BB_UI_BG, MINI46_X - NEXT_X - 4);
    }
    if (!BB_UI_BACKDROP) draw_mini(u, in, MINI46_X, MINI46_Y, 46, &MINI46_BOX);
    for (g = 0; g < 4u; g++) {                                   /* the four group lanes */
        int32_t y = LANES_Y + LANE_H * (int32_t)g, lw;
        uint32_t muted = in->group_muted >> g & 1u, lv = muted || !in->running ? 0u : in->group_out[g];
        u_text(8, y + 5, &AF_S, GROUP_NAME[g], muted ? BB_UI_RED : BB_UI_GREY, BB_UI_BG);
        for (s = 0; s < 16u; s++) {
            int32_t x = CELL_X((int32_t)s);
            uint32_t on = in->gsteps[g] >> s & 1u, acc = in->gacc[g] >> s & 1u;
            if (on)
                u_rect(x, y + 5, 8, 15, muted ? dimred : acc ? c : c_scale(c, 140));
            else
                u_frame(x, y + 5, 8, 15, BB_UI_FAINT, 1);
            if (in->running && s == in->step)
                u_frame(x - 1, y + 3, 10, 19, BB_UI_TEXT, 1);
        }
        lw = (int32_t)(lv * 20u / 255u);                         /* the group's live level */
        u_frame(GMETER_X, y + 5, 21, 15, flash_level(u, g, now) && !muted ? BB_UI_GREY : BB_UI_FAINT, 1);
        u_rect(GMETER_X + 1, y + 6, lw, 13, c);
    }
    u_text(8, MASTER_Y, &AF_S, "OUT", BB_UI_GREY, BB_UI_BG);     /* master: OUT L / R, DSP */
    for (s = 0; s < 2u; s++) {
        int32_t x = 34 + 78 * (int32_t)s, w = (int32_t)in->out_peak[s] * 69 / 255;
        u_frame(x, MASTER_Y + 4, 71, 7, BB_UI_FAINT, 1);
        u_rect(x + 1, MASTER_Y + 5, w, 5, in->out_peak[s] >= 252u ? BB_UI_RED : BB_UI_TEXT);
    }
    u_text(192, MASTER_Y, &AF_S, "DSP", BB_UI_GREY, BB_UI_BG);
    u_frame(220, MASTER_Y + 4, 14, 7, BB_UI_FAINT, 1);
    u_rect(221, MASTER_Y + 5, (int32_t)in->dsp * 12 / 255, 5, in->dsp >= 230u ? BB_UI_RED : c);
}
#endif

#if BB_UI_ALL_PAGES
/* ---------------------------------------------------------------- STUDIO (v3 §3)
 * three lanes: name, section, NEXT; the pattern in brand type; 16 steps with the playhead; live level */
#define LANE_Y(r) (29 + 50 * (r))
#define SCELL_X(i) (16 + 12 * (i))
static void page_studio(const bb_ui *u, const bb_ui_in *in, uint32_t now)
{
    uint32_t r;
    for (r = 0; r < 3u; r++) {
        const bb_ui_lane *l = &in->lane[r];
        int32_t y = LANE_Y(r), x, i, lw;
        uint16_t ic = INST_C[r], c = (l->flags & BB_UI_LN_MUTED) ? c_scale(ic, 100) : ic;
        const char *part = (l->flags & BB_UI_LN_MUTED) ? "MUTED" : l->part ? l->part : "";
        if (l->flags & BB_UI_LN_OFF) {
            u_text(16, y + 8, &AF_M, "+", BB_UI_FAINT, BB_UI_BG);
            continue;
        }
        u_rect(8, y, (l->flags & BB_UI_LN_FOCUS) ? 5 : 3, 41, c);
        x = u_text(16, y - 2, &AF_S, l->name ? l->name : "", c, BB_UI_BG);
        u_text_fit(x + 10, y - 2, &AF_S, part, (l->flags & BB_UI_LN_MUTED) ? BB_UI_RED : BB_UI_GREY, BB_UI_BG,
                   NEXT_X + 4 - x - 10 - 6);
        if (l->next && l->next[0] && !(l->flags & BB_UI_LN_MUTED)) {
            char b[24];
            uint32_t k = 0;
            const char *p = l->next;
            b[k++] = 'N'; b[k++] = 'E'; b[k++] = 'X'; b[k++] = 'T'; b[k++] = ' '; b[k++] = ' ';
            while (*p && k + 1u < sizeof b) b[k++] = *p++;
            b[k] = 0;
            u_text_fit(NEXT_X + 4, y - 2, &AF_S, b, breath_c(ic, now), BB_UI_BG, MINI34_X - NEXT_X - 4 - 6);
        }
        u_brand(16, y + 15, NM_LANE, l->pattern, (l->flags & BB_UI_LN_MUTED) ? BB_UI_GREY : BB_UI_TEXT, BB_UI_BG,
                MINI34_X - 16 - 4, 0);
        for (i = 0; i < 16; i++) {                               /* this bar's steps, the playhead under */
            int32_t sx = SCELL_X(i);
            if (l->accents >> i & 1u)
                u_rect(sx, y + 31, 10, 9, c);
            else if (l->steps >> i & 1u)
                u_rect(sx, y + 31, 10, 9, c_scale(c, 115));
            else
                u_frame(sx, y + 31, 10, 9, BB_UI_FAINT, 1);
            if (in->running && i == (int32_t)in->step)
                u_rect(sx, y + 41, 10, 2, BB_UI_TEXT);
        }
        lw = (int32_t)l->level * 19 / 255;                       /* live level */
        u_frame(212, y + 31, 21, 9, BB_UI_FAINT, 1);
        u_rect(213, y + 32, lw, 7, ic);
    }
    if (!BB_UI_BACKDROP) draw_mini(u, in, MINI34_X, MINI34_Y, 34, &MINI34_BOX);
}

/* ---------------------------------------------------------------- STEPS (drums) */
#define MAT_X 16
#define MAT_Y 47
#if !BB_UI_PLAY
static void page_steps(const bb_ui_in *in)
{
    uint16_t c = inst_c(in);
    int32_t v, i, head = in->running && in->step < 16u ? (int32_t)in->step : -1;   /* step >= 16: in another bank */
    char bank[16];
    uint32_t n = 0;
    u_name(8, 26, &AF_M, in->voice_name ? in->voice_name : "", BB_UI_TEXT, BB_UI_BG, 150);
    if (in->banks > 1u) {                                        /* "BANK 1 / 2" */
        const char *t = "BANK ";
        while (*t) bank[n++] = *t++;
        bank[n++] = (char)('1' + in->bank % 9u);
        bank[n++] = ' '; bank[n++] = '/'; bank[n++] = ' ';
        bank[n++] = (char)('0' + in->banks % 10u);
        bank[n] = 0;
        u_text_r(232, 28, &AF_S, bank, BB_UI_GREY, BB_UI_BG);
    }
    for (v = 0; v < 11; v++) {                                   /* the whole pattern, every voice */
        int32_t y = MAT_Y + 7 * v, sel = v == (int32_t)in->voice;
        for (i = 0; i < 16; i++) {
            uint16_t col = BB_UI_FAINT;
            if (in->vacc[v] >> i & 1u)
                col = c;
            else if (in->vsteps[v] >> i & 1u)
                col = c_scale(c, 115);
            if (!sel && col != BB_UI_FAINT)
                col = c_scale(col, 150);
            u_rect(MAT_X + 13 * i, y, 11, 5, col);
        }
        if (sel)
            u_frame(MAT_X - 2, y - 2, 13 * 16 + 1, 9, c_scale(c, 205), 1);
    }
    if (head >= 0)
        u_rect(MAT_X + 13 * head, MAT_Y - 4, 11, 2, BB_UI_TEXT);
    for (i = 0; i < 16; i++) {                                   /* the selected voice = the 16 white keys */
        int32_t x = 8 + 14 * i;
        uint16_t st = (uint16_t)(in->voice < 11u ? in->vsteps[in->voice] : 0u);
        uint16_t ac = (uint16_t)(in->voice < 11u ? in->vacc[in->voice] : 0u);
        if (ac >> i & 1u)
            u_rect(x, 132, 13, 40, c);
        else if (st >> i & 1u)
            u_rect(x, 132, 13, 40, c_scale(c, 102));
        else
            u_frame(x, 132, 13, 40, BB_UI_FAINT, 1);
        if ((st >> i & 1u) && in->ratchet[i] == BB_UI_RATCHET_MIXED)   /* mixed: one bar, unlike the x2-x4 ticks */
            u_rect(x + 2, 167, 8, 2, (ac >> i & 1u) ? BB_UI_BG : BB_UI_TEXT);
        else if ((st >> i & 1u) && in->ratchet[i] > 1u)          /* 2.0 ratchets: one tick per extra strike, x2..x4 */
            for (uint32_t t = 0; t + 1u < in->ratchet[i] && t < 3u; t++)
                u_rect(x + 2 + 3 * (int32_t)t, 166, 2, 4, (ac >> i & 1u) ? BB_UI_BG : BB_UI_TEXT);
        if (i == head)
            u_rect(x, 175, 13, 2, BB_UI_TEXT);
    }
}
#endif

#endif /* BB_UI_ALL_PAGES */

#if BB_UI_HOME
/* RC2 (P4): a section name too wide for its HOME lane (TR-66 "4 Beat / Bass-Snare A", the eras' phrases) is shortened
 * only when it would be cut (the exact width test), and never part-way through a glyph:
 *   1. " / " closes up ("4 Beat/Bass-Snare A"), then a shelf group keeps its first word ("4/Bass-Snare A");
 *   2. the name's mark stays at the end, so neighbouring sections stay apart: a last word that has a digit, is in
 *      capitals or is 1-2 characters ("A", "CY-4", "OHH"), with a short word or number before it ("2 A"); in one
 *      word, its closing digits ("Kick+Snare2");
 *   3. the rest is shortened with one ellipsis, by whole words, then by whole characters ("4/Bass\x85 A"). */
static int section_mark_word(const char *w, uint32_t n)
{
    uint32_t i, caps = 1;
    for (i = 0; i < n; i++) {
        if (w[i] >= '0' && w[i] <= '9') return 1;
        if (w[i] >= 'a' && w[i] <= 'z') caps = 0;
    }
    return n <= 2u || caps;
}
static void section_try(char *d, uint32_t n, const char *t, uint32_t k, const char *mark, int gap)
{
    uint32_t m = 0;
    while (m < k && m + 2u < n) { d[m] = t[m]; m++; }
    d[m++] = ELLIPSIS;
    if (mark && gap && m + 2u < n) d[m++] = ' ';
    while (mark && *mark && m + 1u < n) d[m++] = *mark++;
    d[m] = 0;
}
static void section_fit(char *d, uint32_t n, const char *s, const aafont_t *f, int32_t maxw)
{
    char t[48];
    uint32_t k = 0, j = 0, h, w, p;
    const char *mark = 0;
    int gap = 1;
    for (k = 0; s[k] && k + 1u < n; k++) d[k] = s[k];
    d[k] = 0;
    if (!s[k] && text_w(f, d) <= maxw) return;               /* fits: as written */
    for (k = 0; s[k] && j + 1u < sizeof t;) {                /* " / " -> "/" */
        if (s[k] == ' ' && s[k + 1] == '/' && s[k + 2] == ' ') { t[j++] = '/'; k += 3; continue; }
        t[j++] = s[k++];
    }
    t[j] = 0;
    if (j < n && text_w(f, t) <= maxw) { for (k = 0; k <= j; k++) d[k] = t[k]; return; }
    for (k = 0; k < j && t[k] != '/'; k++) {}                /* the shelf group's first word: "4/Bass-Snare A" */
    if (k < j) {
        for (h = 0; h < k && t[h] != ' '; h++) {}
        if (h < k) { for (w = 0; t[k + w]; w++) t[h + w] = t[k + w]; j = h + w; t[j] = 0; }
        if (j < n && text_w(f, t) <= maxw) { for (k = 0; k <= j; k++) d[k] = t[k]; return; }
    }
    for (h = j; h && t[h - 1u] != ' '; h--) {}               /* h: the last word's start */
    if (h && section_mark_word(t + h, j - h)) {
        for (p = h - 1u; p && t[p - 1u] != ' '; p--) {}      /* a short word or number before a 1-2 letter mark */
        if (p && j - h <= 2u && section_mark_word(t + p, h - 1u - p) && h - 1u - p <= 2u) h = p;
        mark = t + h; h--;
    } else if (!h) {                                          /* one word: its closing digits */
        for (h = j; h && t[h - 1u] >= '0' && t[h - 1u] <= '9'; h--) {}
        if (h && h < j) { mark = t + h; gap = 0; } else h = j;
    } else h = j;
    for (w = h; w-- > 0u;)                                   /* whole words from the end */
        if (t[w] == ' ') {
            for (k = w; k && t[k - 1u] == ' '; k--) {}
            if (k) { section_try(d, n, t, k, mark, gap); if (text_w(f, d) <= maxw) return; }
        }
    for (w = 0; w < h && t[w] != ' '; w++) {}                /* the first word, by characters (then the mark alone) */
    for (k = w + 1u; mark && k-- > 0u;) { section_try(d, n, t, k, mark, gap && k); if (text_w(f, d) <= maxw) return; }   /* to "\x85HH-16" */
    for (k = w; k > 0u; k--) { section_try(d, n, t, k, 0, 0); if (text_w(f, d) <= maxw) return; }
    d[0] = ELLIPSIS; d[1] = 0;
}
/* ---------------------------------------------------------------- PERFORM (RC2, Joe 10:17 / 10:32 / 10:35 / 10:36)
 * "this song, and all three parts change together when you press the keys". The song; the three parts as a slim row
 * (B1 / B2 / B3, their mute keys; MUTED; the focused one outlined); then only as many buttons as the page has sections,
 * mirroring white keys 1-16 (1-4: one row of tall buttons; 5-8: two rows of 4; 9-16: two rows of 8), coloured in the
 * 808's step-key groups (1-4 red, 5-8 orange, 9-12 yellow, 13-16 cream): dark = there, full = playing, bright outline =
 * queued for the bar (and every key of a combination). Each button: its key number upright in the corner and the name
 * centred at the largest size that fits: AF_M, AF_S, on two lines at a space; in the narrow 9-16 buttons the name may
 * stand on its side (AF_S, read bottom to top); then the abbreviation table (pf_abbrev). Never a cut glyph. */
#define PF_PART_Y 49
#define PF_KEYS_Y 70
#define PF_KEYS_H 128
#define PF_STATUS_Y 208
#define PF_LEGEND_Y 224
static const uint16_t PF_GROUP[4] = {0xD9E5u, 0xEC04u, 0xEE47u, 0xE6F9u};   /* 808 step keys: red, orange, yellow, cream */
/* button k of a page with n sections: its box */
static void pf_key_box(uint32_t k, uint32_t n, int32_t *x, int32_t *y, int32_t *w, int32_t *h)
{
    uint32_t per = n <= 8u ? 4u : 8u, rows = n <= 4u ? 1u : 2u, gap = n <= 8u ? 5u : 4u;
    *w = (int32_t)((224u - (per - 1u) * gap) / per);
    *h = (int32_t)((PF_KEYS_H - (rows - 1u) * 5u) / rows);
    *x = 8 + (int32_t)(k % per) * (*w + (int32_t)gap);
    *y = PF_KEYS_Y + (int32_t)(k / per) * (*h + 5);
}
/* a name in a button, upright: one line, or two at the space nearest the middle. 1 = drawn (it fits). */
static int pf_flat(const char *s, const aafont_t *f, int32_t x, int32_t y, int32_t w, int32_t h, uint16_t fg, uint16_t bg, int draw)
{
    char l1[32], l2[32];
    uint32_t n = 0, sp = 0, i, best = 0;
    int32_t lh = (int32_t)f->h - 2;
    while (s[n] && n < 31u) n++;
    if (text_w(f, s) <= w && lh <= h) {
        if (draw) u_text(x + (w - text_w(f, s)) / 2, y + (h - lh) / 2 - 1, f, s, fg, bg);
        return 1;
    }
    for (i = 1; i < n; i++)                                      /* the space nearest the middle */
        if (s[i] == ' ' && (!sp || (i > n / 2u ? i - n / 2u : n / 2u - i) < best)) { sp = i; best = i > n / 2u ? i - n / 2u : n / 2u - i; }
    if (!sp || 2 * lh > h) return 0;
    for (i = 0; i < sp; i++) l1[i] = s[i];
    l1[sp] = 0;
    for (i = sp + 1u; i < n; i++) l2[i - sp - 1u] = s[i];
    l2[n - sp - 1u] = 0;
    if (text_w(f, l1) > w || text_w(f, l2) > w) return 0;
    if (draw) {
        int32_t ty = y + (h - 2 * lh) / 2 - 1;
        u_text(x + (w - text_w(f, l1)) / 2, ty, f, l1, fg, bg);
        u_text(x + (w - text_w(f, l2)) / 2, ty + lh, f, l2, fg, bg);
    }
    return 1;
}
/* AF_S on its side, read bottom to top (a spine label): the glyphs of the face's first phase, rotated in the blit;
 * main loop only (the fault screen's cv_alpha is not touched). x0, y1: the left and bottom of the line. */
static void pf_side(int32_t x0, int32_t y1, const char *s, uint16_t fg, uint16_t bg)
{
    const aafont_t *f = &AF_S;
    const uint16_t *rv = ramp(fg, bg);
    int32_t pen = 0, X0 = 240, Y0 = 240, X1 = 0, Y1 = 0;
    uint32_t prev = 0;
    const char *s0 = s;
    for (; *s; s++) {
        uint32_t c = fold(f, (uint8_t)*s), k = 0, gx, gy;
        const aag_t *g;
        if (prev) pen += kern(f, prev, c);
        g = &f->g[glyph(f, c)];
        for (gy = 0; gy < g->bh; gy++)
            for (gx = 0; gx < g->bw; gx++, k++) {
                uint32_t v = (f->data[g->off + (k >> 1)] >> ((k & 1u) ? 0 : 4)) & 15u;
                int32_t tx = ((pen + 8) >> 4) + g->bx + (int32_t)gx, ty = g->by + (int32_t)gy;
                int32_t X = x0 + ty, Y = y1 - tx;
                if (X < X0) X0 = X;
                if (X + 1 > X1) X1 = X + 1;
                if (Y < Y0) Y0 = Y;
                if (Y + 1 > Y1) Y1 = Y + 1;
                if (v) u_px(X, Y, swap16(rv[v]));      /* the ramp is in panel byte order */
            }
        pen += g->adv;
        prev = c;
    }
    if (GFX_HOOKS && X1 > X0) GFX_HOOK_TEXT(X0 - g_cx, Y0 - g_cy, X1 - g_cx, Y1 - g_cy, s0, 64u);   /* 64: on its side */
    (void)s0;
}
/* the abbreviation levels (deterministic; RC2 r2: read as words). A name is words ("+", "-", "/", a space or a
 * letter-to-digit change end one). 1..n: the table shortens the last 1..n words (Slow Rock -> Slow Rk); n+1..2n: the
 * last 1..n words left out, never the first, and only in a name without a variant mark (a digit or a lone capital:
 * Bossa Nova -> Bossa; "4 Beat / Tango A" keeps every word); 2n+1..3n: both; then every word cut to 8 .. 1 letters
 * (a word with a digit stays whole), last without spaces (but before a lone capital) and hyphens. 0 = no more levels. The first level that fits
 * is drawn. */
static uint32_t pf_dig(char c) { return (uint32_t)(c >= '0' && c <= '9'); }
static uint32_t pf_abbrev(char *d, uint32_t n, const char *s, uint32_t lvl)
{
    static const char *const W[][2] = {
        {"Variation", "Var"}, {"Bass", "B"}, {"Beat", "B"}, {"Fill", "Fl"}, {"Break", "Brk"}, {"Intro", "Int"},
        {"Ending", "End"}, {"Verse", "Vrs"}, {"Chorus", "Chr"}, {"Bridge", "Brg"}, {"Arranger", "Arr"}, {"Shuffle", "Shf"},
        {"Waltz", "Wlz"}, {"Rock'n", "R'n"}, {"Roll", "R"}, {"Foxtrot", "Fox"}, {"Swing", "Swg"}, {"Rock", "Rk"},
        {"Disco", "Dsc"}, {"Tango", "Tng"}, {"March", "Mch"}, {"Hats", "H"}, {"Kick", "K"}, {"Snare", "S"},
        {"Main", "M"}, {"Part", "P"}};
    const char *ws[12];
    uint8_t wl[12], sp[12], dg[12];
    uint32_t nw = 0, m = 0, i, tab, drop, cut = 99u, flat = 0, mark = 0;
    while (*s && nw < 12u) {                                     /* the words and the separator before each */
        uint32_t k = 0;
        sp[nw] = 0;
        if (*s == ' ') { s++; sp[nw] = ' '; }
        if (*s == '/' || *s == '+' || *s == '-') { sp[nw] = (uint8_t)*s++; if (*s == ' ') s++; }
        ws[nw] = s; dg[nw] = 0;
        while (s[k] && s[k] != ' ' && s[k] != '/' && s[k] != '+' && s[k] != '-' && !(k && pf_dig(s[k]) && !pf_dig(s[k - 1])))
            dg[nw] |= (uint8_t)pf_dig(s[k++]);
        mark |= dg[nw] | (uint32_t)(k == 1u && s[0] >= 'A' && s[0] <= 'Z');
        wl[nw++] = (uint8_t)k; s += k;
        if (!k && !*s) break;
    }
    if (lvl > 3u * nw + 9u) return 0;
    tab = lvl <= nw ? lvl : lvl <= 2u * nw ? 0u : nw;
    drop = lvl <= nw || mark ? 0u : lvl <= 3u * nw ? (lvl - 1u) % nw + 1u : 0u;
    if (lvl > 3u * nw) { cut = lvl - 3u * nw < 9u ? 9u - (lvl - 3u * nw) : 1u; flat = lvl == 3u * nw + 9u; }
    for (i = nw; i-- > 1u && drop; drop--) wl[i] |= 0x80u;      /* leave out the last words */
    for (i = 0; i < nw; i++) {
        const char *r = ws[i];
        uint32_t rl = wl[i], j;
        if (rl & 0x80u) continue;
        if (i >= nw - tab)
            for (j = 0; j < sizeof W / sizeof W[0]; j++) {
                uint32_t q = 0;
                while (W[j][0][q] && q < rl && (W[j][0][q] | 32) == (r[q] | 32)) q++;
                if (q == rl && !W[j][0][q]) { r = W[j][1]; for (rl = 0; r[rl]; rl++) {} break; }
            }
        if (!dg[i] && rl > cut) rl = cut;
        if (m && sp[i] && !(flat && (sp[i] == ' ' || sp[i] == '-') && !(wl[i] == 1u && r[0] >= 'A' && r[0] <= 'Z')) && m + 1u < n)
            d[m++] = (char)sp[i];                                /* (a lone capital keeps its space) */
        for (j = 0; j < rl && m + 1u < n; j++) d[m++] = r[j];
    }
    d[m] = 0;
    return 1;
}
/* the name of button (x, y, w, h): upright (one or two lines) in a wide button, on its side in a narrow one (9-16:
 * every name, so a row reads alike); as is, then each abbreviation level */
static void pf_name(const char *s, int32_t x, int32_t y, int32_t w, int32_t h, uint16_t fg, uint16_t bg, int narrow)
{
    char a[32];
    int32_t ty = y + 15, th = h - 17, tw = w - 4;              /* under the key number */
    const char *t = s;
    uint32_t pass = 0;
    do {
        if (!narrow && (pf_flat(t, &AF_M, x + 2, ty, tw, th, fg, bg, 1) || pf_flat(t, &AF_S, x + 2, ty, tw, th, fg, bg, 1))) return;
        if (narrow && text_w(&AF_S, t) <= th - 1 && (int32_t)AF_S.h - 3 <= tw) {
            pf_side(x + (w - (int32_t)AF_S.h + 3) / 2, ty + th - (th - text_w(&AF_S, t)) / 2, t, fg, bg);
            return;
        }
        t = a;
    } while (pf_abbrev(a, sizeof a, s, ++pass));
}
/* RC2 r2: PERFORM's status line (bb_ui_in.perf_status), composed from what the screen shows: the queued button, else the lit button (the focused part's
 * section; none on this page: its lane's) and, when the other parts play another section, theirs */
static int pf_same(const char *a, const char *b) { while (*a && *a == *b) { a++; b++; } return *a == *b; }
void bb_ui_perform_status(char *o, uint32_t cap, const bb_ui_in *in)
{
    uint32_t k, r, f = 0, m = 0, nq = 0, np = 0, qparts = 0;
    const char *p = 0, *oth = 0, *w;
    int vary = 0;
#define PF_PUT(z) for (w = (z); *w && m + 1u < cap; ) o[m++] = *w++
    for (r = 0; r < 3u; r++) {
        if (in->lane[r].flags & BB_UI_LN_FOCUS) f = r;
        if (in->lane[r].flags & BB_UI_LN_NEXT) qparts |= 1u << r;
    }
    for (k = 0; k < 16u && in->tile_state[k] != BB_UI_TILE_NONE && in->tile[k]; k++) {
        if (in->tile_state[k] == BB_UI_TILE_QUEUED) nq++;
        if (in->tile_state[k] == BB_UI_TILE_PLAYING && !np++) p = in->tile[k];
    }
    if (!p) p = in->lane[f].name && in->lane[f].name[0] ? in->lane[f].name : 0;
    for (r = 0; r < 3u && p && !nq; r++) {
        const char *nm = in->lane[r].name;
        if (r == f || !nm || !nm[0] || pf_same(nm, p)) continue;
        if (oth && !pf_same(oth, nm)) vary = 1;
        oth = nm;
    }
    if (nq && qparts) {
        if (qparts == 7u) PF_PUT("ALL > ");
        else {
            if (qparts & 1u) PF_PUT("A");
            if (qparts & 2u) PF_PUT(qparts & 1u ? "+B" : "B");
            if (qparts & 4u) PF_PUT(qparts & 3u ? "+D" : "D");
            PF_PUT(" > ");
        }
    }
    if (oth && !nq) PF_PUT(f == 0u ? "KIT A " : f == 1u ? "KIT B " : "DILLO ");
    else if (p && !nq) PF_PUT(in->running ? "Playing " : "Ready: ");
    if (p || nq)   /* the queued buttons, else the lit ones; a combination reads "Rock 1 + Disco 1" (RC3) */
        for (k = 0, np = 0; k < 16u && in->tile_state[k] != BB_UI_TILE_NONE && in->tile[k]; k++)
            if (in->tile_state[k] == (nq ? BB_UI_TILE_QUEUED : BB_UI_TILE_PLAYING)) { if (np++) PF_PUT(" + "); PF_PUT(in->tile[k]); }
    if (p && !nq && !np) PF_PUT(p);
    if (nq) PF_PUT(in->running ? " NEXT \xB7 AGAIN CANCELS" : " NEXT");
    else if (oth) { PF_PUT(" \xB7 others "); PF_PUT(vary ? "vary" : oth); }
    else if (p && !in->running) PF_PUT(" \xB7 PLAY starts");
#undef PF_PUT
    o[m] = 0;
}
static void page_perform(const bb_ui_in *in, uint32_t now)
{
    static const char *const mix_name[3] = {"K1 KIT A", "K2 KIT B", "K3 DILLO"};
    static const char *const mix_muted[3] = {"K1 OFF", "K2 OFF", "K3 OFF"};
    uint32_t r, k, n = 0;
    char kb[4];
    u_brand(8, 30, NM_NOW, in->perf_song ? in->perf_song : "", BB_UI_TEXT, BB_UI_BG, 224, 0);
    for (r = 0; r < 3u; r++) {                                   /* the parts: black B1-B3 */
        const bb_ui_lane *l = &in->lane[r];
        int32_t x = 8 + 76 * (int32_t)r;
        uint16_t pc = INST_C[r], fo = (l->flags & BB_UI_LN_FOCUS) != 0, mu = (l->flags & BB_UI_LN_MUTED) != 0,
                 nx = (l->flags & BB_UI_LN_NEXT) != 0;
        uint16_t bg = fo ? c_scale(pc, 56) : 0x10A2u;
        u_rbox(x, PF_PART_Y, 72, 18, 3, fo ? pc : 0x3186u, BB_UI_BG);
        u_rbox(x + 1, PF_PART_Y + 1, 70, 16, 2, bg, fo ? pc : 0x3186u);
        if (nx) u_frame(x, PF_PART_Y, 72, 18, breath_c(pc, now), 2);
        u_text(x + 5, PF_PART_Y + 1, &AF_S, mu ? mix_muted[r] : mix_name[r], mu ? BB_UI_GREY : pc, bg);
        kb[0] = 'B'; kb[1] = (char)('1' + r); kb[2] = 0;
        u_text_r(x + 67, PF_PART_Y + 1, &AF_S, kb, BB_UI_GREY, bg);
    }
    while (n < 16u && in->tile_state[n] != BB_UI_TILE_NONE && in->tile[n]) n++;   /* buttons = this page's sections */
    for (k = 0; k < n; k++) {
        int32_t x, y, w, h;
        uint32_t st = in->tile_state[k], num = 16u * in->tile_page + k + 1u;
        uint16_t c = PF_GROUP[k / 4u], fill = st == BB_UI_TILE_PLAYING ? c : c_scale(c, 72), ink = st == BB_UI_TILE_PLAYING ? BB_UI_BG : BB_UI_TEXT;
        pf_key_box(k, n, &x, &y, &w, &h);
        u_rbox(x, y, w, h, 3, fill, BB_UI_BG);
        if (st == BB_UI_TILE_QUEUED) {                           /* queued for the bar: a bright outline, pulsing */
            u_frame(x, y, w, h, breath_c(c, now), 2);
        }
        kb[0] = (char)(num >= 10u ? '0' + num / 10u : '0' + num); kb[1] = (char)(num >= 10u ? '0' + num % 10u : 0); kb[2] = 0;
        u_text(x + 3, y + 1, &AF_S, kb, ink, fill);
        pf_name(in->tile[k], x, y, w, h, ink, fill, n > 8u);
    }
    u_rect(8, PF_STATUS_Y - 2, 224, 1, BB_UI_FAINT);
    u_name(8, PF_STATUS_Y, &AF_S, in->notice ? in->notice : in->perf_status ? in->perf_status : "", inst_c(in), BB_UI_BG, 224);
    u_name(8, PF_LEGEND_Y, &AF_S, in->footer ? in->footer : "", BB_UI_GREY, BB_UI_BG, 224);
}
#endif

/* ---------------------------------------------------------------- PLAY Dillo */
#define ROLL_Y0 62
#define ROLL_Y1 128
static int32_t note_y(int32_t n)
{
    if (n > 12) n = 12;
    if (n < 0) n = 0;
    return 122 - 5 * n;
}
/* resonant 2-pole low-pass magnitude at frequency f (0..255) for cutoff fc, Q: 1..? (x16) -> gain x256 */
static uint32_t lp_gain(uint32_t f, uint32_t fc, uint32_t q16)
{
    uint64_t x = (uint64_t)f * 256u / fc;                       /* f / fc, Q8 */
    int64_t a = 65536 - (int64_t)(x * x);                      /* 1 - x^2, Q16 */
    uint64_t b = x * 256u * 16u / q16;                         /* x / q, Q16 */
    uint64_t d = (uint64_t)(a < 0 ? -a : a) * (uint64_t)(a < 0 ? -a : a) + b * b;   /* Q32 */
    uint64_t r = d >> 16;                                       /* Q16 */
    uint32_t s = u_isqrt(r > 0xFFFFFFFFu ? 0xFFFFFFFFu : (uint32_t)r);   /* Q8 */
    return s ? (uint32_t)(65536u / s) : 820u;                  /* x256 */
}
#if !BB_UI_PLAY
static void page_dillo(const bb_ui *u, const bb_ui_in *in)
{
    uint16_t c = BB_UI_DILLO;
    int32_t s, head = in->running ? (int32_t)in->step : -1, px = 0, py = 0;
    uint32_t fc = 26u + (uint32_t)in->cutoff * 204u / 255u, q16 = 12u + (uint32_t)in->reso * 84u / 255u;
    char oct[8] = "OCT 0";
    u_brand(8, NOW_CAP_Y, NM_NOW, in->patch, BB_UI_TEXT, BB_UI_BG, MINI34_X - 8 - 6, 0);
    u_text_fit(8, 44, &AF_S, in->scale ? in->scale : "", BB_UI_GREY, BB_UI_BG, MINI34_X - 8 - 6);
    if (!BB_UI_BACKDROP) draw_mini(u, in, MINI34_X, MINI34_Y, 34, &MINI34_BOX);
    for (s = 0; s < 16; s++) {                                   /* the line: pitch up, accents bright, slides joined */
        int32_t x = 8 + 14 * s, n = in->note[s];
        u_rect(x, ROLL_Y0, 13, ROLL_Y1 - ROLL_Y0, c_scale(c, (s & 3) ? 14 : 24));
        if (n >= 0) {
            int32_t y = note_y(n), acc = in->note_acc >> s & 1u;
            uint16_t col = acc ? c : c_scale(c, 140);
            u_rect(x + 1, y, 11, 5, col);
            if (acc)
                u_rect(x + 1, y - 3, 11, 2, BB_UI_TEXT);
            if ((in->note_slide >> s & 1u) && s < 15 && in->note[s + 1] >= 0)
                u_line(x + 11, y + 2, x + 15, note_y(in->note[s + 1]) + 2, col, 2);
        }
        if (s == head)
            u_rect(x, ROLL_Y1 + 3, 13, 2, BB_UI_TEXT);
    }
    u_text(8, 140, &AF_S, "FILTER", BB_UI_GREY, BB_UI_BG);
    oct[4] = (char)(in->octave < 0 ? '-' : '0' + (in->octave > 9 ? 9 : in->octave));
    if (in->octave < 0) { oct[5] = (char)('0' - in->octave); oct[6] = 0; }
    u_text(8, 158, &AF_S, oct, BB_UI_GREY, BB_UI_BG);
    for (s = 0; s <= 100; s += 2) {                              /* live filter curve: CUTOFF / RESO */
        uint32_t g = lp_gain((uint32_t)s * 255u / 100u, fc, q16);
        int32_t x = 128 + s, y;
        if (g > 820u) g = 820u;                                  /* 3.2 x */
        y = 176 - (int32_t)(g * 11u / 256u);
        if (s)
            u_line(px, py, x, y, c, 2);
        px = x;
        py = y;
    }
}
#endif

/* the queue ring: fills towards the next bar while a kit / pattern waits (audit §7) */
static uint32_t pseudo_angle(int32_t dx, int32_t dy)        /* 0..1023 clockwise from 12 o'clock */
{
    int32_t ax = dx < 0 ? -dx : dx, ay = dy < 0 ? -dy : dy;
    uint32_t t = ax + ay ? (uint32_t)(ax * 256 / (ax + ay)) : 0u;   /* 0..256 within the quadrant */
    if (dx >= 0 && dy < 0) return t;
    if (dx >= 0) return 512u - t;
    if (dy >= 0) return 512u + t;
    return 1024u - t;
}
static void draw_ring(int32_t cx, int32_t cy, uint32_t progress, uint16_t c)
{
    int32_t x, y;
    uint32_t lim = progress * 1024u / 255u;
    if (!u_vis(cx - 14, cy - 14, 29, 29))
        return;
    for (y = -14; y <= 14; y++)
        for (x = -14; x <= 14; x++) {
            int32_t r2 = x * x + y * y;
            if (r2 >= 11 * 11 && r2 <= 14 * 14)
                u_px(cx + x, cy + y, pseudo_angle(x, y) < lim ? c : BB_UI_FAINT);
        }
}

/* ---------------------------------------------------------------- choosers (RC2, Joe 10:09)
 * KIT, PATTERN and HOME/SONGS (ERAS, ACID): "the entire screen for the songs". Below the status row the list only:
 * up to ten names in the list type, the cursor row highlighted, a mark on the playing entry, each row's white key at
 * the right, and one footer line (or the current notice). Turning a knob shows a band (name, value, bar) over the last
 * rows for BB_UI_POPUP_MS. The list scrolls to keep the cursor in view. No artwork behind the names. */
#define CH_Y0 27
#define CH_ROW 19
#define CH_BAND_Y 180
#define CH_FOOT_Y 223
static int chooser(uint32_t page) { return page == BB_UI_KIT || page == BB_UI_PATTERN; }
static int full_page(uint32_t page) { return chooser(page) || page == BB_UI_STUDIO; }   /* no knob row / key strip */
static uint32_t list_rows(const bb_ui_in *in, uint32_t pk) { (void)in; return pk != BB_UI_NO ? 8u : 10u; }
static uint32_t list_first(const bb_ui_in *in, uint32_t pk)
{
    uint32_t rows = list_rows(in, pk);
    return in->cursor < in->items && in->cursor >= rows ? in->cursor + 1u - rows : 0u;
}
static uint32_t list_queued(const bb_ui_in *in)
{
    return (in->queued & (in->page == BB_UI_PATTERN ? BB_UI_Q_PATTERN : BB_UI_Q_KIT)) != 0;
}
static void ch_row(const bb_ui_in *in, uint32_t i, uint32_t k, uint32_t now)
{
    uint16_t c = inst_c(in);
    uint32_t cur = k == in->cursor, q = list_queued(in) && k == in->queued_item;
    int32_t ry = CH_Y0 + CH_ROW * (int32_t)i;
    uint16_t bg = cur ? c_scale(c, 56) : BB_UI_BG;
    uint16_t fg = q ? breath_c(c, now) : cur ? c : k == in->playing_item ? BB_UI_TEXT : BB_UI_GREY;
    if (cur)
        u_rect(0, ry, 240, CH_ROW, bg);
    if (k == in->playing_item)
        u_rect(8, ry + 6, 3, 7, c);
    u_brand(16, ry + 4, NM_LIST, in->item[k], fg, bg, 190, 0);
    if (in->item_key[k]) {
        char kb[4]; uint32_t n = in->item_key[k];
        kb[0] = (char)(n >= 10u ? '1' : '0' + n); kb[1] = (char)(n >= 10u ? '0' + n - 10u : 0); kb[2] = 0;
        u_text_r(232, ry + 2, &AF_S, kb, BB_UI_GREY, bg);
    }
}
/* Welcome uses the same cursor as SONGS; selected never implies loaded. */
/* Joe's approved RC mark: a mirrored R shares its spine with C.  It is drawn
 * from the normal UI primitives so the 48 px screen asset stays deterministic,
 * costs no bitmap, and survives the same strip redraws as the rest of the card.
 * The written CONDUCTOR title remains the primary orientation label. */
static void draw_rc_mark(int32_t x, int32_t y)
{
    const uint16_t w = BB_UI_TEXT, orange = BB_UI_KIT_A, pink = BB_UI_DILLO;
    /* mirrored R */
    u_rect(x + 18, y + 5, 6, 29, w);                    /* shared upright */
    u_rect(x + 4, y + 5, 18, 5, w);                    /* bowl top */
    u_rect(x + 2, y + 9, 5, 11, w);                    /* bowl outside */
    u_rect(x + 5, y + 18, 15, 5, w);                   /* bowl waist */
    u_line(x + 19, y + 21, x + 8, y + 34, w, 5);       /* mirrored leg */
    /* C, saddled against the same upright */
    u_rect(x + 24, y + 10, 21, 5, w);
    u_rect(x + 24, y + 10, 5, 22, w);
    u_rect(x + 24, y + 27, 21, 5, w);
    /* the third approved variant's hardware-orange terminals and pink spark */
    u_line(x + 23, y + 8, x + 29, y + 2, orange, 5);
    u_line(x + 28, y + 4, x + 35, y + 4, orange, 5);
    u_line(x + 36, y + 4, x + 42, y + 1, pink, 5);
    u_line(x + 24, y + 34, x + 31, y + 29, orange, 5);
    u_rect(x + 29, y + 29, 12, 5, orange);
}
static void page_welcome(const bb_ui *u, const bb_ui_in *in, uint32_t now)
{
    unsigned c = in->cursor;
    u_text(8, 39, &AF_S, "THREE PARTS. YOUR MUSIC.", BB_UI_GREY, BB_UI_BG);
    if (c < in->items && c < BB_UI_LIST) {
        if(c)u_name(8, 66, &AF_S, in->item[c-1], BB_UI_GREY, BB_UI_BG, 224);
        u_brand(8, 101, NM_NOW, in->item[c], BB_UI_TEXT, BB_UI_BG, 224, 0);
        if(c+1u < in->items)u_name(8, 128, &AF_S, in->item[c+1], BB_UI_GREY, BB_UI_BG, 224);
    }
    char sections[16]; unsigned n=in->sections, at=0;
    if(n>=100u)sections[at++]=(char)('0'+n/100u);
    if(n>=10u)sections[at++]=(char)('0'+n/10u%10u);
    sections[at++]=(char)('0'+n%10u);
    const char *suffix=n==1u?" SECTION":" SECTIONS";
    while(*suffix)sections[at++]=*suffix++;
    sections[at]=0;
    u_text(8, 148, &AF_S, sections, BB_UI_GREY, BB_UI_BG);
    u_text(8, 164, &AF_S, "ALGO: NEXT SONG", BB_UI_GREY, BB_UI_BG);
    draw_rc_mark(186, 137);
    u_text(8, 180, &AF_M, "PRESS PLAY", breath_c(inst_c(in), now), BB_UI_BG);
    u_text(8, 202, &AF_S, "EDIT OR WHITE KEY: SONG LIST", BB_UI_GREY, BB_UI_BG);
    u_name(8, CH_FOOT_Y, &AF_S, in->notice ? in->notice : in->footer ? in->footer : "",
           in->notice ? inst_c(in) : BB_UI_GREY, BB_UI_BG, 224);
}
static void page_chooser(const bb_ui *u, const bb_ui_in *in, uint32_t now, uint32_t pk)
{
    if(in->welcome){page_welcome(u,in,now);return;}
    uint32_t i, rows = list_rows(in, pk), first = list_first(in, pk);
    for (i = 0; i < rows && first + i < in->items && first + i < BB_UI_LIST; i++)
        ch_row(in, i, first + i, now);
    if (pk == BB_UI_NO)
        u_name(8, CH_FOOT_Y, &AF_S, in->notice ? in->notice : in->footer ? in->footer : "", in->notice ? inst_c(in) : BB_UI_GREY,
               BB_UI_BG, 224);
}
/* a turned knob on a full page: one band (name, value, bar) over the bottom, for BB_UI_POPUP_MS */
static void draw_band(const bb_ui_in *in, uint32_t k)
{
    const bb_ui_knob *kn = &in->knob[k];
    uint16_t c = inst_c(in), panel = 0x18C3u;
    const char *vt = kn->flags & BB_UI_KN_MUTE ? "MUTE" : kn->text ? kn->text : "";
    char lb[12];
    u_rect(0, CH_BAND_Y, 240, 60, panel);
    u_rect(0, CH_BAND_Y, 240, 1, c);
    u_text_fit(8, CH_BAND_Y + 5, &AF_S, short_label(kn->label ? kn->label : "", lb, sizeof lb, 120), c, panel, 120);
    u_text_fit(8, CH_BAND_Y + 22, &AF_M, vt, kn->flags & BB_UI_KN_MUTE ? BB_UI_RED : BB_UI_TEXT, panel, 100);
    u_rect(112, CH_BAND_Y + 32, 120, 5, BB_UI_FAINT);
    u_rect(112, CH_BAND_Y + 32, (int32_t)kn->bar * 120 / 255, 5, c);
}

#if BB_UI_ALL_PAGES
/* ---------------------------------------------------------------- FX: the echo machine */
#if !BB_UI_PLAY
static void stadium(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint16_t c)
{
    int32_t r = (y1 - y0) / 2, cy = y0 + r, x, y;
    u_rect(x0 + r, y0, x1 - x0 - 2 * r, 1, c);
    u_rect(x0 + r, y1, x1 - x0 - 2 * r, 1, c);
    if (!u_vis(x0, y0, x1 - x0 + 1, y1 - y0 + 1))
        return;
    for (y = -r; y <= r; y++)
        for (x = -r; x <= 0; x++) {
            int32_t d = x * x + y * y;
            if (d <= r * r && d > (r - 1) * (r - 1)) {
                u_px(x0 + r + x, cy + y, c);
                u_px(x1 - r - x, cy + y, c);
            }
        }
}
#if !BB_UI_PLAY
#endif
static uint32_t echo_phase(const bb_ui_in *in, uint32_t now)   /* 0..7: dots travel at echo time */
{
    uint32_t t = in->echo_ms >= 20u ? in->echo_ms : 20u;
    return in->running ? (now % t) * 8u / t : 0u;
}
#endif
/* 2.1 P1: the tabs (FX: ECHO .. SENDS on white 1-5; EVOLVE: its modes), the shown one bright and underlined; returns
 * the end x. Each tab shows its name's first word (the five FX names in full need 245 px of 224: "TAPE DRIVE" reads
 * TAPE here and in full in the status bar, the legend and HELP). */
static int32_t draw_tabs(const bb_ui_in *in, int32_t x, int32_t y)
{
    uint32_t i, n;
    char w1[12];
    for (i = 0; in->tab && i < in->tabs; i++) {
        const char *t = in->tab[i] ? in->tab[i] : "";
        for (n = 0; t[n] && t[n] != ' ' && n + 1u < sizeof w1; n++) w1[n] = t[n];
        w1[n] = 0;
        if (i == in->tab_cur)
            u_rect(x - 1, y + 12, text_w(&AF_S, w1) + 2, 1, inst_c(in));
        x = u_text(x, y, &AF_S, w1, i == in->tab_cur ? BB_UI_TEXT : BB_UI_GREY, BB_UI_BG) + 7;
    }
    return x;
}
#if !BB_UI_PLAY
static void page_fx(const bb_ui_in *in, uint32_t now)
{
    uint16_t c = inst_c(in);
    int32_t i, n = 1 + (int32_t)in->repeats * 5 / 255;
    uint32_t ph = echo_phase(in, now), k = 256;
    stadium(24, 44, 216, 148, BB_UI_GREY);
    for (i = 0; i < n; i++) {                                    /* the repeats, fading */
        int32_t d = 12 - 2 * i, x = 52 + 24 * i + (int32_t)ph * 3;
        u_rbox(x - d / 2, 96 - d / 2, d, d, d / 2, c_scale(c, k), BB_UI_BG);
        k = k * (96u + (uint32_t)in->repeats * 150u / 255u) / 256u;
    }
    for (i = 0; i < 3; i++) {                                    /* the three heads */
        int32_t x = 60 + 50 * i;
        char lab[2];
        u_rect(x, 140, 16, 16, (in->heads >> i & 1u) ? c : 0x2945u);
        lab[0] = (char)('1' + i);
        lab[1] = 0;
        u_text(x + 5, 160, &AF_S, lab, (in->heads >> i & 1u) ? BB_UI_TEXT : BB_UI_GREY, BB_UI_BG);
    }
    (void)draw_tabs(in, 8, 28);                                  /* 2.1 P1: the tabs (white 1-5) replace the bank dots */
    if (in->tab_note)                                            /* inside the echo frame, under the repeats */
        u_text_fit(120 - text_w(&AF_S, in->tab_note) / 2, 100, &AF_S, in->tab_note, BB_UI_GREY, BB_UI_BG, 180);
}
#endif

/* ---------------------------------------------------------------- KNOBS (SOUND, SAVE, EVOLVE, GLOBAL)
 * The four values as four meters standing on their own knob columns; the knob bar
 * below carries the labels and numbers. */
#define METER_Y0 56
#define METER_Y1 176
static void page_knobs(const bb_ui_in *in)
{
    uint16_t c = inst_c(in);
    uint32_t k;
    if (in->tabs && !in->title) {                                /* 2.1 P1 FX SPRING / SENDS: the tabs are the title */
        (void)draw_tabs(in, 8, 28);
    } else if (in->tabs) {                                       /* EVOLVE: the mode as the title, the modes as tabs */
        if (in->title)
            u_name(8, 26, &AF_M, in->title, BB_UI_TEXT, BB_UI_BG, 96);
        if (in->subtitle)
            u_text_r(232, 28, &AF_S, in->subtitle, BB_UI_GREY, BB_UI_BG);
        {
            int32_t x = draw_tabs(in, 8, 42);
            if (in->tab_note)
                u_text_fit(x + 4, 42, &AF_S, in->tab_note, BB_UI_GREY, BB_UI_BG, 232 - x - 4);
        }
    }
    if (in->title && !in->tabs)
        u_name(8, 28, &AF_M, in->title, BB_UI_TEXT, BB_UI_BG, 224);
    if (in->subtitle && !in->tabs)                               /* what EDIT does here, in grey */
        u_name(8, 44, &AF_S, in->subtitle, BB_UI_GREY, BB_UI_BG, 224);
    else if (in->subtitle && !in->title)
        u_name(8, 44, &AF_S, in->subtitle, BB_UI_GREY, BB_UI_BG, 224);
    for (k = 0; k < 4u; k++) {
        const bb_ui_knob *kn = &in->knob[k];
        int32_t x = KNOB_X(k), hgt = (int32_t)kn->bar * (METER_Y1 - METER_Y0) / 255;
        uint32_t mute = kn->flags & BB_UI_KN_MUTE;
        if ((kn->flags & BB_UI_KN_OFF) || !kn->label)
            continue;
        u_frame(x, METER_Y0, 50, METER_Y1 - METER_Y0, BB_UI_FAINT, 1);
        u_rect(x, METER_Y1 - hgt, 50, hgt, mute ? c_scale(c, 40) : c_scale(c, 90));
        u_rect(x, METER_Y1 - hgt, 50, 2, mute ? BB_UI_RED : c);
    }
}

/* ---------------------------------------------------------------- ATTRACT (30 s idle on STUDIO)
 * the only place the Armadillo takes the whole screen (v3 §3): the full engraving in the instrument
 * colour, the rays flashing on the beat and reaching with the level (v2 additions), the installed
 * tower rings and sparks at full size; stopped: the full-size ball with BIG BEAT (the same ordered
 * re-ink as the mini). */
#define ATTRACT_TOP 96          /* rays + rings region */
static uint32_t base_bit(int32_t x, int32_t y)
{
    if ((uint32_t)x >= 240u || (uint32_t)y >= 240u)
        return 0;
    return (bb_armadillo[(uint32_t)y * 30u + ((uint32_t)x >> 3)] >> ((uint32_t)x & 7u)) & 1u;
}
#if BB_UI_ATTRACT_BALL
static uint32_t bits1(const uint8_t *d, int32_t x, int32_t y, int32_t x0, int32_t y0, int32_t w, int32_t h)
{
    uint32_t dx = (uint32_t)(x - x0), dy = (uint32_t)(y - y0);
    if (dx >= (uint32_t)w || dy >= (uint32_t)h)
        return 0;
    return (d[dy * (((uint32_t)w + 7u) >> 3) + (dx >> 3)] >> (dx & 7u)) & 1u;
}
#endif
/* The engraving's top band (y < 74) holds both the radio tower and the rays. The tower is the lattice
 * between its two legs, from the tip (x 116..128 to y 20) out to x ~89 / ~151 at y 74; the rays are the
 * double lines outside them. Only the rays reach with the level: the tower is always drawn whole, as on
 * the installed face (Joe, #4 6465654550: the tower's lower part was missing at ordinary levels). */
static int attract_tower(int32_t x, int32_t y)
{
    int32_t d = y - 20;
    if (y < 8 || y >= 74)
        return 0;
    if (y < 20)
        return x >= 116 && x <= 128;
    return x >= 116 - d * 3 / 5 - 2 && x <= 127 + d * 11 / 20 + 2;
}
static uint32_t attract_beat(const bb_ui *u, const bb_ui_in *in)   /* quarter of the beat 0..3, 4 = none */
{
    uint32_t age = beat_age(u, in->now_ms), per = beat_ms(in);
    return in->running && age < per ? age * 4u / per : 4u;
}
static void page_attract(const bb_ui *u, const bb_ui_in *in)
{
    uint16_t c = in->fault ? BB_UI_RED : inst_c(in), body = c_scale(c, 205), ray_lo = c_scale(c, 64);
    uint32_t q = attract_beat(u, in), beat = q < 4u ? 64u * (4u - q) : 0u;
    uint32_t reach = 18u + (uint32_t)(in->level >> 5) * 64u / 7u, reach2 = reach * reach;
    int32_t x, y, x0 = g_cx < 0 ? 0 : g_cx, x1 = g_cx + g_cw > 240 ? 240 : g_cx + g_cw;
    int32_t y0 = g_cy < 0 ? 0 : g_cy, y1 = g_cy + g_ch > 240 ? 240 : g_cy + g_ch;
    clip_t all = {0, 0, 240, 240};
    for (y = y0; y < y1; y++) {
        uint16_t *row = cv_px + (uint32_t)(y - g_cy) * cv_w;
        for (x = x0; x < x1; x++) {
            uint32_t v = base_bit(x, y) ? 2u : 0u, bay = BAYER4[y & 3][x & 3];
            uint16_t ink;
#if BB_UI_ATTRACT_BALL
            if (bay < ROLL_THR[u->roll]) {                       /* re-inked into the ball */
                v = bits1(BB_BALL_BITS, x, y, BB_BALL_X, BB_BALL_Y, BB_BALL_W, BB_BALL_H) ? 2u : 0u;
                if (bits1(BB_BLET_BITS, x, y, BB_BLET_X, BB_BLET_Y, BB_BLET_W, BB_BLET_H))
                    v = bay < RLET_THR[u->roll] ? 3u : 0u;
            }
#else
            (void)bay;
#endif
            if (!v)
                continue;
            if (v == 3u)
                ink = in->fault ? c_scale(BB_UI_RED, 230) : BB_UI_TEXT;
            else if (y < 74 && !(BB_UI_ATTRACT_BALL && bay < ROLL_THR[u->roll]) && !attract_tower(x, y)) {   /* the rays */
                uint32_t dx = (uint32_t)(x > 120 ? x - 120 : 120 - x), dy = (uint32_t)(y < 5 ? 5 - y : y - 5);
                uint32_t r2 = dx * dx + dy * dy, r;
                if (r2 > reach2)
                    continue;
                r = u_isqrt(r2);
                ink = c_mix(ray_lo, BB_UI_TEXT, r < 90u ? beat * (90u - r) / 90u : 0u);
            } else {
                ink = (BB_UI_ATTRACT_BALL && bay < ROLL_THR[u->roll]) ? c : body;
            }
            row[x - g_cx] = swap16(ink);
        }
    }
#if !BB_UI_ATTRACT_BALL
    if (u->roll) {
        clip_t none = {0, 0, 0, 0};
        draw_mini(u, in, 97, 180, 46, &none);
    }
#endif
    if (in->running && !in->fault && !u->roll)
        rings_draw(&u->rings, &all, 120, 12, 240, mini_clock(in->now_ms));
}

#endif /* BB_UI_ALL_PAGES */

#if BB_UI_HELP
/* ---------------------------------------------------------------- HELP (hold HOME)
 * 2.1 P1: the rows are generated by the firmware from navigation's one role table (bb_nav_help): the keys, buttons,
 * K1-K4 and encoders that act on the page under the overlay, the same table that lights the keys and buttons.
 * Nine rows a page (OCT-/+ inside HELP turn the page); the control pressed while HOME is held is picked out and
 * said in one line at the bottom. A missing or stale text block shows HELP UNAVAILABLE, never a wrong text. */
static const char *const PAGE_WORD[BB_UI_PAGES] = {"STUDIO", "PLAY", "STEPS", "PLAY", "KIT", "PATTERN", "FX", "", "", ""};
#define HELP_Y0 28
#define HELP_DY 17
static void page_help(const bb_ui_in *in, uint32_t page)
{
    int32_t y = HELP_Y0, x;
    char t[32] = "HELP \xB7 ";
    uint32_t n = 7, i;
    const char *w = in->help_word ? in->help_word :            /* RC1: SONGS / PATCH / BUILD title their own page */
                    page < BB_UI_PAGES && PAGE_WORD[page][0] ? PAGE_WORD[page] : in->page_name ? in->page_name : "";
    for (i = 0; w[i] && n < 22u; i++)
        t[n++] = w[i];
    t[n] = 0;
    u_text(8, STATUS_TXT_Y, &AF_S, t, BB_UI_TEXT, BB_UI_BG);
    if (in->help_pages > 1u) {                                   /* "2/3": OCT turns the page */
        char pg[8] = {(char)('1' + in->help_page % 9u), '/', (char)('0' + in->help_pages % 10u), 0};
        u_text_r(232, STATUS_TXT_Y, &AF_S, pg, BB_UI_GREY, BB_UI_BG);
    }
    u_rect(8, 23, 224, 1, BB_UI_FAINT);
    if (!bb_tx_ready())                                          /* block missing or stale: say so, rows need it */
        u_text(8, y - 1, &AF_S, BB_TX_FALLBACK, BB_UI_GREY, BB_UI_BG);
    else
        for (i = 0; in->help_row && i < in->help_rows && i < BB_UI_HELP_ROWS; i++, y += HELP_DY) {
            const bb_ui_help_row *r = &in->help_row[i];
            const uint16_t bg = i == in->help_sel ? 0x2945u : BB_UI_BG, ink = i == in->help_sel ? BB_UI_TEXT : BB_UI_GREY;
            if (!u_vis(0, y - 2, 240, HELP_DY))
                continue;
            if (bg != BB_UI_BG)
                u_rect(4, y - 2, 232, HELP_DY - 1, bg);
            x = 8;
            if (r->rec)
                x = cv_keycap(x - g_cx, y - g_cy, KC_REC, BB_UI_TEXT, bg, bg) + g_cx + 3;
            if (r->kc != BB_UI_NO)
                x = cv_keycap(x - g_cx, y - g_cy, r->kc, BB_UI_TEXT, bg, bg) + g_cx + 3;
            if (r->kc2 != BB_UI_NO)
                x = cv_keycap(x - g_cx, y - g_cy, r->kc2, BB_UI_TEXT, bg, bg) + g_cx + 3;
            if (r->keys && r->keys[0] && r->kc == BB_UI_NO)            /* a key range: "1-11", "B1-4" */
                x = u_text(x, y - 1, &AF_S, r->keys, BB_UI_TEXT, bg) + 3;
            if (r->kc == KC_K14 && r->keys)                            /* K1-K4: their labels are the text */
                u_text_fit(x + 2, y - 1, &AF_S, r->keys, ink, bg, 232 - x - 2);
            else
                u_text_fit(x < 76 ? 76 : x + 2, y - 1, &AF_S, r->text ? r->text : "", ink, bg, 232 - (x < 76 ? 76 : x + 2));
        }
    if (in->help_note)                                           /* the control pressed while HOME is held */
        u_text_fit(8, 182, &AF_S, in->help_note, BB_UI_TEXT, BB_UI_BG, 224);
    u_text(8, 212, &AF_S, "Let go of HOME to return", BB_UI_GREY, BB_UI_BG);
}

#endif

/* ---------------------------------------------------------------- the whole page */
static uint32_t eff_page(const bb_ui *u, const bb_ui_in *in)
{
    if (in->help && BB_UI_HELP)
        return BB_UI_PAGES;                                      /* (help) */
#if BB_UI_ALL_PAGES
    if (in->page == BB_UI_STUDIO && !in->fault && in->now_ms - u->idle_t >= BB_UI_ATTRACT_MS)
        return BB_UI_ATTRACT;
#else
    (void)u;
#endif
    return in->page < BB_UI_PAGES ? in->page : BB_UI_STUDIO;
}
/* 2.0 D4: one shared feedback timer (pop_t / pop_k), so no new state: pop_k 0..3 = that knob's pop-up for
 * BB_UI_POPUP_MS, BB_UI_POP_LEGEND = the key legend for BB_UI_LEGEND_MS; the latest event wins */
static uint32_t pop_on(const bb_ui *u, const bb_ui_in *in)
{
    return u->pop_t && u->pop_k < 4u && in->now_ms - (u->pop_t & ~1u) < BB_UI_POPUP_MS ? u->pop_k : BB_UI_NO;
}
/* the legend shows over the key band (the key map, or PLAY's OUT / DSP row) for about 2 s after BB_EV_LEGEND; HOME
 * leaves the band to the quick start and the Dillo NEXT line, which reach into it */
static uint32_t pl_owns(uint32_t page);
static uint32_t legend_on(const bb_ui *u, const bb_ui_in *in, uint32_t page)
{
    if (pl_owns(page))                                           /* 2.2: the playing pages label their keys themselves */
        return 0;
    if (!u->pop_t || u->pop_k != BB_UI_POP_LEGEND || in->now_ms - (u->pop_t & ~1u) >= BB_UI_LEGEND_MS ||
        !in->legend || !in->legend[0] || page >= BB_UI_ATTRACT)
        return 0;
#if BB_UI_HOME
    if (page == BB_UI_STUDIO && (in->quick_start || (in->lane[2].next && in->lane[2].next[0])))
        return 0;
#endif
    return 1;
}
/* 2.2: the playing pages drawn by bb_ui_play.c: they draw their own keys, with no artwork behind them */
static uint32_t pl_owns(uint32_t page)
{
    return BB_UI_PLAY && (page == BB_UI_PLAY_DRUMS || page == BB_UI_STEPS || page == BB_UI_PLAY_DILLO || page == BB_UI_FX);
}
static uint32_t has_keys(uint32_t page) { return page != BB_UI_PLAY_DRUMS && !pl_owns(page); }   /* v3 PLAY: lanes + master */
#if BB_UI_ALL_PAGES
/* ---------------------------------------------------------------- the Armadillo backdrop
 * The engraving (and, stopped, the BIG BEAT ball) drawn large behind every normal page, under all
 * content, instead of the small corner mini. BB_UI_BACKDROP 1: layout C (128 px, lower right, dim,
 * coloured per page so it stays background); 2: layout A (104 px, top-right corner). Area-sampled
 * from the 240 px art already in flash: no new art. It redraws only when page, instrument, colour or
 * the roll step change, never per frame: the beat stays on the status-bar dots and the play mark. */
#if BB_UI_BACKDROP == 2
#define BD_X 134
#define BD_Y 26
#define BD_N 104
#else
#define BD_X 120
#define BD_Y 70
#define BD_N 128
#endif
#define BD_Y1 198               /* stage only: the knob bar stays clean */
#define BB_UI_BD_COOL 0x3A0Cu   /* a cool slate under content drawn in the instrument colour */
static int full_page(uint32_t page);
static int backdrop_on(uint32_t page)      /* RC2: never behind a full page; 2.2: nor behind the playing pages */
{
    return BB_UI_BACKDROP && page != BB_UI_ATTRACT && page != BB_UI_PAGES && !full_page(page) && !pl_owns(page);
}
static uint16_t backdrop_c(const bb_ui_in *in, uint32_t page)
{
    if (in->fault)
        return c_scale(BB_UI_RED, 90);
#if BB_UI_BACKDROP == 2
    (void)page;
    return c_scale(inst_c(in), 205);
#else
    /* browse pages (HOME, PATTERN, KIT): the instrument colour, dim. Pages whose own content is
     * in the instrument colour (step grids, bars, the Dillo roll, effects): a neutral slate. */
    if (page == BB_UI_STUDIO || page == BB_UI_PATTERN || page == BB_UI_KIT)
        return c_scale(inst_c(in), 108);
    return BB_UI_BD_COOL;
#endif
}
static uint32_t backdrop_hash(const bb_ui *u, const bb_ui_in *in, uint32_t page)
{
    return backdrop_on(page) ? h_u(H0, page | (uint32_t)in->instr << 8 | (uint32_t)u->roll << 16 | (uint32_t)in->fault << 24) : 0u;
}
static void draw_backdrop(const bb_ui *u, const bb_ui_in *in, uint32_t page)
{
    uint16_t ink = backdrop_c(in, page), let = c_scale(BB_UI_TEXT, BB_UI_BACKDROP == 2 ? 200 : 120);
    int32_t x, y, xa = BD_X < g_cx ? g_cx : BD_X, xb = BD_X + BD_N > g_cx + g_cw ? g_cx + g_cw : BD_X + BD_N;
    int32_t ya = BD_Y < g_cy ? g_cy : BD_Y, yb = BD_Y + BD_N > g_cy + g_ch ? g_cy + g_ch : BD_Y + BD_N;
    if (xb > 240) xb = 240;
    if (yb > BD_Y1) yb = BD_Y1;
    for (y = ya; y < yb; y++) {
        int32_t s0 = (y - BD_Y) * 240 / BD_N, s1 = (y - BD_Y + 1) * 240 / BD_N;
        for (x = xa; x < xb; x++) {
            int32_t t0 = (x - BD_X) * 240 / BD_N, t1 = (x - BD_X + 1) * 240 / BD_N, sx, sy, n = 0, hit = 0, lh = 0;
            int ball = BB_UI_ATTRACT_BALL && BAYER4[y & 3][x & 3] < ROLL_THR[u->roll];
            for (sy = s0; sy < s1; sy++)
                for (sx = t0; sx < t1; sx++, n++) {
#if BB_UI_ATTRACT_BALL
                    if (ball) {
                        if (bits1(BB_BLET_BITS, sx, sy, BB_BLET_X, BB_BLET_Y, BB_BLET_W, BB_BLET_H)) lh++;
                        else if (bits1(BB_BALL_BITS, sx, sy, BB_BALL_X, BB_BALL_Y, BB_BALL_W, BB_BALL_H)) hit++;
                        continue;
                    }
#endif
                    hit += (int32_t)base_bit(sx, sy);
                }
            if (!n || (!hit && !lh))
                continue;
            u_px(x, y, lh * 2 >= n ? let : c_scale(ink, (uint32_t)(hit + lh) * 256u / (uint32_t)n));
        }
    }
}
#endif

static void page_arrange(const bb_ui *u,const bb_ui_in *in,uint32_t now){
    uint16_t colour=inst_c(in);(void)u;(void)now;
    u_text(8,30,&AF_S,in->running?"PLAYING":in->small_items?"SOURCE":"LOADED",BB_UI_GREY,BB_UI_BG);
    u_name(64,30,&AF_S,in->pattern_name?in->pattern_name:"NONE",BB_UI_TEXT,BB_UI_BG,132);
    u_text(8,48,&AF_S,"NEXT",BB_UI_GREY,BB_UI_BG);
    u_name(64,48,&AF_S,in->next_name?in->next_name:"NONE",colour,BB_UI_BG,160);
    u_name(8,66,&AF_S,in->subtitle?in->subtitle:"",BB_UI_GREY,BB_UI_BG,224);
    for(unsigned i=0;i<in->items&&i<4;i++){
        int32_t y=86+(int32_t)i*18;uint16_t bg=i==in->cursor?c_scale(colour,56):BB_UI_BG;
        if(i==in->cursor)u_rect(5,y,230,18,bg);
        u_text(8,y+3,&AF_S,i==in->playing_item?"P":i==in->queued_item?"N":" ",colour,bg);
        if(in->small_items){u_rect(5,y,230,18,bg);u_name(8,y+3,&AF_S,in->item[i],BB_UI_TEXT,bg,224);}
        else u_brand(24,y+3,NM_LIST,in->item[i],i==in->cursor?colour:BB_UI_TEXT,bg,208,0);
    }
    u_name(8,165,&AF_S,in->title?in->title:"",BB_UI_GREY,BB_UI_BG,224);
}
static int pl_draw(const bb_ui_in *in, uint32_t page);   /* bb_ui_play.c */
static void page_draw(const bb_ui *u, const bb_ui_in *in)
{
    uint32_t page = eff_page(u, in), now = in->now_ms, pk = pop_on(u, in);
#if BB_UI_HELP
    if (page == BB_UI_PAGES) {
        page_help(in, in->page);
        return;
    }
#endif
#if BB_UI_ALL_PAGES
    if (page == BB_UI_ATTRACT) {
        page_attract(u, in);
        return;
    }
#endif
    draw_status(in, now);
#if BB_UI_ALL_PAGES
    if (backdrop_on(page))
        draw_backdrop(u, in, page);
#endif
    if (!pl_draw(in, page))
    switch (page) {
#if !BB_UI_PLAY
    case BB_UI_PLAY_DRUMS: page_play_drums(u, in, now); break;
    case BB_UI_PLAY_DILLO: page_dillo(u, in); break;
#endif
    case BB_UI_KIT: case BB_UI_PATTERN: page_chooser(u, in, now, pk); break;
    case BB_UI_ARRANGE: page_arrange(u,in,now); break;
#if BB_UI_HOME
    case BB_UI_STUDIO: page_perform(in, now); break;
#else
#if BB_UI_ALL_PAGES
    case BB_UI_STUDIO: page_studio(u, in, now); break;
#endif
#endif
#if BB_UI_ALL_PAGES
#if !BB_UI_PLAY
    case BB_UI_STEPS: page_steps(in); break;
    case BB_UI_FX: page_fx(in, now); break;
#endif
    default: page_knobs(in); break;
#else
    default: break;
#endif
    }
    if (full_page(page)) {                                       /* RC2: choosers and PERFORM: no knob row, key strip */
        BB_UI_HOOK_LAYER(1);
        if (pk != BB_UI_NO) draw_band(in, pk);
        if (in->fault) draw_toast(u, in);
        BB_UI_HOOK_LAYER(0);
        return;
    }
    if (has_keys(page))
        draw_keys(in, now);
    draw_knobs(in, pk);
    BB_UI_HOOK_LAYER(1);
    if (pk != BB_UI_NO)                                          /* 2.0 D4: every page, KNOBS and SONG included */
        draw_popup(in, pk);
    if (legend_on(u, in, page)) {                                /* 2.0 D4: the key legend, transient, over the key band */
        u_rect(0, KEYS_Y, 240, 15, BB_UI_BG);
        u_text_fit(8, KEYS_Y, &AF_S, in->legend, BB_UI_GREY, BB_UI_BG, 224);
    }
    if (in->fault)
        draw_toast(u, in);
    else if (in->notice)
        draw_notice(in);
    BB_UI_HOOK_LAYER(0);
}

/* ================================================================ elements ===
 * Each page lists its elements in priority order: rectangle + hash of what is drawn
 * there. The list for a page is always the same length and order (the hash table
 * u->drawn is indexed by position). */
typedef struct { uint8_t x, y, w, h; uint32_t hash; } el_t;
typedef struct { el_t e[BB_UI_MAX_EL]; uint32_t n; } els_t;

static void el(els_t *l, int32_t x, int32_t y, int32_t w, int32_t h, uint32_t hash)
{
    el_t *e;
    if (l->n >= BB_UI_MAX_EL)
        return;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > 240) w = 240 - x;
    if (y + h > 240) h = 240 - y;
    if (w < 0) w = 0;
    if (h < 0) h = 0;
    if (w && h && (uint32_t)(w * h) > BB_UI_BUDGET_PX) {     /* big: bands that each fit a frame */
        int32_t rows = (int32_t)(BB_UI_BUDGET_PX / (uint32_t)w), r;
        for (r = 0; r < h && l->n < BB_UI_MAX_EL; r += rows) {
            e = &l->e[l->n++];
            e->x = (uint8_t)x; e->y = (uint8_t)(y + r); e->w = (uint8_t)w;
            e->h = (uint8_t)(h - r < rows ? h - r : rows);
            e->hash = hash;
        }
        return;
    }
    e = &l->e[l->n++];
    e->x = (uint8_t)x; e->y = (uint8_t)y; e->w = (uint8_t)w; e->h = (uint8_t)h;
    e->hash = hash;
}
static uint32_t keys_hash(const bb_ui_in *in, uint32_t now)
{
    uint32_t h = h_u(H0, in->keys_down), k, br = 0;
    h = h_u(h, in->keys_red);
    h = h_u(h, in->instr);
    for (k = 0; k < 27u; k++) {
        h = h_u(h, in->key_led[k]);
        br |= in->key_led[k] == BB_UI_LED_BREATH;
    }
    return br ? h_u(h, breath_q(now)) : h;
}
static uint32_t knob_hash(const bb_ui_in *in, uint32_t k, uint32_t pk)
{
    const bb_ui_knob *kn = &in->knob[k];
    uint32_t h = h_s(h_s(H0, kn->label), kn->text);
    return h_u(h_u(h, (uint32_t)kn->bar << 16 | (uint32_t)kn->flags << 8 | in->instr), pk == k);
}
/* the elements every page shares: toast and beat dots first; pop-ups, knobs, keys, status last */
static void els_frame_hi(els_t *l, const bb_ui *u, const bb_ui_in *in)
{
    el(l, TOAST_X, TOAST_Y, TOAST_W, TOAST_H, in->fault ? h_u(h_s(H0, in->fault_text), mini_hash(u, in)) :
       in->notice && !full_page(eff_page(u, in)) ? h_u(h_s(H0, in->notice), inst_c(in)) : 0u);
    el(l, 150, 6, 36, 12, h_u(H0, (uint32_t)in->running << 8 | in->beat));
#if BB_UI_ALL_PAGES
    el(l, BD_X, BD_Y, (BD_X + BD_N > 240 ? 240 : BD_X + BD_N) - BD_X, (BD_Y + BD_N > BD_Y1 ? BD_Y1 : BD_Y + BD_N) - BD_Y,
       backdrop_hash(u, in, eff_page(u, in)));
#endif
}
static void els_frame_lo(els_t *l, const bb_ui *u, const bb_ui_in *in, uint32_t now, uint32_t page)
{
    uint32_t k, pk = pop_on(u, in), full = full_page(page);
    for (k = 0; k < 4u; k++)
        el(l, pop_x(k), POP_Y, POP_W, POP_H, pk == k && !full ? h_u(knob_hash(in, k, 9), 1) : 0u);
    for (k = 0; k < 4u; k++)
        el(l, KNOB_X(k) - 2, 199, 56, 41, full ? 0u : knob_hash(in, k, pk));
    el(l, 0, KEYS_Y, 240, 15, full ? 0u : legend_on(u, in, page) ? h_s(H0, in->legend) : has_keys(page) ? keys_hash(in, now) : 0u);
    el(l, 0, CH_BAND_Y, 240, 60, full && pk != BB_UI_NO ? h_u(knob_hash(in, pk, 9), 2) : 0u);   /* RC2: the knob band */
    el(l, 0, 0, 148, 23, h_u(h_s(h_s(H0, in->page_name), in->instr_name), (uint32_t)in->instr << 8 | in->accent |
                                (page == BB_UI_PATTERN && in->tabs ? 0x10000u << in->tab_cur : 0u)));
    el(l, 186, 0, 54, 23, h_u(h_u(H0, in->bpm_x10), (uint32_t)in->running << 8 |
                             (!in->running && (in->queued & BB_UI_Q_START) ? 1u + breath_q(now) : 0u)));
}
static void el_mini(els_t *l, const bb_ui *u, const bb_ui_in *in, const clip_t *b)
{
    el(l, b->x0, b->y0, b->x1 - b->x0, b->y1 - b->y0, BB_UI_BACKDROP ? 0u : mini_hash(u, in));
}

#if BB_UI_PLAY
#include "bb_ui_play.c"
#else
static int pl_draw(const bb_ui_in *in, uint32_t page) { (void)in; (void)page; return 0; }
#endif
static void els_page(els_t *l, const bb_ui *u, const bb_ui_in *in, uint32_t page)
{
    uint32_t now = in->now_ms, i, h, g;
#if BB_UI_ALL_PAGES
    uint32_t k;
#endif
    int32_t head = in->running ? (int32_t)in->step : -1;
#if BB_UI_PLAY
    (void)g;
    (void)head;
#endif
    l->n = 0;
    if (page == BB_UI_PAGES) {                                  /* help: one picture per page of rows / pick */
        h = h_u(h_u(H0, in->page), (uint32_t)in->help_rows | (uint32_t)in->help_sel << 8 | (uint32_t)in->help_page << 16 |
                                    (uint32_t)in->help_pages << 24);
        for (i = 0; in->help_row && i < in->help_rows && i < BB_UI_HELP_ROWS; i++)
            h = h_u(h_s(h_s(h, in->help_row[i].text), in->help_row[i].keys),
                    (uint32_t)in->help_row[i].kc | (uint32_t)in->help_row[i].kc2 << 8 | (uint32_t)in->help_row[i].rec << 16);
        el(l, 0, 0, 240, 240, h_s(h_s(h, in->help_note), in->page_name));
        return;
    }
#if BB_UI_ALL_PAGES
    if (page == BB_UI_ATTRACT) {
        h = h_u(h_u(H0, in->level >> 5), attract_beat(u, in) | (uint32_t)u->roll << 8 | (uint32_t)in->instr << 16);
        el(l, 0, 0, 240, ATTRACT_TOP, in->running && !in->fault && !u->roll ? h_u(h, rings_hash(&u->rings, now)) : h);
        el(l, 0, 0, 240, 240, h_u(H0, (uint32_t)in->instr | (uint32_t)in->fault << 8 | (uint32_t)u->roll << 16));
        return;
    }
#endif
    els_frame_hi(l, u, in);
    if (BB_UI_PLAY && pl_els(l, in, page))
        ;
    else switch (page) {
#if BB_UI_HOME
    case BB_UI_STUDIO: {                                        /* RC2: PERFORM */
        uint32_t n = 0;
        while (n < 16u && in->tile_state[n] != BB_UI_TILE_NONE && in->tile[n]) n++;
        el(l, 0, 24, 240, 24, h_s(H0, in->perf_song));
        for (i = 0; i < 3u; i++)
            el(l, 8 + 76 * (int32_t)i, PF_PART_Y, 72, 18, h_u(H0, in->lane[i].flags | (uint32_t)in->instr << 8));
        el(l, 0, PF_KEYS_Y, 240, PF_KEYS_H, h_u(H0, n | (uint32_t)in->tile_page << 8));   /* the layout (clears) */
        for (i = 0; i < 16u; i++) {
            int32_t x = 0, y = 0, w = 0, bh = 0;
            if (i < n) pf_key_box(i, n, &x, &y, &w, &bh);
            h = i < n ? h_u(h_s(H0, in->tile[i]), in->tile_state[i] | (uint32_t)in->tile_page << 8 | n << 16) : 0u;
            el(l, x, y, w, bh, h && in->tile_state[i] == BB_UI_TILE_QUEUED ? h_u(h, breath_q(now)) : h);
        }
        el(l, 0, PF_STATUS_Y - 2, 240, 16, h_u(h_s(h_s(H0, in->notice), in->perf_status), in->instr));
        el(l, 0, PF_LEGEND_Y, 240, 16, h_s(H0, in->footer));
        break;
    }
#endif
#if !BB_UI_PLAY
    case BB_UI_PLAY_DRUMS:
        el_mini(l, u, in, &MINI46_BOX);
        for (i = 0; i < 16u; i++) {                             /* the playhead moves: two columns */
            h = h_u(H0, ((int32_t)i == head) | (uint32_t)in->group_muted << 1 | (uint32_t)in->instr << 8);
            for (g = 0; g < 4u; g++)
                h = h_u(h, (uint32_t)(in->gsteps[g] >> i & 1u) | (uint32_t)(in->gacc[g] >> i & 1u) << 1);
            el(l, CELL_X((int32_t)i) - 1, LANES_Y, 10, 4 * LANE_H, h);
        }
        for (g = 0; g < 4u; g++) {
            uint32_t muted = in->group_muted >> g & 1u, lv = muted || !in->running ? 0u : in->group_out[g];
            int32_t y = LANES_Y + LANE_H * (int32_t)g;
            el(l, GMETER_X, y + 5, 21, 15, h_u(H0, lv * 20u / 255u | flash_level(u, g, now) << 8 | muted << 9 |
                                                    (uint32_t)in->instr << 10));
            el(l, 0, y, 48, LANE_H, muted);
        }
        el(l, 0, 24, MINI46_BOX.x0, 26, h_s(H0, in->pattern_name));
        h = h_s(h_s(h_s(H0, in->section_name), in->next_name), 0);
        h = h_u(h, (uint32_t)in->bar << 8 | in->bars | (uint32_t)in->instr << 16);
        el(l, 0, 50, MINI46_BOX.x0, 20, in->next_name && in->next_name[0] ? h_u(h, breath_q(now)) : h);
        el(l, 30, MASTER_Y, 156, 15, h_u(H0, (uint32_t)in->out_peak[0] * 69u / 255u | (uint32_t)in->out_peak[1] * 69u / 255u << 8 |
                                              (uint32_t)(in->out_peak[0] >= 252u) << 16 | (uint32_t)(in->out_peak[1] >= 252u) << 17));
        el(l, 186, MASTER_Y, 54, 15, h_u(H0, (uint32_t)in->dsp * 12u / 255u | (uint32_t)(in->dsp >= 230u) << 8 |
                                              (uint32_t)in->instr << 9));
        break;
#endif
#if BB_UI_ALL_PAGES
#if !BB_UI_HOME                                           /* HOME replaces the three-lane STUDIO */
    case BB_UI_STUDIO:
        for (i = 0; i < 3u; i++) {
            const bb_ui_lane *ln = &in->lane[i];
            int32_t y = LANE_Y(i);
            el(l, 0, y - 4, 240, 50, ln->flags & (BB_UI_LN_FOCUS | BB_UI_LN_OFF | BB_UI_LN_MUTED));
            h = h_u(h_u(H0, ln->steps), ln->accents);
            el(l, SCELL_X(0), y + 31, SCELL_X(16) - SCELL_X(0), 12, h_u(h_u(h, (uint32_t)head), ln->flags));
            el(l, 212, y + 31, 21, 9, (uint32_t)ln->level * 19u / 255u);
            h = h_s(h_s(h_s(h_s(H0, ln->name), ln->pattern), ln->part), ln->next);
            el(l, 12, y - 3, 190, 34, h_u(h, (uint32_t)ln->flags << 8 | (ln->next && ln->next[0] ? breath_q(now) : 0u)));
        }
        el_mini(l, u, in, &MINI34_BOX);
        break;
#endif
#if !BB_UI_PLAY
    case BB_UI_STEPS:
        for (i = 0; i < 16u; i++) {
            uint32_t v;
            h = h_u(H0, (uint32_t)in->voice << 8 | ((int32_t)i == head));
            for (v = 0; v < 11u; v++)
                h = h_u(h, (uint32_t)(in->vsteps[v] >> i & 1u) << 1 | (in->vacc[v] >> i & 1u));
            el(l, MAT_X - 2 + 13 * (int32_t)i, MAT_Y - 4, i == 15u ? 14 : 13, 7 * 11 + 3, h ^ in->instr);
        }
        for (i = 0; i < 16u; i++) {
            uint32_t st = in->voice < 11u ? (uint32_t)(in->vsteps[in->voice] >> i & 1u) << 1 |
                                            (in->vacc[in->voice] >> i & 1u) : 0u;
            el(l, 8 + 14 * (int32_t)i, 130, 14, 48, h_u(H0, st << 8 | ((int32_t)i == head) << 4 | in->instr));
        }
        el(l, 0, 24, 240, 22, h_u(h_s(H0, in->voice_name), (uint32_t)in->bank << 8 | in->banks));
        break;
#endif
#endif
#if !BB_UI_PLAY
    case BB_UI_PLAY_DILLO:
        el_mini(l, u, in, &MINI34_BOX);
        for (i = 0; i < 16u; i++) {
            h = h_u(H0, (uint32_t)(uint8_t)in->note[i] << 8 | ((int32_t)i == head));
            h = h_u(h, (in->note_acc >> i & 1u) | (in->note_slide >> i & 1u) << 1);
            if (i < 15u) h = h_u(h, (uint8_t)in->note[i + 1]);
            if (i) h = h_u(h, (uint32_t)(uint8_t)in->note[i - 1] << 8 | (in->note_slide >> (i - 1) & 1u) << 1 |
                              (in->note_acc >> (i - 1) & 1u));
            el(l, 8 + 14 * (int32_t)i, ROLL_Y0 - 4, 14, ROLL_Y1 - ROLL_Y0 + 9, h);
        }
        el(l, 126, 136, 112, 44, h_u(H0, (uint32_t)(in->cutoff >> 2) << 8 | (in->reso >> 2)));
        el(l, 0, 138, 100, 38, (uint8_t)in->octave);
        el(l, 0, 24, MINI34_BOX.x0, 36, h_s(h_s(H0, in->patch), in->scale));
        break;
#endif
    case BB_UI_ARRANGE:
        el(l,0,24,200,22,h_u(h_s(H0,in->pattern_name),in->running));
        el(l,0,46,240,18,h_s(H0,in->next_name));
        el(l,0,64,240,20,h_s(H0,in->subtitle));
        for(i=0;i<4;i++){
            h=h_u(h_s(H0,i<in->items?in->item[i]:0),in->small_items);
            h=h_u(h,(i==in->cursor)|((i==in->playing_item)<<1)|((i==in->queued_item)<<2));
            el(l,0,86+(int32_t)i*18,240,18,h);
        }
        el(l,0,163,240,16,h_s(H0,in->title));break;
    case BB_UI_KIT:
    case BB_UI_PATTERN: {                                       /* RC2: the list rows, the footer */
        if(in->welcome) {
            h=h_u(h_u(H0,in->cursor),in->sections);
            h=h_u(h,mini_hash(u,in));
            for(i=0;i<in->items&&i<BB_UI_LIST;i++)h=h_s(h,in->item[i]);
            h=h_s(h_s(h,in->footer),in->notice);
            el(l,0,24,240,216,h_u(h,breath_q(now)));
            break;
        }
        uint32_t pk = pop_on(u, in), rows = list_rows(in, pk), first = list_first(in, pk), qbit = list_queued(in);
        for (i = 0; i < BB_UI_LIST; i++) {
            uint32_t idx = first + i, q = qbit && idx == in->queued_item;
            h = i < rows && idx < in->items ? h_u(h_s(H0, in->item[idx]), in->item_key[idx]) : 0u;
            if (h) h = h_u(h, (idx == in->cursor) | (idx == in->playing_item) << 1 | q << 2 | (q ? breath_q(now) << 4 : 0u) |
                              (uint32_t)in->instr << 9);
            el(l, 0, CH_Y0 + CH_ROW * (int32_t)i, 240, CH_ROW, h);
        }
        el(l, 0, CH_FOOT_Y - 2, 240, 17, pk != BB_UI_NO ? 0u : h_u(h_s(h_s(H0, in->notice), in->footer), in->instr | 0x100u));
        break;
    }
#if BB_UI_ALL_PAGES
#if !BB_UI_PLAY
    case BB_UI_FX:
        el(l, 24, 80, 192, 32, h_u(h_u(H0, in->repeats), echo_phase(in, now) << 8 | in->instr));
        el(l, 0, 24, 240, 172, h_s(h_u(H0, (uint32_t)in->heads | in->instr << 8 | (uint32_t)in->tabs << 16 |
                                           (uint32_t)in->tab_cur << 24), in->tab_note));
        break;
#endif
    default:
        for (k = 0; k < 4u; k++)
            el(l, KNOB_X(k), METER_Y0, 50, METER_Y1 - METER_Y0, knob_hash(in, k, 9));
        el(l, 0, 24, 240, METER_Y0 - 24, h_s(h_u(h_s(h_s(H0, in->title), in->subtitle), (uint32_t)in->tabs << 8 |
                                                 in->tab_cur), in->tab_note));                /* title + EDIT line (+ tabs) */
        break;
    #else
    default:
        break;
#endif
    }
    els_frame_lo(l, u, in, now, page);
}

/* ================================================================ drawing ===== */
static uint32_t draw_rect(const bb_ui *u, const bb_ui_in *in, int32_t x, int32_t y, int32_t w, int32_t h)
{
    int32_t rows, r;
    int32_t oy = cv_oy;
    if (w <= 0 || h <= 0)
        return 0;
    rows = (int32_t)(CV_MAX / (uint32_t)w);
    cv_oy = 0;
    for (r = 0; r < h; r += rows) {
        int32_t hh = h - r < rows ? h - r : rows;
        cv_begin((uint32_t)w, (uint32_t)hh, BB_UI_BG);
        g_cx = x;
        g_cy = y + r;
        g_cw = w;
        g_ch = hh;
        page_draw(u, in);
        cv_blit((uint32_t)x, (uint32_t)(y + r));
    }
    cv_oy = oy;
    return (uint32_t)(w * h);
}

uint8_t bb_ui_page_for(uint8_t nav_page, uint8_t play_keys, uint8_t synth)
{
    /* bb_nav_page order: STUDIO PLAY KIT PATTERN SOUND FX SAVE EVOLVE GLOBAL */
    switch (nav_page) {
    case 0: return BB_UI_STUDIO;
    case 1: return synth ? BB_UI_PLAY_DILLO : play_keys ? BB_UI_STEPS : BB_UI_PLAY_DRUMS;
    case 2: return BB_UI_KIT;
    case 3: return BB_UI_PATTERN;
    case 5: return BB_UI_FX;
    default: return BB_UI_KNOBS;
    }
}


void bb_ui_init(bb_ui *u, uint32_t now_ms, uint8_t running)
{
    uint32_t i;
    u->page = BB_UI_NO;
    u->help = 0;
    u->n_el = 0;
    for (i = 0; i < BB_UI_MAX_EL; i++)
        u->drawn[i] = 0;
    for (i = 0; i < 4u; i++) {
        u->flash_t[i] = 0;
        u->rings.wave[i].born = 0;
        u->rings.wave[i].active = 0;
    }
    u->rings.tick = 0;
    u->rings.slot = 0;
    u->rings.seen = 0;
    u->pop_t = 0;
    u->pop_k = BB_UI_NO;
    u->beat_t = 0;
    u->idle_t = now_ms;
    u->roll = running ? 0u : BB_UI_ROLL_STEPS;
    u->roll_t = now_ms;
    u->budget_px = BB_UI_BUDGET_PX;
    u->st.px = 0;
    u->st.regions = u->st.waiting = u->st.full = 0;
}

void bb_ui_invalidate(bb_ui *u) { u->page = BB_UI_NO; }

const bb_ui_stats *bb_ui_frame(bb_ui *u, const bb_ui_in *in)
{
    els_t l;                                /* (stack scratch, ~520 B) */
    uint32_t now = in->now_ms, ev = in->events, i, page, px = 0, drawn = 0, waiting = 0;
    /* ---- events */
    if (ev & BB_EV_BEAT)
        u->beat_t = now | 1u;
    for (i = 0; i < 4u; i++)
        if (ev & (BB_EV_KICK << i))
            u->flash_t[i] = now | 1u;
    if (ev & BB_EV_KNOB) {
        u->pop_t = now | 1u;
        u->pop_k = in->ev_knob < 4u ? in->ev_knob : BB_UI_NO;
    }
    if (in->knob_turning < 4u) {
        u->pop_t = now | 1u;
        u->pop_k = in->knob_turning;
    }
    if (ev & BB_EV_LEGEND) {                /* 2.0 D4: page entered, or a key the page does not use */
        u->pop_t = now | 1u;
        u->pop_k = BB_UI_POP_LEGEND;
    }
    if ((ev & (BB_EV_KNOB | BB_EV_INPUT | BB_EV_TAP)) || in->page != BB_UI_STUDIO || in->help || in->fault)
        u->idle_t = now;
    /* ---- the Armadillo: the rings (installed timing) and the roll, one ordered step at a time */
    rings_motion(&u->rings, in->running, in->ticks, now);
    {
        uint32_t target = in->running ? 0u : BB_UI_ROLL_STEPS;
        if (u->roll == target)
            u->roll_t = now;
        else if (now - u->roll_t >= roll_step_ms(u->roll < target ? u->roll : u->roll - 1u)) {
            u->roll = (uint8_t)(u->roll < target ? u->roll + 1u : u->roll - 1u);
            u->roll_t = now;
        }
    }
    /* ---- what changed */
    page = eff_page(u, in);
    if (page != u->page && !(ev & (BB_EV_KNOB | BB_EV_LEGEND)) && in->knob_turning >= 4u)
        u->pop_t = 0;                       /* a pop-up belongs to its page's knob */
    els_page(&l, u, in, page);
    u->st.full = 0;
    if (page != u->page) {                  /* page change: the whole screen, once */
        px = draw_rect(u, in, 0, 0, 240, 240);
        for (i = 0; i < l.n; i++)
            u->drawn[i] = l.e[i].hash;
        u->page = (uint8_t)page;
        u->n_el = (uint8_t)l.n;
        u->st.full = 1;
        drawn = 1;
    } else {
        for (i = 0; i < l.n; i++) {
            const el_t *e = &l.e[i];
            uint32_t a = (uint32_t)e->w * e->h, j;
            if (u->drawn[i] == e->hash)
                continue;
            if (!a) {
                u->drawn[i] = e->hash;
                continue;
            }
            if (drawn && u->budget_px && px + a > u->budget_px) {
                waiting++;                  /* skipped: drawn on a later frame */
                continue;
            }
            px += draw_rect(u, in, e->x, e->y, e->w, e->h);
            drawn++;
            u->drawn[i] = e->hash;
            for (j = i + 1u; j < l.n; j++) {   /* elements inside what was just drawn are up to date */
                const el_t *f = &l.e[j];
                if (f->x >= e->x && f->y >= e->y && f->x + f->w <= e->x + e->w && f->y + f->h <= e->y + e->h)
                    u->drawn[j] = f->hash;
                else if (u->drawn[j] != f->hash &&
                         f->x < e->x + e->w && e->x < f->x + f->w && f->y < e->y + e->h && e->y < f->y + f->h)
                    u->drawn[j] = ~f->hash;    /* a changed element repainted in part: never take its old hash as drawn */
            }
        }
    }
    u->st.px = (uint16_t)(px > 0xFFFFu ? 0xFFFFu : px);
    u->st.regions = (uint8_t)drawn;
    u->st.waiting = (uint8_t)waiting;
    return &u->st;
}
