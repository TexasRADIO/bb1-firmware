/* SPDX-License-Identifier: GPL-3.0-only
 * BB1 effects chain — ROOM reverb, DIGI/TAPE delay, distortion, compressor.
 * Adapted from OG Synth X0X fxbus (GPL-3.0-only) and BB1's echo/spring.
 *
 * Signal chain:  dry + ROOM(rev_send) + DELAY(dly_send) → DIST → COMP → VOL → out
 *
 * Replaces bb_echo + bb_spring. The delay keeps BB1's three-head architecture
 * and motion system. ROOM is block-float FDN (17 KB). Delay is int16 ring.
 * Seven distortion types from d9_shape. One-knob glue compressor.
 *
 * Build note: compile bb_fxbus.c with -Oz (minsize) to fit in BB1's code budget. */
#ifndef BB_FXBUS_H
#define BB_FXBUS_H
#include <stdint.h>

/* ── ROOM reverb (block-float FDN) ──────────────────────────────────── */
/* BB1 2.3 LITE: 4 FDN lines + 2 diffusers (was 8+4) to fit the CPU budget.
 * Halves per-sample work while keeping the reverb character recognisable. */
#ifndef BB_RM_LITE
#define BB_RM_LITE 1    /* 1 = 4-line lite (BB1), 0 = full 8-line (future) */
#endif
#if BB_RM_LITE
#define BB_RM_N       4
#define BB_RM_DIF_N   2
#else
#define BB_RM_N       8
#define BB_RM_DIF_N   4
#endif
#define BB_RM_BLK     16
#define BB_RM_L0 1123
#define BB_RM_L1 1459
#define BB_RM_L2 1747
#define BB_RM_L3 1571
#if !BB_RM_LITE
#define BB_RM_L4 1283
#define BB_RM_L5 2083
#define BB_RM_L6 2243
#define BB_RM_L7 1907
#endif
#define BB_RM_NB(l) ((l) / BB_RM_BLK + 1)
#if BB_RM_LITE
#define BB_RM_BLOCKS (BB_RM_NB(BB_RM_L0)+BB_RM_NB(BB_RM_L1)+BB_RM_NB(BB_RM_L2)+BB_RM_NB(BB_RM_L3))
#define BB_RM_DIF_TOTAL (211+157)
#else
#define BB_RM_BLOCKS (BB_RM_NB(BB_RM_L0)+BB_RM_NB(BB_RM_L1)+BB_RM_NB(BB_RM_L2)+BB_RM_NB(BB_RM_L3)+\
                      BB_RM_NB(BB_RM_L4)+BB_RM_NB(BB_RM_L5)+BB_RM_NB(BB_RM_L6)+BB_RM_NB(BB_RM_L7))
#define BB_RM_DIF_TOTAL (211+157+397+293)
#endif

typedef struct {
    float stage[BB_RM_N][BB_RM_BLK];
    float read[BB_RM_N][BB_RM_BLK];
    float c[BB_RM_N], a[BB_RM_N], lp[BB_RM_N];
    float tap_a, tap_k, tap_zl, tap_zr;
    float emph_x, emph_l, emph_r;
    float out_gain;
    int32_t quiet;
    uint8_t pos;
    uint16_t base[BB_RM_N], nblk[BB_RM_N], wblk[BB_RM_N];
    int16_t dif_pos[BB_RM_DIF_N];
    int16_t dif[BB_RM_DIF_TOTAL];
    int8_t exp[BB_RM_BLOCKS];
    int8_t man[BB_RM_BLOCKS * BB_RM_BLK];
} bb_room_t;

/* ── Delay ──────────────────────────────────────────────────────────── */
#define BB_DELAY_RING 22050u  /* 500 ms at 44.1 kHz, int16 = 44.1 KB */

typedef struct {
    int16_t ring[BB_DELAY_RING];
    float time_target;     /* target delay in samples */
    float dcur;            /* current slewed delay */
    float fdbk, tone, level, wear;
    float lp, tape_hp;
    float hp_z1, hp_a;     /* simple one-pole HP state */
    uint32_t wow_ph, flut_ph;
    int32_t w;             /* write position */
    int32_t type;          /* 0=DIGI, 1=TAPE */
    /* BB1 three-head and motion (carried from bb_echo) */
    float head_offset[3], head_target[3];
    float pan_offset[3], pan_target[3];
    float motion, motion_target, motion_slew;
    uint32_t motion_remaining, motion_changes, motion_rng;
    uint8_t heads;         /* bitmask of active heads */
} bb_delay_t;

/* ── Distortion (7 types) ───────────────────────────────────────────── */
typedef struct {
    int32_t type;
    float k, g, c1, c2;
    float crush_st[2];    /* bitcrush state */
} bb_shape_t;

/* ── Compressor ─────────────────────────────────────────────────────── */
typedef struct {
    float env_db, det;
    float thr, slope, makeup;
    float _cached_g;   /* gain held between sub-rate updates */
} bb_comp_t;

/* ── Combined bus ───────────────────────────────────────────────────── */
typedef struct {
    bb_room_t room;
    bb_delay_t delay;
    bb_shape_t shape;
    bb_comp_t comp;
    float volume;          /* master output level */
    float rv_level;        /* reverb return level */
    uint32_t sample_rate;
} bb_fxbus;

/* ── API ────────────────────────────────────────────────────────────── */
int  bb_fxbus_init(bb_fxbus *f, unsigned sample_rate);

/* Reverb parameters: decay 0..100, tone 0..100, hpf 0..100, level 0..100 */
int  bb_fxbus_set_reverb(bb_fxbus *f, unsigned decay, unsigned tone, unsigned level);

/* Delay parameters: ms 10..500, fdbk 0..95, tone 0..100, wear 0..100,
 * wet 0..100, heads bitmask 0..7, type 0=DIGI/1=TAPE */
int  bb_fxbus_set_delay(bb_fxbus *f, unsigned ms, unsigned fdbk, unsigned tone,
                        unsigned wear, unsigned wet, unsigned heads, unsigned type);

/* Motion depth: 0..100 */
int  bb_fxbus_set_motion(bb_fxbus *f, unsigned depth);

/* Distortion: type 0..7 (0=Diode..7=Crush, see bb_fxbus.c), drive 0..100 */
int  bb_fxbus_set_dist(bb_fxbus *f, unsigned type, unsigned drive);

/* Compressor amount: 0..100 (0 = bypass) */
int  bb_fxbus_set_comp(bb_fxbus *f, unsigned amount);

/* Master volume: 0..100 */
int  bb_fxbus_set_volume(bb_fxbus *f, unsigned vol);

/* Process one block. Separate send buffers for reverb and delay.
 * Output is stereo wet (added to left/right). 1..128 frames.
 * Buffers must not overlap each other or bus state. */
int  bb_fxbus_process(bb_fxbus *restrict f,
                      const float *restrict rev_send,
                      const float *restrict dly_send,
                      float *restrict left, float *restrict right,
                      unsigned frames);

#endif /* BB_FXBUS_H */
