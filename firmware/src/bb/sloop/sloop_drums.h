/* SPDX-License-Identifier: GPL-3.0-only
 * Derived from isod89/sloop-fm1 5eb24c1dc18261a85c193e8b28318944690e58fd. */
#ifndef SLOOP_DRUMS_H
#define SLOOP_DRUMS_H
#include <stdint.h>
#define SL_LANES 16u
#define SL_CTL 32u
typedef struct {
    uint8_t wave, src;           /* DW_*, DN_* (| DN_CLAP) */
    uint8_t pitch, fine;         /* the tone: MIDI note + 1/16 semitones (METAL / CYM: their base) */
    uint8_t bend, btime;         /* pitch drop: semitones at the hit, DECAY_K index of its fall */
    uint8_t hold, decay, tlev;   /* tone: hold (2 ms units), DECAY_K index, level 0..127 */
    uint8_t t2, t2lev;           /* second partial: ratio x32 (0 = none), level 0..127 */
    uint8_t click;               /* attack burst level 0..127 */
    uint8_t nlev, nhold, ndec;   /* noise: level 0..127, hold (2 ms units), DECAY_K index */
    uint8_t flt, fcut;           /* filter: DF_* | res (0..31) << 3; CUTOFF_HZ index */
    int8_t fenv;                 /* cutoff offset at the hit (CUTOFF_HZ steps, follows the pitch drop) */
    uint8_t hpf;                 /* CUTOFF_HZ index of the noise high-pass, 0 = off */
    uint8_t chip;                /* CHIP noise: clock note */
    uint8_t drive, level;        /* drive 0..127; level: 128 + 4 x dB (-32 .. +31.75 dB) */
} sl_sound;
typedef struct {
    const sl_sound *d;
    uint32_t ph, inc, inc_to;    /* tone phase; increment now and at the end of the block */
    uint32_t ph2, ph3, mph[6];   /* BELL / FM second oscillator, the second partial, the metal squares */
    uint32_t minc[6];
    int32_t pe;                  /* pitch envelope Q15 (32767 = BEND semitones) */
    int32_t amp, amp_to;         /* tone envelope Q15, now and at the end of the block */
    int32_t nz, nz_to;           /* noise envelope Q15 */
    int32_t ck, ck_to;           /* click envelope Q15 */
    uint32_t kpe, ka, kn;        /* per-block decay factors Q16 */
    uint32_t t;                  /* samples since the hit (CLAP bursts) */
    uint16_t hold, nhold;        /* blocks left at full level */
    int32_t base16;              /* pitch in 1/16 semitones */
    int32_t cut;                 /* filter cutoff (CUTOFF_HZ index << 8) without the envelope */
    int32_t f1, f2, fk;          /* SVF states, damping (Q12) */
    int32_t a1, a2, a3;          /* SVF coefficients (dsp.c tsvf_coef) */
    int32_t hp1, hp2, ahp;       /* noise high-pass states, coefficient Q15 (0 = off) */
    int32_t rnd, cprev;          /* white noise state, the click's last noise sample */
    uint32_t lfsr, cacc, cinc;   /* CHIP noise */
    int32_t gain, lg;            /* velocity Q15, the sound's level Q10 */
    int32_t held, holdn;         /* crush: sample-and-hold */
    uint8_t crush, bursts, fmode;
    int8_t dk;                   /* (2.7) the DECAY's DECAY_K steps (the CLAP's tail after its bursts) */
} sl_dsv;
typedef struct {sl_dsv state;int32_t block[SL_CTL];uint8_t offset,available,active,more;} sl_voice;
static inline int sl_voice_cache_valid(const sl_voice *v){return v&&v->active<=1&&v->more<=1&&v->offset<=SL_CTL&&v->available<=SL_CTL&&v->offset+v->available<=SL_CTL;}
extern const uint8_t sl_lane_note[SL_LANES];
unsigned sl_lane_of_note(unsigned,int32_t *);
int sl_voice_on(sl_voice *,const sl_sound *,unsigned,unsigned,unsigned,int32_t,unsigned,uint32_t);
int sl_voice_on_sound(sl_voice *,const sl_sound *,unsigned,unsigned,unsigned,int32_t,unsigned,uint32_t);
unsigned sl_voice_render(sl_voice *,int32_t *,unsigned);
#endif
