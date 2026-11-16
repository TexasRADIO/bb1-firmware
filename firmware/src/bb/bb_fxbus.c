/* SPDX-License-Identifier: GPL-3.0-only
 * BB1 effects chain. Adapted from:
 *   - OG Synth X0X fxbus.c (GPL-3.0-only, (C) Charles Vestal)
 *   - BB1 bb_echo.c (GPL-3.0-only, (C) Big Beat / Hügelton Instruments)
 *
 * ROOM reverb: block-float 8-line FDN from X0X.
 * Delay: int16 ring with DIGI/TAPE modes from X0X, three-head + motion from BB1.
 * Distortion: 7 types from X0X d9_shape.
 * Compressor: one-knob glue from X0X.
 *
 * Compile with -Oz for BB1's code budget.  */
#include "bb_fxbus.h"

/* ── Helpers ────────────────────────────────────────────────────────── */

static inline float fxabs(float x) { return x < 0 ? -x : x; }

static float fx_tanh(float x) {
    float ax = fxabs(x);
    if (ax >= 4.0f) return x < 0 ? -1.0f : 1.0f;
    float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

static inline float fx_psin(uint32_t ph) {
    /* parabolic sine from phase accumulator */
    int32_t s = (int32_t)(ph >> 16);
    float x = (float)s * (1.0f / 32768.0f);
    return x * (2.0f - fxabs(x));
}

static inline float fx_log2(float x) {
    union { float f; uint32_t u; } v = { .f = x };
    float e = (float)((int32_t)(v.u >> 23) - 127);
    v.u = (v.u & 0x007fffff) | 0x3f800000;
    return e + (v.f - 1.0f) * (2.0f - 0.33333f * (v.f - 1.0f));
}

static inline float fx_exp2(float x) {
    int32_t i = (int32_t)x;
    float f = x - (float)i;
    if (f < 0) { f += 1.0f; i--; }
    float p = 1.0f + f * (0.6931472f + f * (0.2402265f + f * 0.0558f));
    union { float fv; uint32_t u; } v;
    v.u = (uint32_t)(i + 127) << 23;
    return v.fv * p;
}

static inline float fx_db2lin(float db) { return fx_exp2(db * (1.0f / 6.0206f)); }

/* ── ROOM reverb ────────────────────────────────────────────────────── */

static const uint16_t rm_lengths[BB_RM_N] = {
    BB_RM_L0, BB_RM_L1, BB_RM_L2, BB_RM_L3
#if !BB_RM_LITE
    , BB_RM_L4, BB_RM_L5, BB_RM_L6, BB_RM_L7
#endif
};

static float fx_pow2(int e) {  /* 2^e for -126 <= e <= 127 */
    union { float f; uint32_t u; } v = { .u = (uint32_t)(e + 127) << 23 };
    return v.f;
}

static void rm_encode(bb_room_t *r, int line) {
    uint16_t blk = r->wblk[line], base = r->base[line];
    const float *src = r->stage[line];
    float peak = 0;
    for (int i = 0; i < BB_RM_BLK; i++) {
        float a = fxabs(src[i]);
        if (a > peak) peak = a;
    }
    /* Block floating point: e is the smallest power of two at or above the
     * block peak, and the mantissas are src * 127 / 2^e. */
    int e = -60;
    if (peak > 0) {
        union { float f; uint32_t u; } v = { .f = peak };
        e = (int)((v.u >> 23) & 0xFF) - 126;  /* peak < 2^e */
        if (e < -60) e = -60; if (e > 60) e = 60;
    }
    const float fwd = 127.0f * fx_pow2(-e);
    r->exp[base + blk] = (int8_t)e;
    int8_t *dst = &r->man[(base + blk) * BB_RM_BLK];
    for (int i = 0; i < BB_RM_BLK; i++) {
        int32_t q = (int32_t)(src[i] * fwd);
        if (q > 127) q = 127; if (q < -127) q = -127;
        dst[i] = (int8_t)q;
    }
    r->wblk[line] = (blk + 1 >= r->nblk[line]) ? 0 : blk + 1;
}

static void rm_decode(bb_room_t *r, int line) {
    uint16_t blk = r->wblk[line], base = r->base[line];
    const float scale = fx_pow2(r->exp[base + blk]) * (1.0f / 127.0f);
    const int8_t *src = &r->man[(base + blk) * BB_RM_BLK];
    for (int i = 0; i < BB_RM_BLK; i++)
        r->read[line][i] = (float)src[i] * scale;
}

static float rm_diffuse(bb_room_t *r, float x) {
#if BB_RM_LITE
    static const uint16_t dlen[2] = {211, 157};
    static const float dg[2] = {0.7f, 0.7f};
#else
    static const uint16_t dlen[4] = {211, 157, 397, 293};
    static const float dg[4] = {0.7f, 0.7f, 0.65f, 0.65f};
#endif
    uint16_t off = 0;
    for (int d = 0; d < BB_RM_DIF_N; d++) {
        int16_t pos = r->dif_pos[d];
        float rd = (float)r->dif[off + pos] * (1.0f / 2048.0f);
        float y = rd - dg[d] * x;
        int32_t wr = (int32_t)((x + dg[d] * y) * 2048.0f);
        if (wr > 32767) wr = 32767; if (wr < -32767) wr = -32767;
        r->dif[off + pos] = (int16_t)wr;
        r->dif_pos[d] = (pos + 1 >= (int16_t)dlen[d]) ? 0 : pos + 1;
        x = y;
        off += dlen[d];
    }
    return x;
}

static void rm_hadamard(float *x) {
    /* 4-lane Hadamard: unrolled butterfly + scaling in one pass */
#if BB_RM_LITE
    float a0 = x[0]+x[1], a1 = x[0]-x[1], a2 = x[2]+x[3], a3 = x[2]-x[3];
    x[0] = (a0+a2) * 0.5f;
    x[1] = (a1+a3) * 0.5f;
    x[2] = (a0-a2) * 0.5f;
    x[3] = (a1-a3) * 0.5f;
#else
    for (int s = 1; s < BB_RM_N; s <<= 1)
        for (int i = 0; i < BB_RM_N; i += s << 1)
            for (int j = i; j < i + s; j++) {
                float a = x[j], b = x[j+s];
                x[j] = a + b; x[j+s] = a - b;
            }
    for (int i = 0; i < BB_RM_N; i++) x[i] *= 0.35355339f;   /* 1/sqrt(8) */
#endif
}

static void __attribute__((noinline)) rm_process(bb_room_t *r, const float *in, float *out_l, float *out_r, int n) {
    float gain = r->out_gain;
    /* Idle detection: skip when the reverb has decayed to silence */
    if (r->quiet > 0) {
        int silent = 1;
        for (int i = 0; i < n && silent; i++)
            if (fxabs(in[i]) > 1e-8f) silent = 0;
        if (silent) { r->quiet--; return; }
        r->quiet = 0;
    }
    for (int i = 0; i < n; i++) {
        uint8_t pos = r->pos;
        if (pos == 0) {
            for (int l = 0; l < BB_RM_N; l++) {
                rm_encode(r, l);
                rm_decode(r, l);
            }
        }
        float x = rm_diffuse(r, in[i]);
        float lanes[BB_RM_N];
        float lo = 0, ro = 0;
        for (int l = 0; l < BB_RM_N; l++) {
            float rv = r->read[l][pos];
            r->lp[l] = r->a[l] * r->lp[l] + rv * r->c[l];
            lanes[l] = r->lp[l];
            /* Accumulate L/R taps in the same pass */
            if (l & 1) ro += rv; else lo += rv;
        }
        rm_hadamard(lanes);
        for (int l = 0; l < BB_RM_N; l++)
            r->stage[l][pos] = lanes[l] + x;
        out_l[i] += lo * gain;
        out_r[i] += ro * gain;
        r->pos = (pos + 1) & (BB_RM_BLK - 1);
    }
    /* Check if output is near zero — start quiet countdown (44100 samples ≈ 1s) */
    float peak = 0;
    for (int l = 0; l < BB_RM_N; l++) {
        for (int j = 0; j < BB_RM_BLK; j++) {
            float a = fxabs(r->stage[l][j]);
            if (a > peak) peak = a;
        }
    }
    if (peak < 1e-7f) r->quiet = 44100 / n;
}

static void rm_init(bb_room_t *r) {
    /* Zero everything first — caller is responsible for memset of parent struct */
    uint16_t base = 0;
    for (int l = 0; l < BB_RM_N; l++) {
        r->base[l] = base;
        r->nblk[l] = BB_RM_NB(rm_lengths[l]);
        base += r->nblk[l];
        r->c[l] = 0.3f;
        r->a[l] = 0.7f;
    }
    r->out_gain = 0.5f;
}

static void rm_set(bb_room_t *r, float decay, float tone) {
    /* decay 0..1 → damping coefficients; tone 0..1 → HF absorption */
    float d = 0.1f + 0.85f * decay;
    float t = 0.3f + 0.65f * tone;
    for (int l = 0; l < BB_RM_N; l++) {
        r->a[l] = t;
        r->c[l] = d * (1.0f - t);
    }
}

/* ── Delay with three heads and motion ──────────────────────────────── */

static uint32_t dl_rng(bb_delay_t *d) {
    uint32_t x = d->motion_rng;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    d->motion_rng = x;
    return x;
}

static void dl_motion_targets(bb_delay_t *d) {
    for (unsigned h = 0; h < 3; h++) {
        float u = (float)(dl_rng(d) & 65535u) * (1.0f / 65535.0f);
        d->head_target[h] = h == 2 ? u*0.15f - 0.15f : u*0.2f - 0.1f;
        u = (float)(dl_rng(d) & 65535u) * (1.0f / 65535.0f);
        d->pan_target[h] = u * 0.3f - 0.15f;
    }
    d->motion_remaining = 44100u * (20u + dl_rng(d) % 41u);
    if (d->motion_changes != UINT32_MAX) d->motion_changes++;
}

static float dl_tap(const int16_t *buf, int32_t len, int32_t w, float delay) {
    float rp = (float)w - delay;
    float flen = (float)len;
    while (rp < 0.0f) rp += flen;
    int32_t i0 = (int32_t)rp;
    float fr = rp - (float)i0;
    if (i0 >= len) i0 -= len;
    int32_t i1 = i0 + 1 >= len ? 0 : i0 + 1;
    float b0 = (float)buf[i0] * (1.0f / 2048.0f);
    float b1 = (float)buf[i1] * (1.0f / 2048.0f);
    return b0 + (b1 - b0) * fr;
}

static void __attribute__((noinline)) dl_process(bb_delay_t *d, const float *in, float *out_l, float *out_r, int n) {
    float target = d->time_target;
    int32_t len = (int32_t)BB_DELAY_RING;
    float lvl = d->level;
    int32_t w = d->w;
    float dcur = d->dcur, lp_st = d->lp;
    float hp_z1 = d->hp_z1, hp_a = d->hp_a;
    /* Hoist loop-invariant computations */
    float tc = 0.06f + d->tone * 0.6f;
    int is_tape = d->type == 1;
    float tape_depth = is_tape ? (0.1f + 1.9f * d->wear) : 0;
    float tape_wow_k = 1.5f * (1.0f / 44.1f) * tape_depth;
    float tape_flut_k = 0.15f * (1.0f / 44.1f) * tape_depth;
    float tape_sat_g = is_tape ? (1.0f + 1.5f * d->wear) : 0;
    float tape_sat_ig = tape_sat_g > 0 ? (1.0f / tape_sat_g) : 0;
    uint32_t wow_inc = (uint32_t)(0.5f * 4294967296.0f / 44100.0f);
    uint32_t flut_inc = (uint32_t)(6.1f * 4294967296.0f / 44100.0f);
    int has_motion = d->motion_target != 0 || d->motion != 0;
    uint8_t heads = d->heads;
    /* Precompute static pan values */
    static const float pan_l[3] = {0.75f, 0.5f, 0.25f};

    for (int i = 0; i < n; i++) {
        /* HP filter on send */
        float hp_out = in[i] - hp_z1;
        hp_z1 += hp_out * hp_a;

        /* Slew delay time */
        dcur += (target - dcur) * 0.0008f;

        /* Motion update (only when active) */
        if (has_motion) {
            d->motion += (d->motion_target - d->motion) * 0.0005f;
            if (!d->motion_target && d->motion < 1e-7f) { d->motion = 0; has_motion = 0; }
            else {
                if (!d->motion_remaining) dl_motion_targets(d);
                d->motion_remaining--;
                float sl = d->motion_slew;
                for (unsigned h = 0; h < 3; h++) {
                    d->head_offset[h] += (d->head_target[h] - d->head_offset[h]) * sl;
                    d->pan_offset[h] += (d->pan_target[h] - d->pan_offset[h]) * sl;
                }
            }
        }

        /* Three-head read */
        float tap[3], l = 0, r = 0;
        float d3 = dcur * (1.0f / 3.0f);
        float motion_d = d->motion * dcur;
        /* Precompute tape modulation (once, shared by all heads) */
        float tape_mod = 0;
        if (is_tape) {
            tape_mod = tape_wow_k * fx_psin(d->wow_ph) + tape_flut_k * fx_psin(d->flut_ph);
            d->wow_ph += wow_inc;
            d->flut_ph += flut_inc;
        }
        for (unsigned h = 0; h < 3; h++) {
            float delay = d3 * (float)(h + 1u);
            if (has_motion) delay += motion_d * d->head_offset[h];
            if (is_tape) delay += tape_mod;
            tap[h] = dl_tap(d->ring, len, w, delay);
        }

        /* Feedback through first head */
        float fb = tap[0] * d->fdbk;
        lp_st += (fb - lp_st) * tc;
        if (fxabs(lp_st) < 1e-20f) lp_st = 0;

        float record = hp_out + lp_st;

        if (is_tape) {
            /* TAPE: saturation + DC block */
            record = fx_tanh(record * tape_sat_g) * tape_sat_ig;
            d->tape_hp += (record - d->tape_hp) * 0.0042725f;
            record -= d->tape_hp;
        }

        /* Write to ring (12-bit range in int16) */
        int32_t k = (int32_t)(record * 2048.0f);
        if (k > 32767) k = 32767; if (k < -32767) k = -32767;
        d->ring[w] = (int16_t)k;
        w++; if (w >= len) w = 0;  /* branch cheaper than modulo on pi32v2 */

        /* Stereo pan per head */
        for (unsigned h = 0; h < 3; h++) {
            if (!(heads & (1u << h))) continue;
            float pan = pan_l[h];
            if (has_motion) pan += d->motion * d->pan_offset[h];
            l += tap[h] * pan;
            r += tap[h] * (1.0f - pan);
        }

        out_l[i] += l * lvl;
        out_r[i] += r * lvl;
    }

    d->w = w; d->dcur = dcur; d->lp = lp_st;
    d->hp_z1 = hp_z1;
}

static void dl_init(bb_delay_t *d) {
    d->hp_a = 1.0f / (1.0f + 6.283185307f * 80.0f / 44100.0f);
    d->motion_slew = 1.0f / (8.0f * 44100.0f);
    d->motion_rng = 0x42424543u;
    d->heads = 7;
    d->dcur = 44100.0f * 0.360f; /* default 360ms */
    d->time_target = d->dcur;
}

/* ── Distortion ─────────────────────────────────────────────────────── */

static void sh_prep(bb_shape_t *s, float drive, int type) {
    float k = drive > 0.01f ? drive : 0.01f;
    s->type = type; s->k = k;
    s->g = k < 1.0f ? 1.0f / k : 1.0f;
    s->c1 = s->c2 = 0.0f;
    switch (type) {
    case 0: s->g = 1.0f / fx_tanh(k); break;                     /* Diode */
    case 2: s->c1 = 0.08f*k; s->c2 = k<1?k:1; break;            /* SAT */
    case 3: { float m=(k-0.85f)/1.15f;                            /* BFZ */
              s->c1 = m<0?0:(m>1?1:m); s->c2=k*2.5f; break; }
    case 5: s->g = 1.0f / (k > 0.01f ? k * 2.0f : 0.02f); break; /* Fold */
    case 6: s->c1 = 4.0f + (1.0f-k) * 252.0f;                    /* Crush: quantize steps */
            s->g = 1.0f; break;
    default: break;
    }
}

static float sh_apply(const bb_shape_t *s, float x) {
    float k = s->k;
    switch (s->type) {
    case 0: return fx_tanh(k * x) * s->g;                         /* Diode */
    case 1: { float v=(fx_tanh(x*k+0.35f)-0.33638f)*0.958f;      /* Clip */
              return v*s->g; }
    case 2: { float u=x*k+s->c1*x*x; float w=u/(1+fxabs(u));     /* SAT */
              float m=s->c2; return ((1-0.65f*m)*x*m+0.65f*m*w*1.35f)*s->g; }
    case 3: { float u=x*s->c2+0.22f; float w=(u/(1+fxabs(u))-0.18033f)*1.05f;  /* BFZ */
              return (1-s->c1)*x+s->c1*w; }
    case 4: { float u=x*k+0.12f; if(u>1)u=1;if(u<-1)u=-1;        /* PDIST */
              return ((u-u*u*u/3.0f)-(0.12f-0.12f*0.12f*0.12f/3.0f))*(1.5f/1.479f); }
    case 5: { float u=x*k*2.0f; int i; for(i=0;i<3;i++){         /* Fold */
              if(u>1.0f)u=2.0f-u;if(u<-1.0f)u=-2.0f-u;}
              return u*s->g; }
    case 6: { float q=s->c1; float u=x*k;                         /* Crush */
              u=((int)(u*q+0.5f))/q;
              return u*s->g; }
    default: return x;
    }
}

/* 2.3: Fold (type 5) and Crush (type 6) now implemented. */

/* ── Public API ─────────────────────────────────────────────────────── */

int bb_fxbus_init(bb_fxbus *f, unsigned sample_rate) {
    if (!f || sample_rate < 8000 || sample_rate > 96000) return 0;
    /* Zero everything */
    unsigned char *p = (unsigned char *)f;
    for (unsigned long i = 0; i < sizeof(*f); i++) p[i] = 0;
    f->sample_rate = sample_rate;
    rm_init(&f->room);
    dl_init(&f->delay);
    f->volume = 1.0f;
    f->rv_level = 0.5f;
    sh_prep(&f->shape, 1.0f, 0); /* Diode default */
    return 1;
}

int bb_fxbus_set_reverb(bb_fxbus *f, unsigned decay, unsigned tone, unsigned level) {
    if (!f || decay > 100 || tone > 100 || level > 100) return 0;
    rm_set(&f->room, (float)decay * 0.01f, (float)tone * 0.01f);
    f->room.out_gain = (float)level * 0.01f;
    return 1;
}

int bb_fxbus_set_delay(bb_fxbus *f, unsigned ms, unsigned fdbk, unsigned tone,
                       unsigned wear, unsigned wet, unsigned heads, unsigned type) {
    if (!f || ms < 10 || ms > 500 || fdbk > 95 || tone > 100 ||
        wear > 100 || wet > 100 || heads > 7 || type > 1) return 0;
    bb_delay_t *d = &f->delay;
    float delay = (float)(f->sample_rate * ms) / 1000.0f;
    if (delay > (float)(BB_DELAY_RING - 64u))
        delay = (float)(BB_DELAY_RING - 64u);
    d->time_target = delay;
    d->fdbk = (float)fdbk * 0.01f;
    d->tone = (float)tone * 0.01f;
    d->wear = (float)wear * 0.01f;
    d->level = (float)wet * 0.01f;
    d->heads = (uint8_t)heads;
    d->type = (int32_t)type;
    return 1;
}

int bb_fxbus_set_motion(bb_fxbus *f, unsigned depth) {
    if (!f || depth > 100) return 0;
    f->delay.motion_target = (float)depth * 0.01f;
    return 1;
}

int bb_fxbus_set_dist(bb_fxbus *f, unsigned type, unsigned drive) {
    if (!f || type > 6 || drive > 100) return 0; /* types 0..6 */
    if (type == 0 && drive == 0) {
        f->shape.type = -1; /* bypass */
        return 1;
    }
    sh_prep(&f->shape, (float)drive * 0.04f, (int)type);
    return 1;
}

int bb_fxbus_set_comp(bb_fxbus *f, unsigned amount) {
    if (!f || amount > 100) return 0;
    float a = (float)amount * 0.01f;
    float ratio = 2.0f + 3.0f * a;
    f->comp.thr = 10.0f - 18.0f * a;
    f->comp.slope = 1.0f - 1.0f / ratio;
    f->comp.makeup = a * 2.2f + a * a * 6.6f;
    return 1;
}

int bb_fxbus_set_volume(bb_fxbus *f, unsigned vol) {
    if (!f || vol > 100) return 0;
    f->volume = (float)vol * 0.01f;
    return 1;
}

int bb_fxbus_process(bb_fxbus *restrict f,
                     const float *restrict rev_send,
                     const float *restrict dly_send,
                     float *restrict left, float *restrict right,
                     unsigned frames) {
    if (!f || !f->sample_rate || !rev_send || !dly_send ||
        !left || !right || !frames || frames > 128) return 0;

    /* Clear wet buffers */
    for (unsigned i = 0; i < frames; i++) left[i] = right[i] = 0;

    /* Reverb return */
    if (f->room.out_gain > 0.001f)
        rm_process(&f->room, rev_send, left, right, (int)frames);

    /* Delay return */
    if (f->delay.level > 0.001f)
        dl_process(&f->delay, dly_send, left, right, (int)frames);

    /* Master distortion (7 types: Diode, Clip, SAT, BFZ, PDIST, Fold, Crush) */
    if (f->shape.type >= 0) {
        for (unsigned i = 0; i < frames; i++) {
            left[i] = sh_apply(&f->shape, left[i]) * 0.7f;
            right[i] = sh_apply(&f->shape, right[i]) * 0.7f;
        }
    }

    /* Compressor + volume (fused when both active to avoid a second pass) */
    float vol = f->volume;
    if (f->comp.slope > 0.001f) {
        float thr = f->comp.thr, slope = f->comp.slope, makeup = f->comp.makeup;
        float det = f->comp.det, env = f->comp.env_db;
        float k_att = 0.01f, k_rel = 0.9995f;
        for (unsigned i = 0; i < frames; i++) {
            float ml = fxabs(left[i]), mr = fxabs(right[i]);
            float mag = ml > mr ? ml : mr;
            det = mag > det ? mag : det * 0.9999f;
            /* Sub-rate gain computation: log2/exp2 every 4th sample */
            if ((i & 3u) == 0) {
                float in_db = det > 1e-9f ? 6.0206f * fx_log2(det) : -120.0f;
                float over = in_db - thr;
                float gr = 0;
                if (over >= 3.0f) gr = -over * slope;
                else if (over > -3.0f) { float t = over + 3; gr = -(t*t)/12.0f * slope; }
                env = gr + (env - gr) * (gr < env ? k_att : k_rel);
                f->comp._cached_g = fx_db2lin(env + makeup) * vol;
            }
            left[i] *= f->comp._cached_g;
            right[i] *= f->comp._cached_g;
        }
        f->comp.det = det;
        f->comp.env_db = env;
    } else {
        /* Volume-only pass */
        for (unsigned i = 0; i < frames; i++) {
            left[i] *= vol;
            right[i] *= vol;
        }
    }

    return 1;
}
