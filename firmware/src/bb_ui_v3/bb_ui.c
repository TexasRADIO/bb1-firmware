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
    int32_t w = text_w(&AF_S, nm), x0 = 110 - w / 2, i, pw;
    { int32_t d0; const char *pn0 = in->page_name ? in->page_name : ""; pw = text_w(brand_face(2, pn0, &AF_S, &d0), pn0); }
    char bpm[8];
    uint32_t b = in->bpm_x10, n = 0;
    if (x0 < 8 + pw + 20)                                     /* a long page name pushes the instrument right */
        x0 = 8 + pw + 20;
    if (x0 + w > 148)
        x0 = 148 - w;
    {
        const char *pn = in->page_name ? in->page_name : "";
        int32_t dy; const aafont_t *pf = brand_face(2, pn, &AF_S, &dy);
        u_text_fit(8, STATUS_TXT_Y + dy, pf, pn, BB_UI_TEXT, BB_UI_BG, x0 - 10 - 8 - 4);
    }
    u_rbox(x0 - 10, 9, 6, 6, 3, in->accent ? BB_UI_RED : c, BB_UI_BG);
    u_text(x0, STATUS_TXT_Y, &AF_S, nm, c, BB_UI_BG);
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
static int32_t black_x(uint32_t j)           /* black key j (0..10) sits after white keys 0,1,3,4,5,7,8,10,11,12,14 */
{
    static const uint8_t AFTER[11] = {0, 1, 3, 4, 5, 7, 8, 10, 11, 12, 14};
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
        if (i == head)
            u_rect(x, 175, 13, 2, BB_UI_TEXT);
    }
}

#endif /* BB_UI_ALL_PAGES */

#if BB_UI_HOME
/* HOME groups the two drum layers inside Big Beat; Dillo remains the other
 * instrument. Only committed player selections are described as playing. */
static void page_home(const bb_ui *u, const bb_ui_in *in, uint32_t now)
{
    uint32_t r;
    const bb_ui_lane *d = &in->lane[2];
    (void)now;
    u_text(12, 28, &AF_M, "BIG BEAT", BB_UI_KIT_A, BB_UI_BG);
    if (!BB_UI_BACKDROP) draw_mini(u, in, MINI34_X, MINI34_Y, 34, &MINI34_BOX);
    for (r = 0; r < 2u; r++) {
        const bb_ui_lane *l = &in->lane[r];
        int32_t y = 51 + 41 * (int32_t)r;
        uint16_t c = r ? BB_UI_KIT_B : BB_UI_KIT_A;
        if (l->flags & BB_UI_LN_OFF) c = BB_UI_FAINT;
        else if (l->flags & BB_UI_LN_MUTED) c = BB_UI_GREY;
        u_rect(8, y - 2, (l->flags & BB_UI_LN_FOCUS) ? 5 : 3, 37, c);
        u_text(18, y, &AF_S, r ? "B" : "A", c, BB_UI_BG);
        u_text_fit(35, y, &AF_S, l->part ? l->part : "", BB_UI_GREY, BB_UI_BG, 88);
        u_text_fit(126, y, &AF_S, (l->flags & BB_UI_LN_OFF) ? "SELECT, PRESETS" : l->name ? l->name : "", BB_UI_GREY, BB_UI_BG,
                   l->next && l->next[0] ? 57 : 106);         /* an off lane says how to add a kit */
        u_brand(35, y + 13, NM_LIST,
                (l->flags & BB_UI_LN_OFF) ? "ADD KIT" : l->pattern,
                (l->flags & BB_UI_LN_OFF) ? BB_UI_GREY : BB_UI_TEXT,
                BB_UI_BG, 196, 0);
        if (l->next && l->next[0])
            u_text_fit(188, y, &AF_S, "NEXT", c, BB_UI_BG, 44);
    }
    u_rect(8, 130, 224, 1, BB_UI_FAINT);
    u_rect(8, 137, (d->flags & BB_UI_LN_FOCUS) ? 5 : 3, 42,
           (d->flags & BB_UI_LN_MUTED) ? BB_UI_GREY : BB_UI_DILLO);
    u_text(19, 134, &AF_M, "DILLO", BB_UI_DILLO, BB_UI_BG);
    u_text_fit(93, 137, &AF_S, d->part ? d->part : "", BB_UI_GREY, BB_UI_BG, 80);
    u_text_fit(178, 137, &AF_S, d->name ? d->name : "", BB_UI_GREY, BB_UI_BG, 54);
    u_brand(19, 154, NM_LIST, d->pattern, BB_UI_TEXT, BB_UI_BG, 212, 0);
    if (d->next && d->next[0])
        u_text_fit(19, 172, &AF_S, "NEXT PATTERN QUEUED", BB_UI_DILLO, BB_UI_BG, 205);
    u_text_fit(8, 184, &AF_S, "EDIT PLAY  PRESETS SOUND  ALGO PAT", BB_UI_GREY, BB_UI_BG, 224);
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

/* ---------------------------------------------------------------- KIT (v3 §3)
 * the name in brand type (26 px; a second line under the mini if it needs one), a grey line,
 * the list (15 px), "loads at the next bar" while queued (the ring fills to the bar) */
#define TITLE_CAP_Y 30
#define TITLE_W (MINI34_X - 8 - 6)
#define TITLE_W2 224
#define LIST_ROW 18
#define LIST_END 178            /* KIT: the list runs to the key strip */
#define PLIST_END 150           /* PATTERN: the list, then the section strip */
static uint32_t title_lines(const char *s)
{
    nm_lay L = name_layout(NM_TITLE, s ? s : "", TITLE_W, TITLE_W2);
    return L.split ? 2u : 1u;
}
static int32_t list_y(const bb_ui_in *in) { return 74 + NM_TITLE_LEAD * ((int32_t)title_lines(in->title) - 1); }
static int32_t list_end(const bb_ui_in *in) { return in->page == BB_UI_PATTERN ? PLIST_END : LIST_END; }
static uint32_t list_rows(const bb_ui_in *in)
{
    uint32_t n = (uint32_t)(list_end(in) - list_y(in)) / LIST_ROW;
    return n > BB_UI_LIST ? BB_UI_LIST : n;
}
static uint32_t list_first(const bb_ui_in *in)     /* the window of the list that holds the cursor */
{
    uint32_t rows = list_rows(in), items = in->items < BB_UI_LIST ? in->items : BB_UI_LIST;
    if (items <= rows || in->cursor < rows)
        return 0;
    return in->cursor + 1u - rows;
}
static uint32_t list_queued(const bb_ui_in *in)
{
    return (in->queued & (in->page == BB_UI_PATTERN ? BB_UI_Q_PATTERN : BB_UI_Q_KIT)) != 0;
}
/* the title, its grey line and the list (KIT: kits / sounds; PATTERN: patterns) */
static void draw_browser(const bb_ui_in *in, uint32_t now)
{
    uint16_t c = inst_c(in);
    uint32_t i, rows = list_rows(in), first = list_first(in);
    int32_t ly = list_y(in);
    u_brand(8, TITLE_CAP_Y, NM_TITLE, in->title, BB_UI_TEXT, BB_UI_BG, TITLE_W, TITLE_W2);
    u_name(8, ly - 20, &AF_S, in->subtitle ? in->subtitle : "", BB_UI_GREY, BB_UI_BG, 224);
    for (i = 0; i < rows && first + i < in->items && first + i < BB_UI_LIST; i++) {
        uint32_t k = first + i, cur = k == in->cursor, q = list_queued(in) && k == in->queued_item;
        int32_t ry = ly + LIST_ROW * (int32_t)i;
        uint16_t bg = cur ? c_scale(c, 56) : BB_UI_BG;
        uint16_t fg = q ? breath_c(c, now) : cur ? c : k == in->playing_item ? BB_UI_TEXT : BB_UI_GREY;
        if (cur)
            u_rect(0, ry, 176, LIST_ROW, bg);
        if (k == in->playing_item)
            u_rect(8, ry + 6, 3, 6, c);
        u_brand(16, ry + 4, NM_LIST, in->item[k], fg, bg, 156, 0);
    }
}
static void page_kit(const bb_ui *u, const bb_ui_in *in, uint32_t now)
{
    uint16_t c = inst_c(in);
    draw_browser(in, now);
    if (in->queued & BB_UI_Q_KIT) {
        draw_ring(207, 110, in->progress, c);
        u_text_r(232, 126, &AF_S, "NEXT BAR", c, BB_UI_BG);
    } else if (in->loading) {
        u_text_r(232, 126, &AF_S, "LOADING", breath_c(c, now), BB_UI_BG);
    }
    if (!BB_UI_BACKDROP) draw_mini(u, in, MINI34_X, MINI34_Y, 34, &MINI34_BOX);
}

/* ---------------------------------------------------------------- PATTERN (v3 §3)
 * the name in brand type, a grey line, the pattern list (15 px), then the pattern's sections as a
 * strip (width = bars; current filled with the playhead, queued breathing), the current section
 * and NEXT in words, the fill countdown */
#define TL_Y 153
#define TL_H 10
#define TL_TEXT_Y 164
static void page_pattern(const bb_ui *u, const bb_ui_in *in, uint32_t now)
{
    uint16_t c = inst_c(in);
    uint32_t i, total = 0;
    int32_t x = 8, tx = 8;
    draw_browser(in, now);
    if (list_queued(in)) {
        draw_ring(207, 110, in->progress, c);
        u_text_r(232, 126, &AF_S, "NEXT BAR", c, BB_UI_BG);
    }
    for (i = 0; i < in->sections && i < BB_UI_SECTIONS; i++)
        total += in->sec_bars[i] ? in->sec_bars[i] : 1u;
    for (i = 0; i < in->sections && i < BB_UI_SECTIONS; i++) {   /* the sections: width = bars */
        int32_t w = (int32_t)((in->sec_bars[i] ? in->sec_bars[i] : 1u) * 224u / total);
        uint32_t cur = i == in->sec_cur, nx = i == in->sec_next;
        if (cur)
            u_rect(x, TL_Y, w - 2, TL_H, c);
        else if (nx)
            u_frame(x, TL_Y, w - 2, TL_H, breath_c(c, now), 2);
        else
            u_frame(x, TL_Y, w - 2, TL_H, BB_UI_FAINT, 1);
        if (cur && in->running)
            u_rect(x + (int32_t)in->sec_pos * (w - 3) / 255, TL_Y - 2, 2, TL_H + 4, BB_UI_TEXT);
        x += w;
    }
    if (in->sec_cur < in->sections && in->sec_cur < BB_UI_SECTIONS && in->sec_name[in->sec_cur])
        tx = u_text(8, TL_TEXT_Y, &AF_S, in->sec_name[in->sec_cur], c, BB_UI_BG) + 12;
    if (in->sec_next < in->sections && in->sec_next < BB_UI_SECTIONS && in->sec_name[in->sec_next]) {
        char b[24];
        uint32_t k = 0;
        const char *p = in->sec_name[in->sec_next];
        b[k++] = 'N'; b[k++] = 'E'; b[k++] = 'X'; b[k++] = 'T'; b[k++] = ' '; b[k++] = ' ';
        while (*p && k + 1u < sizeof b) b[k++] = *p++;
        b[k] = 0;
        u_text_fit(tx, TL_TEXT_Y, &AF_S, b, breath_c(c, now), BB_UI_BG, 170 - tx);
    }
    if (in->fill_beats) {                                        /* fill countdown, in beats */
        int32_t k;
        u_text(176, TL_TEXT_Y, &AF_S, "FILL", c, BB_UI_BG);
        for (k = 0; k < 4; k++)
            u_rbox(204 + 8 * k, TL_TEXT_Y + 5, 5, 5, 2, k < (int32_t)in->fill_beats ? c : BB_UI_FAINT, BB_UI_BG);
    } else if (in->fill) {
        u_text(176, TL_TEXT_Y, &AF_S, "FILL", BB_UI_TEXT, BB_UI_BG);
    }
    if (!BB_UI_BACKDROP) draw_mini(u, in, MINI34_X, MINI34_Y, 34, &MINI34_BOX);
}

#if BB_UI_ALL_PAGES
/* ---------------------------------------------------------------- FX: the echo machine */
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
static uint32_t echo_phase(const bb_ui_in *in, uint32_t now)   /* 0..7: dots travel at echo time */
{
    uint32_t t = in->echo_ms >= 20u ? in->echo_ms : 20u;
    return in->running ? (now % t) * 8u / t : 0u;
}
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
    for (i = 0; i < (int32_t)in->banks_fx && i < 8; i++) {      /* FX banks (EDIT = next) */
        int32_t x = 120 - 6 * (int32_t)in->banks_fx + 12 * i;
        u_rbox(x + 3, 184, 6, 6, 3, (uint32_t)i == in->bank_fx ? c : BB_UI_FAINT, BB_UI_BG);
    }
}

/* ---------------------------------------------------------------- KNOBS (SOUND, SAVE, EVOLVE, GLOBAL)
 * The four values as four meters standing on their own knob columns; the knob bar
 * below carries the labels and numbers. */
#define METER_Y0 56
#define METER_Y1 176
static void page_knobs(const bb_ui_in *in)
{
    uint16_t c = inst_c(in);
    uint32_t k;
    if (in->title)
        u_name(8, 28, &AF_M, in->title, BB_UI_TEXT, BB_UI_BG, 224);
    if (in->subtitle)                                            /* what EDIT does here, in grey */
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
/* ---------------------------------------------------------------- HELP (hold HOME) */
typedef struct { uint8_t kc, kc2; const char *mid, *act; } u_help_row;
#define NOKC 0xFFu
static const u_help_row HELP_STUDIO[] = {
    {KC_HOME, NOKC, 0, "Studio"}, {KC_ARP, NOKC, 0, "Play keys"}, {KC_SEQ, NOKC, 0, "Write steps"},
    {KC_SELECT, NOKC, 0, "Instrument"}, {KC_ALGO, NOKC, 0, "Pattern"}, {KC_PRESETS, NOKC, 0, "Kit"},
    {KC_KEYS, NOKC, 0, "Mute lanes, sections"}, {KC_REC, KC_HOME, "+", "All notes off"}, {0, 0, 0, 0}};
static const u_help_row HELP_PLAY[] = {
    {KC_KEYS, NOKC, 0, "Drums, kits, mutes"}, {KC_SEQ, NOKC, 0, "Write steps"}, {KC_EDIT, NOKC, 0, "Confirm"},
    {KC_SCL, NOKC, 0, "Swing / scale"}, {KC_REC, KC_KEYS, "+", "Accent"}, {KC_REC, KC_GLO, "+", "Tap tempo"},
    {KC_K14, NOKC, 0, "Group levels"}, {KC_REC, KC_HOME, "+", "All notes off"}, {0, 0, 0, 0}};
static const u_help_row HELP_STEPS[] = {
    {KC_KEYS, NOKC, 0, "Steps 1-16"}, {KC_ARP, NOKC, 0, "Play keys"}, {KC_OCTPM, NOKC, 0, "Step bank"},
    {KC_REC, KC_SEQ, "+", "Next bank"}, {KC_REC, KC_PRESETS, "+", "Voice"}, {KC_EDIT, NOKC, 0, "Step type"},
    {KC_REC, KC_HOME, "+", "All notes off"}, {0, 0, 0, 0}};
static const u_help_row HELP_BROWSE[] = {
    {KC_PRESETS, NOKC, 0, "Kit"}, {KC_ALGO, NOKC, 0, "Pattern"}, {KC_EDIT, NOKC, 0, "Confirm"},
    {KC_PLAY, NOKC, 0, "Start / stop"}, {KC_KEYS, NOKC, 0, "Quick pick"}, {KC_OCTPM, NOKC, 0, "List page"},
    {KC_HOME, NOKC, 0, "Studio"}, {0, 0, 0, 0}};
static const u_help_row HELP_FX[] = {
    {KC_EDIT, NOKC, 0, "Next effect"}, {KC_K14, NOKC, 0, "Effect controls"}, {KC_REC, KC_FX, "+", "Dub throw (hold)"},
    {KC_HOME, NOKC, 0, "Studio"}, {0, 0, 0, 0}};
static const char *const PAGE_WORD[BB_UI_PAGES] = {"STUDIO", "PLAY", "STEPS", "PLAY", "KIT", "PATTERN", "FX", "", ""};
static void page_help(const bb_ui_in *in, uint32_t page)
{
    const u_help_row *r = page == BB_UI_STUDIO ? HELP_STUDIO : page == BB_UI_PLAY_DRUMS || page == BB_UI_PLAY_DILLO ?
                        HELP_PLAY : page == BB_UI_STEPS ? HELP_STEPS : page == BB_UI_FX ? HELP_FX : HELP_BROWSE;
    int32_t y = 34, x;
    char t[24] = "HELP \xB7 ";
    uint32_t n = 7, i;
    const char *w = page < BB_UI_PAGES && PAGE_WORD[page][0] ? PAGE_WORD[page] : in->page_name ? in->page_name : "";
    for (i = 0; w[i] && n < sizeof t - 1u; i++)
        t[n++] = w[i];
    t[n] = 0;
    u_text(8, STATUS_TXT_Y, &AF_S, t, BB_UI_TEXT, BB_UI_BG);
    u_rect(8, 23, 224, 1, BB_UI_FAINT);
    for (; r->act; r++, y += 20) {
        if (!u_vis(0, y, 240, 20))
            continue;
        x = cv_keycap(8 - g_cx, y - g_cy, r->kc, BB_UI_TEXT, BB_UI_BG, BB_UI_BG) + g_cx;
        if (r->kc2 != NOKC) {
            x = u_text(x + 4, y - 1, &AF_S, r->mid, BB_UI_GREY, BB_UI_BG);
            cv_keycap(x + 4 - g_cx, y - g_cy, r->kc2, BB_UI_TEXT, BB_UI_BG, BB_UI_BG);
        }
        u_text(120, y - 1, &AF_S, r->act, BB_UI_GREY, BB_UI_BG);
    }
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
static uint32_t pop_on(const bb_ui *u, const bb_ui_in *in)
{
    return u->pop_t && in->now_ms - (u->pop_t & ~1u) < BB_UI_POPUP_MS ? u->pop_k : BB_UI_NO;
}
static uint32_t has_keys(uint32_t page) { return page != BB_UI_PLAY_DRUMS; }   /* v3 PLAY: lanes + master instead */
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
static int backdrop_on(uint32_t page) { return BB_UI_BACKDROP && page != BB_UI_ATTRACT && page != BB_UI_PAGES; }
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
    switch (page) {
    case BB_UI_PLAY_DRUMS: page_play_drums(u, in, now); break;
    case BB_UI_PLAY_DILLO: page_dillo(u, in); break;
    case BB_UI_KIT: page_kit(u, in, now); break;
    case BB_UI_PATTERN: page_pattern(u, in, now); break;
#if BB_UI_HOME
    case BB_UI_STUDIO: page_home(u, in, now); break;
#else
#if BB_UI_ALL_PAGES
    case BB_UI_STUDIO: page_studio(u, in, now); break;
#endif
#endif
#if BB_UI_ALL_PAGES
    case BB_UI_STEPS: page_steps(in); break;
    case BB_UI_FX: page_fx(in, now); break;
    default: page_knobs(in); break;
#else
    default: break;
#endif
    }
    if (has_keys(page))
        draw_keys(in, now);
    draw_knobs(in, pk);
    BB_UI_HOOK_LAYER(1);
    if (pk != BB_UI_NO && page != BB_UI_KNOBS)
        draw_popup(in, pk);
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
       in->notice ? h_u(h_s(H0, in->notice), inst_c(in)) : 0u);
    el(l, 150, 6, 36, 12, h_u(H0, (uint32_t)in->running << 8 | in->beat));
#if BB_UI_ALL_PAGES
    el(l, BD_X, BD_Y, (BD_X + BD_N > 240 ? 240 : BD_X + BD_N) - BD_X, (BD_Y + BD_N > BD_Y1 ? BD_Y1 : BD_Y + BD_N) - BD_Y,
       backdrop_hash(u, in, eff_page(u, in)));
#endif
}
static void els_frame_lo(els_t *l, const bb_ui *u, const bb_ui_in *in, uint32_t now, uint32_t page)
{
    uint32_t k, pk = pop_on(u, in);
    for (k = 0; k < 4u; k++)
        el(l, pop_x(k), POP_Y, POP_W, POP_H, pk == k && page != BB_UI_KNOBS ? h_u(knob_hash(in, k, 9), 1) : 0u);
    for (k = 0; k < 4u; k++)
        el(l, KNOB_X(k) - 2, 199, 56, 41, knob_hash(in, k, pk));
    el(l, 0, KEYS_Y, 240, 15, has_keys(page) ? keys_hash(in, now) : 0u);
    el(l, 0, 0, 148, 23, h_u(h_s(h_s(H0, in->page_name), in->instr_name), (uint32_t)in->instr << 8 | in->accent));
    el(l, 186, 0, 54, 23, h_u(h_u(H0, in->bpm_x10), (uint32_t)in->running << 8 |
                             (!in->running && (in->queued & BB_UI_Q_START) ? 1u + breath_q(now) : 0u)));
}
static void el_mini(els_t *l, const bb_ui *u, const bb_ui_in *in, const clip_t *b)
{
    el(l, b->x0, b->y0, b->x1 - b->x0, b->y1 - b->y0, BB_UI_BACKDROP ? 0u : mini_hash(u, in));
}

static void els_page(els_t *l, const bb_ui *u, const bb_ui_in *in, uint32_t page)
{
    uint32_t now = in->now_ms, i, h, g;
#if BB_UI_ALL_PAGES
    uint32_t k;
#endif
    int32_t head = in->running ? (int32_t)in->step : -1;
    l->n = 0;
    if (page == BB_UI_PAGES) {                                  /* help: one static picture */
        el(l, 0, 0, 240, 240, h_u(H0, in->page));
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
    switch (page) {
#if BB_UI_HOME
    case BB_UI_STUDIO:
        el(l, 0, 24, 240, 25, h_u(H0, 1u));
        for (i = 0; i < 2u; i++) {
            const bb_ui_lane *ln = &in->lane[i];
            h = h_s(h_s(h_s(h_s(H0, ln->name), ln->part), ln->pattern), ln->next);
            h = h_u(h, ln->flags);
            el(l, 0, 49 + 41 * (int32_t)i, 240, 41, h);
        }
        {
            const bb_ui_lane *ln = &in->lane[2];
            h = h_s(h_s(h_s(h_s(H0, ln->name), ln->part), ln->pattern), ln->next);
            el(l, 0, 131, 240, 48, h_u(h, ln->flags));
        }
        el(l, 0, 179, 240, 20, h_u(H0, 2u));
        el_mini(l, u, in, &MINI34_BOX);
        break;
#endif
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
    case BB_UI_KIT:
    case BB_UI_PATTERN: {
        uint32_t rows = list_rows(in), first = list_first(in), qbit = list_queued(in);
        int32_t ly = list_y(in);
        el(l, 0, 24, 240, list_end(in) + 3 - 24, h_u(h_s(H0, in->title), rows | first << 8 | (uint32_t)in->instr << 16));
        el(l, 0, 24, 240, ly - 24, h_s(H0, in->subtitle));
        for (i = 0; i < BB_UI_LIST; i++) {
            uint32_t idx = first + i, q = qbit && idx == in->queued_item;
            if (i >= rows) {
                el(l, 0, 0, 0, 0, 0);                            /* (the list keeps its length) */
                continue;
            }
            h = h_s(H0, idx < in->items ? in->item[idx] : 0);
            el(l, 0, ly + LIST_ROW * (int32_t)i, 176, LIST_ROW,
               h_u(h, (idx == in->cursor) | (idx == in->playing_item) << 1 | q << 2 | (q ? breath_q(now) << 4 : 0u) |
                      (uint32_t)in->instr << 9));
        }
        el(l, 176, 92, 64, 50, h_u(H0, qbit ? 1u + (in->progress >> 3) :
                                       in->loading && page == BB_UI_KIT ? 0x100u + breath_q(now) : 0u) ^ in->instr);
        if (page == BB_UI_PATTERN) {
            h = h_u(H0, (uint32_t)in->sections << 16 | (uint32_t)in->sec_cur << 8 | in->sec_next);
            for (i = 0; i < in->sections && i < BB_UI_SECTIONS; i++)
                h = h_u(h_s(h, in->sec_name[i]), in->sec_bars[i]);
            h = h_u(h, in->running ? 1u + ((uint32_t)in->sec_pos >> 2) : 0u);   /* the playhead tick shows only while running */
            h = h_u(h, (uint32_t)in->fill_beats << 8 | in->fill | (uint32_t)in->instr << 16);
            el(l, 0, TL_Y - 3, 240, 28, h_u(h, in->sec_next != BB_UI_NO ? breath_q(now) : 0u));
        } else {
            el(l, 0, 0, 0, 0, 0);
        }
        el_mini(l, u, in, &MINI34_BOX);
        break;
    }
#if BB_UI_ALL_PAGES
    case BB_UI_FX:
        el(l, 24, 80, 192, 32, h_u(h_u(H0, in->repeats), echo_phase(in, now) << 8 | in->instr));
        el(l, 0, 40, 240, 156, h_u(H0, (uint32_t)in->heads | in->instr << 8 | (uint32_t)in->banks_fx << 16 |
                                       (uint32_t)in->bank_fx << 24));
        break;
    default:
        for (k = 0; k < 4u; k++)
            el(l, KNOB_X(k), METER_Y0, 50, METER_Y1 - METER_Y0, knob_hash(in, k, 9));
        el(l, 0, 24, 240, METER_Y0 - 24, h_s(h_s(H0, in->title), in->subtitle));   /* title + EDIT line */
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
    if (page != u->page && !(ev & BB_EV_KNOB) && in->knob_turning >= 4u)
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
