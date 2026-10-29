/* SPDX-License-Identifier: GPL-3.0-only
 * L3 fix A: the core-1 job container (hal/bb_c1_metal_job.h) gives exactly the direct result. For random calls
 * (spans, kit A metal hits, faders, settings), prepare -> run -> commit leaves the same dry/rev/dly buses, the same
 * complete drum808_t and the same gg_state as bb_metal_extract -> bb_metal_capture_call -> bb_metal_merge ->
 * bb_metal_apply. Tampering is refused with nothing written: a changed input byte after prepare (run refuses), a
 * changed output byte after run (commit refuses), a wrong ticket, a broken guard, a NaN term. Host only. */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "x0x/group_renderer.c"
#include "bb_c1_metal_job.h"
static const int METAL_TRACK[4] = { D8_CB, D8_CH, D8_OH, D8_CY };
static uint32_t rng = 0x10b5u;
static uint32_t rnd(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }
static bb_c1_metal_job job;
int main(void)
{
    static drum808_t kit; static bb_metal_state ms; static bb_metal_terms terms;
    unsigned checks = 0, failures = 0, tamper = 0, calls = 0;
    drum808_prepare_curves(); drum808_init(&kit);
    gg_state groups; gg_init(&groups);
    for (unsigned call = 0; call < 3000; call++) {
        if (call % 53 == 0) { kit.choke = (int32_t)(rnd() % 3);
            for (unsigned g = 0; g < GG_COUNT; g++) gg_set(&groups, g, rnd() % 128, rnd() % 6 == 0); }
        if (rnd() % 3 == 0) for (int t = 0; t < D8_NUM; t++) if (rnd() % 4 == 0) drum808_trigger(&kit, t, 1.0f);
        uint16_t sf[BB_METAL_SPANS]; unsigned nsp = 1 + rnd() % BB_METAL_SPANS, total = 0;
        for (unsigned s = 0; s < nsp; s++) { sf[s] = (uint16_t)(1 + rnd() % 32); total += sf[s]; }
        bb_metal_hits h; memset(&h, 0, sizeof h); h.n = (uint8_t)(rnd() % 6);
        for (unsigned j = 0, at = 0; j < h.n; j++) { at += rnd() % 3 ? 0 : rnd() % 8; if (at >= total) at = total - 1;
            h.at[j] = (uint8_t)at; h.k[j] = (uint8_t)(rnd() % 4); h.vel[j] = (float)(1 + rnd() % 100) / 100.0f; }
        float in[3][BB_METAL_MAX]; for (int b = 0; b < 3; b++) for (unsigned i = 0; i < total; i++) in[b][i] = (float)(rnd() % 1000) * 1e-4f;
        /* direct */
        drum808_t kd = kit; gg_state gd = groups; float d[3][BB_METAL_MAX]; memcpy(d, in, sizeof d);
        bb_metal_extract(&kd, &ms);
        int r = bb_metal_capture_call(&ms, &gd, &terms, sf, (int)nsp, &h);
        if (r < 0) continue;
        if (r == 1) bb_metal_merge(&terms, d[0], d[1], d[2]);
        bb_metal_apply(&kd, &ms);
        /* through the job */
        drum808_t kj = kit; gg_state gj = groups; float o[3][BB_METAL_MAX]; memcpy(o, in, sizeof o);
        uint32_t ticket = 1000 + call, res = 0;
        int ok = bb_c1_metal_prepare(&job, &kj, &gj, sf, nsp, &h, ticket) && bb_c1_metal_run(&job, ticket, &res) &&
                 bb_c1_metal_commit(&job, ticket, res, &kj, &gj, o[0], o[1], o[2]);
        checks++; calls++;
        if (!ok || memcmp(d, o, sizeof d) || memcmp(&kd, &kj, sizeof kd) || memcmp(&gd, &gj, sizeof gd)) {
            if (failures < 5) printf("job differs at call %u\n", call); failures++; }
        /* tampering: each must refuse and leave kit, faders and buses untouched */
        if (call % 10 == 0) {
            drum808_t kt = kit; gg_state gt = groups; float t3[3][BB_METAL_MAX]; memcpy(t3, in, sizeof t3);
            int bad = 0;
            bb_c1_metal_prepare(&job, &kt, &gt, sf, nsp, &h, ticket);
            ((uint8_t *)&job.state)[rnd() % sizeof job.state] ^= 1; bad += bb_c1_metal_run(&job, ticket, &res);
            bb_c1_metal_prepare(&job, &kt, &gt, sf, nsp, &h, ticket); bad += bb_c1_metal_run(&job, ticket + 1, &res);
            bb_c1_metal_prepare(&job, &kt, &gt, sf, nsp, &h, ticket); job.end_guard ^= 1; bad += bb_c1_metal_run(&job, ticket, &res);
            if (bb_c1_metal_prepare(&job, &kt, &gt, sf, nsp, &h, ticket) && bb_c1_metal_run(&job, ticket, &res)) {
                job.terms.v[rnd() % 4][rnd() % BB_METAL_MAX] += 1.0f;
                bad += bb_c1_metal_commit(&job, ticket, res, &kt, &gt, t3[0], t3[1], t3[2]); }
            if (bb_c1_metal_prepare(&job, &kt, &gt, sf, nsp, &h, ticket) && bb_c1_metal_run(&job, ticket, &res) && job.result == 1) {
                job.terms.v[0][0] = __builtin_nanf(""); res = bb_c1_metal_output_check(&job);
                bad += bb_c1_metal_commit(&job, ticket, res, &kt, &gt, t3[0], t3[1], t3[2]); }
            checks++; tamper++;
            if (bad || memcmp(&kt, &kit, sizeof kt) || memcmp(&gt, &groups, sizeof gt) || memcmp(t3, in, sizeof t3)) {
                if (failures < 5) printf("tamper accepted at call %u\n", call); failures++; }
        }
        kit = kd; groups = gd;
        (void)METAL_TRACK;
    }
    printf("fix A job: %u calls, %u tamper rounds, %zu B job, %u checks, %u failures\n", calls, tamper, sizeof(bb_c1_metal_job), checks, failures);
    return failures != 0;
}
