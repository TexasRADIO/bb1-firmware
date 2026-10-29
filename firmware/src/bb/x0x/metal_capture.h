/* SPDX-License-Identifier: GPL-3.0-only
 * L3 fix A: types and entry points of the kit A metal capture (metal_capture.inc, built into group_renderer.c),
 * shared with the core-1 job container (hal/bb_c1_metal_job.h). */
#ifndef BB_METAL_CAPTURE_H
#define BB_METAL_CAPTURE_H
#include <stdint.h>
#include "drum808.h"
#include "group_gain.h"
enum { BB_METAL_LANES = 4, BB_METAL_MAX = 128, BB_METAL_SPANS = 4, BB_METAL_HITS = 12,
       BB_METAL_SEGS = BB_METAL_SPANS + BB_METAL_HITS };   /* job maximum; BB_P1B_SEGMENTS; the per-call hit cap */
/* One float per lane per sample: the lane's voiced value. The rev/dly terms are voiced*ra and voiced*da with ra/da
 * fixed for the call, so CPU0 forms them with the same single multiply the renderer does (#4 6486297976).
 * A call is up to BB_METAL_SPANS event spans, and kit A's hits land anywhere in it: between spans (audio_hit) and
 * inside them (the rich phrase renderer's primary and delayed strikes, #4 6490068347). A lane can stop and restart
 * inside a call and its fast/full add mode can change, so values carry per-sample presence bits and each piece
 * (split at span starts and hit offsets) its add masks. */
typedef struct {
    float v[BB_METAL_LANES][BB_METAL_MAX];
    float ra[BB_METAL_LANES], da[BB_METAL_LANES];
    uint32_t pres[BB_METAL_LANES][BB_METAL_MAX/32];   /* bit i: lane k added a value at sample i */
    uint8_t seg_at[BB_METAL_SEGS];    /* first sample of each piece */
    uint8_t seg_add[BB_METAL_SEGS];   /* bits 0-3: add lane k's rev term; bits 4-7: its dly term
                                         (the fast path skips a zero send; the full path never does) */
    uint16_t frames, filled;
    uint8_t nseg, any;
} bb_metal_terms;
/* Kit A's metal hits for the call, in the order the firmware would apply them (equal frames keep their order). */
typedef struct {
    float vel[BB_METAL_HITS];
    uint8_t at[BB_METAL_HITS], k[BB_METAL_HITS];
    uint8_t n;
} bb_metal_hits;
/* Route 2 (#4 6487490002): only the kit A state the metal capture reads or writes, so it travels in the CPU1 job
 * without a whole drum808_t. Lane alive flags are the voices' own .active fields (bb_context_active_offset). */
typedef struct {
    d8_bank_t bank;
    d8_cb_t cb;
    d8_hat_t ch, oh;
    d8_cy_t cy;
    d8_lane_t lane[BB_METAL_LANES];
    float level[BB_METAL_LANES], rev[BB_METAL_LANES], dly[BB_METAL_LANES];   /* potv[snd][LEVEL/REV/DLY] */
    d8_shp_t shp[BB_METAL_LANES];
    uint8_t dist[BB_METAL_LANES], snd[BB_METAL_LANES];                         /* pot[snd][DIST], the lane's sound */
    float vol;
    /* for hits inside a CPU1 call (bb_metal_trigger): potv[snd][TUNE/DECAY], pot[snd][TUNE], vel_depth, choke */
    float tune[BB_METAL_LANES], decay[BB_METAL_LANES];
    uint8_t pot_tune[BB_METAL_LANES];
    float vel_depth;
    int32_t choke;
} bb_metal_state;
void bb_metal_extract(const drum808_t *d, bb_metal_state *m);
void bb_metal_apply(drum808_t *d, const bb_metal_state *m);
int bb_metal_trigger(bb_metal_state *m, int k, float vel);
int bb_metal_capture_state(bb_metal_state *ms, gg_state *groups, bb_metal_terms *t, int n);
int bb_metal_capture_call(bb_metal_state *ms, gg_state *groups, bb_metal_terms *t,
                          const uint16_t *span_frames, int nspans, const bb_metal_hits *h);
void bb_metal_merge(const bb_metal_terms *t, float *dry, float *rev, float *dly);
int bb_metal_ready(const drum808_t *d);
uint32_t bb_metal_params(const drum808_t *d);
void bb_metal_park(drum808_t *d, d8_lane_t saved[BB_METAL_LANES]);
void bb_metal_unpark(drum808_t *d, const d8_lane_t saved[BB_METAL_LANES]);
#endif
