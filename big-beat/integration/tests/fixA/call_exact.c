/* SPDX-License-Identifier: GPL-3.0-only
 * L3 fix A gate for a whole CPU1 call: up to BB_METAL_SPANS event spans with kit A's hits anywhere in the call:
 * between spans (audio_hit) and inside them (the rich phrase renderer's primary and delayed strikes, which split
 * its render at each strike and at every step boundary; bb_audio_phrase_render.c, #4 6490068347).
 * Reference: the released renderer over the same pieces the firmware renders, every hit applied in order with
 * drum808_trigger at its frame, plus extra cuts where nothing happens (empty step boundaries). Split: CPU1 runs
 * bb_metal_capture_call on the compact state (it splits only at span starts and its own metal hits); CPU0 renders
 * its other lanes with metal masked over the reference pieces and takes the non-metal hits; merge; apply.
 * Compares every dry/rev/dly float, the complete drum808_t and both gg_state copies. Covers equal-frame hits,
 * CH/OH chokes under choke modes 0/1/2, lanes that end and restart inside a call, moving/muted faders, per-call
 * settings changes, and the cap: a call with more than BB_METAL_HITS metal hits must be refused before anything
 * changes (state and faders untouched). Must fail: -DMUTATE_SEGFLAGS (first piece's add masks for the whole call),
 * -DMUTATE_HITSHIFT (a metal hit one sample late), -DMUTATE_EQUALORDER (equal-frame metal hits reversed). Host only. */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "x0x/group_renderer.c"
static const int METAL[4] = { L_CB, L_CH, L_OH, L_CY };
static const int METAL_TRACK[4] = { D8_CB, D8_CH, D8_OH, D8_CY };
static uint32_t rng = 0xca11u;
static uint32_t rnd(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }
static int metal_track(int t) { for (int k = 0; k < 4; k++) if (METAL_TRACK[k] == t) return k; return -1; }
enum { MAXH = 40 };
typedef struct { uint16_t at; int track; float vel; } hit_t;
/* render [0,total) in pieces cut at span starts, every hit frame and the extra cuts; apply hits at each piece start */
static void pieces(drum808_t *d, gg_state *g, float f[3][BB_METAL_MAX], const uint16_t *sf, int nsp,
                   const hit_t *h, int nh, const uint8_t *cut, int metal_hits)
{
    int total = 0, j = 0; for (int s = 0; s < nsp; s++) total += sf[s];
    uint8_t edge[BB_METAL_MAX + 1] = {0};
    for (int s = 0, o = 0; s < nsp; o += sf[s], s++) edge[o] = 1;
    for (int i = 0; i < nh; i++) edge[h[i].at] = 1;
    for (int i = 0; i < total; i++) if (cut[i]) edge[i] = 1;
    for (int o = 0; o < total;) {
        for (; j < nh && h[j].at == o; j++) if (metal_hits || metal_track(h[j].track) < 0) drum808_trigger(d, h[j].track, h[j].vel);
        int e = o + 1; while (e < total && !edge[e]) e++;
        bb_proposal_group_span(d, g, f[0] + o, f[1] + o, f[2] + o, e - o);
        o = e;
    }
}
int main(void)
{
    static drum808_t kit; static bb_metal_terms terms; static bb_metal_state ms, ms0;
    unsigned checks = 0, failures = 0, with_hits = 0, inside = 0, equal = 0, refused = 0, at_cap = 0, multi = 0;
    drum808_prepare_curves(); drum808_init(&kit);
    gg_state groups; gg_init(&groups);
    for (unsigned call = 0; call < 10000; call++) {
        if (call % 61 == 0) {   /* settings change only between calls (the A guard is a wiring item, #4 6490093611) */
            const int P[7] = { D8P_LEVEL, D8P_REV, D8P_DLY, D8P_DRIVE, D8P_DIST, D8P_TUNE, D8P_DECAY };
            kit.choke = (int32_t)(rnd() % 3);
            for (int k = 0; k < 4; k++) for (int p = 0; p < 7; p++) {
                const x0x_param_t *d = drum808_param(METAL_TRACK[k], P[p]); if (!d) continue;
                drum808_set(&kit, METAL_TRACK[k], P[p], (rnd() % 4 == 0) ? 0 : (int)(rnd() % ((unsigned)d->max + 1)));
            }
            for (unsigned g = 0; g < GG_COUNT; g++) gg_set(&groups, g, rnd() % 128, rnd() % 6 == 0);
        }
        uint16_t sf[BB_METAL_SPANS]; int nsp = 1 + (int)(rnd() % BB_METAL_SPANS), total = 0;
        for (int s = 0; s < nsp; s++) { int mx = BB_METAL_MAX - total - (nsp - s - 1); if (mx > 64) mx = 64;
            sf[s] = (uint16_t)(1 + rnd() % (unsigned)mx); total += sf[s]; }
        multi += nsp > 1;
        /* hits: 0..9 usually; sometimes exactly 12, 13 or 14 metal hits; equal frames often */
        hit_t h[MAXH]; int nh = 0, want_metal = -1;
        unsigned mode = rnd() % 10;
        if (mode == 0) want_metal = 12 + (int)(rnd() % 3);
        int count = want_metal >= 0 ? want_metal + (int)(rnd() % 4) : (int)(rnd() % 10);
        for (int q = 0; q < count && nh < MAXH; q++) {
            hit_t x; x.at = (uint16_t)(nh && rnd() % 3 == 0 ? h[nh - 1].at : rnd() % (unsigned)total);
            x.track = (want_metal >= 0 ? q < want_metal : rnd() % 2) ? METAL_TRACK[rnd() % 4] : (int)(rnd() % D8_NUM);
            if (want_metal >= 0 && q >= want_metal) while (metal_track(x.track) >= 0) x.track = (int)(rnd() % D8_NUM);
            x.vel = (rnd() % 8 == 0) ? 1.0f : (float)(1 + rnd() % 100) / 100.0f;
            int p = nh++; while (p > 0 && h[p - 1].at > x.at) { h[p] = h[p - 1]; p--; } h[p] = x;   /* stable by frame */
        }
        uint8_t cut[BB_METAL_MAX] = {0}; for (int c = (int)(rnd() % 4); c > 0; c--) cut[rnd() % (unsigned)total] = 1;
        float in[3][BB_METAL_MAX];
        for (int b = 0; b < 3; b++) for (int i = 0; i < total; i++) in[b][i] = ((int)(rnd() % 2001) - 1000) * (b ? 1e-5f : 1e-4f);
        /* reference */
        drum808_t full = kit; gg_state gf = groups; float f[3][BB_METAL_MAX]; memcpy(f, in, sizeof f);
        pieces(&full, &gf, f, sf, nsp, h, nh, cut, 1);
        /* CPU1 input */
        bb_metal_hits mh; int nm = 0, span_start[BB_METAL_MAX] = {0}, has_inside = 0, has_equal = 0;
        for (int s = 0, o = 0; s < nsp; o += sf[s], s++) span_start[o] = 1;
        memset(&mh, 0, sizeof mh);
        for (int j = 0; j < nh; j++) { int k = metal_track(h[j].track); if (k < 0) continue;
            if (nm < BB_METAL_HITS) { mh.at[nm] = (uint8_t)h[j].at; mh.k[nm] = (uint8_t)k; mh.vel[nm] = h[j].vel; }
            if (nm && h[j].at == mh.at[nm < BB_METAL_HITS ? nm - 1 : BB_METAL_HITS - 1]) has_equal = 1;
            if (!span_start[h[j].at]) has_inside = 1;
            nm++; }
        mh.n = (uint8_t)(nm < 255 ? nm : 255);
#ifdef MUTATE_HITSHIFT
        if (nm && nm <= BB_METAL_HITS && mh.at[0] + 1 < total && (nm == 1 || mh.at[1] > mh.at[0] + 1)) mh.at[0]++;
#endif
#ifdef MUTATE_EQUALORDER
        for (int a = 0; a + 1 < nm && a + 1 < BB_METAL_HITS; a++) if (mh.at[a] == mh.at[a + 1]) {
            uint8_t k = mh.k[a]; float v = mh.vel[a]; mh.k[a] = mh.k[a + 1]; mh.vel[a] = mh.vel[a + 1]; mh.k[a + 1] = k; mh.vel[a + 1] = v; a++; }
#endif
        gg_state gc = groups; bb_metal_extract(&kit, &ms); ms0 = ms;
        int r = bb_metal_capture_call(&ms, &gc, &terms, sf, nsp, &mh);
        if (r < 0) {   /* must be exactly the over-cap calls, and nothing may have changed */
            refused++;
            if (nm <= BB_METAL_HITS || memcmp(&ms, &ms0, sizeof ms) || memcmp(&gc, &groups, sizeof gc)) {
                if (failures < 5) printf("bad refusal at call %u (metal hits %d)\n", call, nm); failures++; }
            kit = full; groups = gf; continue;
        }
        if (nm > BB_METAL_HITS) { if (failures < 5) printf("over-cap call %u accepted (%d)\n", call, nm); failures++; }
        at_cap += nm == BB_METAL_HITS;
        drum808_t oth = kit; gg_state go = groups; float c[3][BB_METAL_MAX]; memcpy(c, in, sizeof c);
        for (int k = 0; k < 4; k++) oth.lane[METAL[k]].cg = 0.0f;
        pieces(&oth, &go, c, sf, nsp, h, nh, cut, 0);
#ifdef MUTATE_SEGFLAGS
        for (int s = 1; s < terms.nseg; s++) terms.seg_add[s] = terms.seg_add[0];
#endif
        if (r == 1) bb_metal_merge(&terms, c[0], c[1], c[2]);
        bb_metal_apply(&oth, &ms);
        checks++; with_hits += nm > 0; inside += has_inside; equal += has_equal;
        if (memcmp(f, c, sizeof f) || memcmp(&oth, &full, sizeof full) || memcmp(&go, &gf, sizeof gf) || memcmp(&gc, &gf, sizeof gf)) {
            if (failures < 5) printf("differs at call %u (spans %d, hits %d, metal %d)\n", call, nsp, nh, nm);
            failures++;
        }
        kit = full; groups = gf;
    }
    printf("fix A call gate: %u calls compared (%u multi-span, %u with metal hits, %u inside a span, %u equal-frame, %u at the %d-hit cap), %u over-cap refused, %u failures\n",
           checks, multi, with_hits, inside, equal, at_cap, BB_METAL_HITS, refused, failures);
    return failures != 0;
}
