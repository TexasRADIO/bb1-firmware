/* SPDX-License-Identifier: GPL-3.0-only
 * L3 fix A gate (TIMING-OPTIONS-R9.9.md): kit A's metal lanes (CB, CH, OH, CY + the shared bank) rendered by the
 * firmware's single-pass capture (x0x/metal_capture.inc, via the compact bb_metal_state), merged per sample in
 * CB -> CH -> OH -> CY order after the other lanes, are BIT-IDENTICAL to the released full render: every dry/rev/dly
 * float, the complete drum808_t and the gg_state. Host only.
 * Coverage: dense hits; the last-metal choke/retrigger boundary (#4 6485803291); random choke ramps; every 97 spans
 * new per-sound LEVEL/REV/DLY/DRIVE/DIST on the metal sounds (zero and non-zero sends) and moving/muted group faders
 * (#4 6488119452). Mutations that must fail: -DMUTATE_PRESUM (metal pre-summed, added once; #4 6483954898) and
 * -DMUTATE_STATE (a kit field changed after the split; #4 6485319810). */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "x0x/group_renderer.c"
enum { MAXN = 128 };   /* the CPU1 job maximum (bb_rich_worker_begin) */
static const int METAL[4] = { L_CB, L_CH, L_OH, L_CY };
static const int METAL_TRACK[4] = { D8_CB, D8_CH, D8_OH, D8_CY };
static uint32_t rng = 0x8085u;
static uint32_t rnd(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }
static int same(const void *a, const void *b, size_t n) { return !memcmp(a, b, n); }
int main(void)
{
    static drum808_t kit;
    static bb_metal_terms terms;
    unsigned checks = 0, failures = 0, metal_spans = 0, param_changes = 0;
    drum808_prepare_curves();
    drum808_init(&kit);
    gg_state groups; gg_init(&groups);
    for (unsigned step = 0; step < 6000; step++) {
        if (step < 2) {
            if (step == 0) { drum808_trigger(&kit, D8_CH, 1.0f); kit.lane[L_CH].cstep = -0.25f; }
            else drum808_trigger(&kit, D8_CH, 1.0f);
        } else {
            if (step % 97 == 0) {   /* new metal sound settings and group faders */
                const int P[5] = { D8P_LEVEL, D8P_REV, D8P_DLY, D8P_DRIVE, D8P_DIST };
                for (int k = 0; k < 4; k++) for (int p = 0; p < 5; p++) {
                    const x0x_param_t *d = drum808_param(METAL_TRACK[k], P[p]);
                    if (!d) continue;
                    int v = (rnd() % 4 == 0) ? 0 : (int)(rnd() % ((unsigned)d->max + 1));
                    drum808_set(&kit, METAL_TRACK[k], P[p], v);
                }
                for (unsigned g = 0; g < GG_COUNT; g++) gg_set(&groups, g, rnd() % 128, rnd() % 6 == 0);
                param_changes++;
            }
            if (rnd() % 3 == 0) for (int t = 0; t < D8_NUM; t++) if (rnd() % 4 == 0) drum808_trigger(&kit, t, (rnd() % 2) ? 1.0f : 0.6f);
            if (rnd() % 5 == 0) { int l = METAL[rnd() % 4]; if (kit.lane[l].cg > 0.0f) kit.lane[l].cstep = -(float)(1 + rnd() % 64) / 256.0f; }
        }
        int n = step < 2 ? 128 : 1 + (int)(rnd() % MAXN);
        float in_d[MAXN], in_r[MAXN], in_l[MAXN];
        for (int i = 0; i < n; i++) {
            in_d[i] = ((int)(rnd() % 2001) - 1000) * 1e-4f; in_r[i] = ((int)(rnd() % 2001) - 1000) * 1e-5f;
            in_l[i] = ((int)(rnd() % 2001) - 1000) * 1e-5f;
            if (rnd() % 64 == 0) in_d[i] = -0.0f;
        }
        /* full, released renderer */
        drum808_t full = kit; gg_state gf = groups;
        float fd[MAXN], fr[MAXN], fl[MAXN]; memcpy(fd, in_d, n * 4); memcpy(fr, in_r, n * 4); memcpy(fl, in_l, n * 4);
        bb_proposal_group_span(&full, &gf, fd, fr, fl, n);
        /* split: CPU1's capture on the compact state; CPU0's other lanes with metal masked; merge */
        static bb_metal_state ms; gg_state gc = groups;
        bb_metal_extract(&kit, &ms);
        int r = bb_metal_capture_state(&ms, &gc, &terms, n);
        metal_spans += r == 1;
        drum808_t oth = kit; gg_state go = groups; float saved[4];
        for (int k = 0; k < 4; k++) { saved[k] = oth.lane[METAL[k]].cg; oth.lane[METAL[k]].cg = 0.0f; }
        float cd[MAXN], cr[MAXN], cl[MAXN]; memcpy(cd, in_d, n * 4); memcpy(cr, in_r, n * 4); memcpy(cl, in_l, n * 4);
        bb_proposal_group_span(&oth, &go, cd, cr, cl, n);
        for (int k = 0; k < 4; k++) if (oth.lane[METAL[k]].cg == 0.0f) oth.lane[METAL[k]].cg = saved[k];
        if (r == 1) {
#ifndef MUTATE_PRESUM
            bb_metal_merge(&terms, cd, cr, cl);
#else
            for (int i = 0; i < n; i++) { float pd = 0, pr = 0, pl = 0;
                for (int k = 0; k < 4; k++) if ((terms.pres[k][i >> 5] >> (i & 31)) & 1u) { float v = terms.v[k][i]; pd += v;
                    if ((terms.seg_add[0] >> k) & 1u) pr += v * terms.ra[k]; if ((terms.seg_add[0] >> (k + 4)) & 1u) pl += v * terms.da[k]; }
                cd[i] += pd; cr[i] += pr; cl[i] += pl; }
#endif
            bb_metal_apply(&oth, &ms);
        }
#ifdef MUTATE_STATE
        oth.vol += 0.125f;
#endif
        checks++;
        if (r < 0 || !same(fd, cd, n * 4) || !same(fr, cr, n * 4) || !same(fl, cl, n * 4) ||
            !same(&oth, &full, sizeof full) || !same(&go, &gf, sizeof gf) || (r == 1 && !same(&gc, &gf, sizeof gf))) {
            if (failures < 5) printf("differs at step %u (n=%d)\n", step, n);
            failures++;
        }
        kit = full; groups = gf;
    }
    printf("fix A capture gate: %u checks, %u spans with metal, %u parameter/fader changes, %u failures\n",
           checks, metal_spans, param_changes, failures);
    return failures != 0;
}
