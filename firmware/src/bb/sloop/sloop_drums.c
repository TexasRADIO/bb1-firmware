/* SPDX-License-Identifier: GPL-3.0-only
 * Actual SLOOP2.8.3 fixed-point kernel with event-span buffering; no stock sequencer.
 * Pin5eb24c1dc18261a85c193e8b28318944690e58fd. DSP original copyright retained in inc. */
#include "sloop_drums.h"
#include <string.h>
#include "sloop_tables.h"
typedef struct {int32_t amp0,amp1;} vmod_t;
#include "sloop_dsp.inc"
#if defined(__APPLE__) || defined(__wasm__)
/* Host ports have no FM-1 linker pool. */
static uint32_t sl_rng_term;
#else
static uint32_t sl_rng_term __attribute__((section(".pool"))); /* CPU0 control scratch */
#endif
static uint32_t rng(void){return sl_rng_term;}
enum { DW_NONE, DW_SINE, DW_TRI, DW_SQUARE, DW_FM, DW_BELL };
enum { DN_NONE, DN_WHITE, DN_METAL, DN_CYM, DN_CHIP, DN_CLAP = 0x10 };
enum { DF_OFF, DF_LP, DF_BP, DF_HP, DF_ALL = 4 };   /* flt: mode | DF_ALL (the tone too) | res << 3 */
/* GM note -> synth lane and a pitch offset in semitones */
static const struct { uint8_t lane; int8_t semi; } DS_MAP[128 - 35] = {
    /* 35 */ {14, 0}, {0, 0}, {11, 0}, {1, 0}, {2, 0}, {15, 0}, {5, -3}, {3, 0}, {5, 0}, {3, -1},
    /* 45 */ {5, 3}, {4, 0}, {6, -3}, {6, 0}, {7, 0}, {6, 3}, {8, 0}, {7, 3}, {8, 6}, {9, 4},
    /* 55 */ {7, 5}, {12, 0}, {7, 1}, {9, -5}, {8, 1}, {10, 9}, {10, 6}, {10, 3}, {10, 0}, {10, -5},
    /* 65 */ {6, 5}, {6, 1}, {12, 7}, {12, 3}, {9, -3}, {9, 0}, {13, 9}, {13, 6}, {9, -7}, {9, -9},
    /* 75 */ {13, 0}, {13, 4}, {13, -1}, {10, 12}, {10, 8}, {8, 24}, {8, 20}, {9, 2},
    /* 83..127: shaker */
};
static uint32_t ds_lane(uint32_t note, int32_t *semi)
{
    *semi = 0;
    if (note < 35u)
        return 0;
    if (note >= 83u)
        return 9;
    *semi = DS_MAP[note - 35u].semi;
    return DS_MAP[note - 35u].lane;
}



/* per-sample DECAY_K (Q16) -> per-block (CTL = 32 samples) by squaring five times */
static uint32_t ds_k32(uint32_t idx)
{
    uint32_t k = DECAY_K[idx & 127u], i;
    for (i = 0; i < 5u; i++)
        k = (k * k) >> 16;
    return k;
}
static int32_t ds_onepole(uint32_t cut)                 /* CUTOFF_HZ index -> a = g / (1 + g), Q15 */
{
    uint32_t g = SVF_G[cut & 127u];
    return (int32_t)((g << 15) / (4096u + g));
}
static uint32_t ds_inc(int32_t p16) { return pitch_inc(clamp(p16, 0, 127 * 16 + 15)); }
static uint16_t ds_blocks(uint32_t units2ms) { return (uint16_t)(units2ms * 2u * FS / 1000u / CTL); }

/* the filter's coefficients for this block: the cutoff plus the envelope's share */
static void ds_filter(sl_dsv *s)
{
    const sl_sound *d = s->d;
    int32_t cut = s->cut + ((s->pe * (int32_t)d->fenv) >> 7), i, g, den;   /* (pe Q15 x steps << 8) */
    cut = clamp(cut, 0, 127 << 8);
    i = cut >> 8;
    g = SVF_G[i];
    if (i < 127)
        g += ((SVF_G[i + 1] - g) * (cut & 255)) >> 8;
    den = 4096 + ((g * (g + s->fk)) >> 12);
    s->a1 = (int32_t)((4096u << 13) / (uint32_t)den);
    s->a2 = (s->a1 * g) >> 12;
    s->a3 = (s->a2 * g) >> 12;
}

/* the 808 cymbal oscillators (205.3 304.4 369.6 522.7 540 800 Hz) as 1/16 semitones above the first */
static const int16_t DS_METAL[6] = {0, 109, 163, 259, 268, 377};

/* (2.7, the DRUMS screen's SOUND page) a sound's DECAY, -4 .. +3 (0: as designed): the decays' DECAY_K steps (x0.25 ..
 * x3 the time, 5.4 % a step) and the holds scaled alike (Q8) */
static const int8_t DK_IDX[8] = {-26, -17, -10, -4, 0, 6, 13, 21};
static const uint16_t DK_SCALE[8] = {64, 102, 154, 205, 256, 358, 512, 768};
/* tune16: the sound's TUNE in 1/16 semitones; dsel: its DECAY + 4 (0 .. 7) */
static void ds_on(sl_dsv *s, const sl_sound *d, uint32_t crush, uint32_t note, uint32_t vel, int32_t tune16, uint32_t dsel)
{
    int32_t dk = DK_IDX[dsel & 7u];
    int32_t semi;
    uint32_t i;
    (void)ds_lane(note, &semi);
    memset(s, 0, sizeof *s);
    s->d = d;
    s->crush = (uint8_t)crush;
    s->gain = (int32_t)vel * 258;
    {   /* level: quarter dB from 128; 24 quarters = 6 dB = x2 (0.02 dB off per 6 dB) */
        static const uint16_t QDB[24] = {1024, 1054, 1085, 1116, 1149, 1182, 1217, 1252, 1289, 1326, 1365, 1405,
                                         1446, 1488, 1532, 1576, 1622, 1669, 1718, 1768, 1820, 1873, 1928, 1984};
        int32_t e = (int32_t)d->level - 128 + 192, q = e / 24 - 8;   /* (+192: positive for the divide) */
        uint32_t g = QDB[e % 24];
        s->lg = (int32_t)(q >= 0 ? g << q : g >> -q);
    }
    s->base16 = ((int32_t)d->pitch + semi) * 16 + d->fine + tune16;
    s->pe = d->bend || d->fenv ? 32767 : 0;
    s->kpe = ds_k32(d->btime);
    s->inc = s->inc_to = ds_inc(s->base16 + ((s->pe * (int32_t)d->bend * 16) >> 15));
    s->amp = s->amp_to = d->wave ? (int32_t)d->tlev * 258 : 0;
    s->ka = ds_k32((uint32_t)clamp((int32_t)d->decay + dk, 0, 127));
    s->hold = ds_blocks(d->hold);
    s->dk = (int8_t)dk;
    if (dk)
        s->hold = (uint16_t)(((uint32_t)s->hold * DK_SCALE[dsel & 7u]) >> 8);
    s->nz = s->nz_to = (d->src & 15u) ? (int32_t)d->nlev * 258 : 0;
    s->kn = (d->src & DN_CLAP) ? ds_k32(12) : ds_k32((uint32_t)clamp((int32_t)d->ndec + dk, 0, 127));   /* CLAP: short bursts first */
    s->nhold = (d->src & DN_CLAP) ? 0 : ds_blocks(d->nhold);
    if (dk)
        s->nhold = (uint16_t)(((uint32_t)s->nhold * DK_SCALE[dsel & 7u]) >> 8);
    s->ck = s->ck_to = (int32_t)d->click * 258;
    s->bursts = 1;
    s->fmode = d->flt & 7u;
    s->fk = 8192 - (int32_t)(d->flt >> 3) * 245;        /* damping 2.0 .. ~0.15 (Q12), as tsvf_coef */
    /* softer hits are darker: about an octave from the hardest to a ghost note */
    s->cut = ((int32_t)d->fcut << 8) + ((int32_t)vel - 110) * 40;
    s->ahp = d->hpf ? ds_onepole(d->hpf) : 0;
    if (!d->wave && tune16) {                           /* (2.7.2) a tone-less sound (closed hat, shaker, clap): TUNE
                                                         * shifts its brightness too, so it reacts like resampling a
                                                         * sample kit (~1.168 CUTOFF_HZ steps a semitone). The tonal
                                                         * sounds and the metal cymbals already track TUNE via base16. */
        int32_t co8 = tune16 * 1197 / 64;               /* the TUNE as a CUTOFF_HZ index offset, Q8 */
        s->cut += co8;
        if (d->hpf)
            s->ahp = ds_onepole((uint32_t)clamp((int32_t)d->hpf + co8 / 256, 1, 127));
    }
    if (s->fmode & 3u)
        ds_filter(s);
    s->rnd = (int32_t)(0x9E3779B9u ^ (note * 2654435761u) ^ rng());
    s->lfsr = 0x4001u;
    {   /* CHIP clock: 8x that note; saturated (the top notes would wrap 32 bits: a dull noise) */
        uint32_t ci = pitch_inc(clamp((int32_t)d->chip, 0, 127) * 16);
        s->cinc = ci >= 0x20000000u ? 0xFFFFFFFFu : ci << 3;
    }
    for (i = 0; i < 6u; i++)
        s->minc[i] = ds_inc(s->base16 + DS_METAL[i]);
}

/* one CTL block of control: the envelopes' targets at its end */
static void ds_control(sl_dsv *s)
{
    const sl_sound *d = s->d;
    s->amp = s->amp_to;
    s->nz = s->nz_to;
    s->ck = s->ck_to;
    s->inc = s->inc_to;
    if (s->hold)
        s->hold--;
    else
        s->amp_to = (int32_t)(((uint32_t)s->amp * s->ka) >> 16);
    if ((d->src & DN_CLAP) && s->bursts < 4u && s->t >= s->bursts * 420u) {
        s->nz = (int32_t)d->nlev * 258;                 /* bursts at 0, 9.5, 19 ms; the 4th is the tail */
        if (++s->bursts == 4u) {
            s->kn = ds_k32((uint32_t)clamp((int32_t)d->ndec + s->dk, 0, 127));
            s->nhold = ds_blocks(d->nhold);
        }
    }
    if (s->nhold)
        s->nhold--;
    else
        s->nz_to = (int32_t)(((uint32_t)s->nz * s->kn) >> 16);
    s->ck_to = (s->ck * 3) >> 3;                        /* ~1 ms: gone in a few blocks */
    s->pe = (int32_t)(((uint32_t)s->pe * s->kpe) >> 16);
    s->inc_to = ds_inc(s->base16 + ((s->pe * (int32_t)d->bend * 16) >> 15));
    if ((s->fmode & 3u) && d->fenv)
        ds_filter(s);
}

static int ds_alive(const sl_dsv *s)
{
    if ((s->d->src & DN_CLAP) && s->bursts < 4u)
        return 1;                                       /* bursts still to come */
    return s->hold || s->amp > 8 || s->amp_to > 8 || s->nz > 8 || s->nz_to > 8 || s->ck > 8;
}

/* n (<= CTL) samples of voice s into out[] (Q15-ish, peak ~ 32767); returns 0 when it ended */
static int ds_render(sl_dsv *s, int32_t *out, uint32_t n)
{
    const sl_sound *d = s->d;
    uint32_t i, src = d->src & 15u, wave = d->wave, fmode = s->fmode & 3u, fall = s->fmode & DF_ALL;
    /* (2.6, a whistle on the kicks) the levels glide to their targets exactly: a0 + r (i + 1) / CTL. A step of
     * r >> CTL_LOG2 a sample fell up to CTL - 1 short and jumped the rest at every block: a click 1378 times a
     * second under the kick's long sine (tones at 1.4 / 2.8 kHz). The pitch keeps its step (a frequency, no jump) */
    int32_t a0 = s->amp, ra = s->amp_to - s->amp, z0 = s->nz, rn = s->nz_to - s->nz, c0 = s->ck, rc = s->ck_to - s->ck;
    int32_t di = (int32_t)(s->inc_to - s->inc) >> CTL_LOG2;
    int32_t a = s->amp, z = s->nz, c = s->ck, inc = (int32_t)s->inc;
    int32_t drive = 16 + d->drive, bits = s->crush & 15, hold = (s->crush >> 4) + 1;
    int32_t t2l = (int32_t)d->t2lev * 258;
    for (i = 0; i < n; i++) {
        int32_t tone = 0, nz = 0, x;
        if (wave) {
            switch (wave) {
            case DW_SINE: tone = sine_i(s->ph); break;
            case DW_TRI: tone = osc_tri(s->ph); break;
            case DW_SQUARE: tone = (int32_t)s->ph < 0 ? -24000 : 24000; break;
            case DW_FM:                                 /* ratio 1.41: metallic, index follows the tone */
                tone = sine_i(s->ph + ((uint32_t)(sine_i(s->ph2) * a) << 1));
                s->ph2 += (uint32_t)inc + ((uint32_t)inc >> 2) + ((uint32_t)inc >> 3) + ((uint32_t)inc >> 5);
                break;
            default:                                    /* BELL: two squares */
                tone = ((int32_t)s->ph < 0 ? -12000 : 12000) + ((int32_t)s->ph2 < 0 ? -12000 : 12000);
                s->ph2 += (uint32_t)inc + ((uint32_t)inc >> 1) - ((uint32_t)inc >> 6);
                break;
            }
            s->ph += (uint32_t)inc;
            tone = mulq15(tone, a);
            if (d->t2) {                                /* the second mode: a sine, decaying twice as fast */
                s->ph3 += ((uint32_t)inc >> 5) * d->t2;
                tone += mulq15(mulq15(sine_i(s->ph3), mulq15(a, a)), t2l);
            }
        }
        if (src) {
            if (src == DN_WHITE) {
                nz = (int32_t)noise32(&s->rnd) >> 17;
            } else if (src == DN_CHIP) {
                s->cacc += s->cinc;
                if (s->cacc < s->cinc) {                /* wrapped: clock the LFSR */
                    uint32_t b = (s->lfsr ^ (s->lfsr >> 1)) & 1u;
                    s->lfsr = (s->lfsr >> 1) | (b << 14);
                }
                nz = (s->lfsr & 1u) ? 20000 : -20000;
            } else {                                    /* METAL / CYM: six squares (+ white) */
                s->mph[0] += s->minc[0];              /* unrolled: one sign bit each */
                s->mph[1] += s->minc[1];
                s->mph[2] += s->minc[2];
                s->mph[3] += s->minc[3];
                s->mph[4] += s->minc[4];
                s->mph[5] += s->minc[5];
                nz = (int32_t)(((s->mph[0] >> 31) + (s->mph[1] >> 31) + (s->mph[2] >> 31) +
                                (s->mph[3] >> 31) + (s->mph[4] >> 31) + (s->mph[5] >> 31)) * 9600u) - 28800;
                if (src == DN_CYM)
                    nz += (int32_t)noise32(&s->rnd) >> 18;
            }
            if (wave == DW_BELL)
                nz += tone, tone = 0;
            if (s->ahp) {                               /* two one-pole high-passes: 12 dB / oct */
                s->hp1 += mulq15(nz - s->hp1, s->ahp);
                nz -= s->hp1;
                s->hp2 += mulq15(nz - s->hp2, s->ahp);
                nz -= s->hp2;
            }
            nz = mulq15(nz, z);
            if (d->src & DN_CLAP)
                nz = (int32_t)((uint32_t)nz << 1);                               /* band-passed: as loud as the others */
        }
        if (c) {                                        /* the click: differentiated noise, bright */
            int32_t w = (int32_t)noise32(&s->rnd) >> 17;
            nz += mulq15(w - s->cprev, c);
            s->cprev = w;
        }
        x = fall ? tone + nz : nz;
        if (fmode) {                                    /* SVF (dsp.c tsvf_lp), with the BP / HP outs */
            int32_t v3 = x - s->f2;
            int32_t v1 = (s->a1 * s->f1 + s->a2 * v3) >> 13;
            int32_t v2 = s->f2 + ((s->a2 * s->f1 + s->a3 * v3) >> 13);
            s->f1 = clamp(2 * v1 - s->f1, -150000, 150000);
            s->f2 = clamp(2 * v2 - s->f2, -150000, 150000);
            x = fmode == DF_LP ? v2 : fmode == DF_BP ? v1 : x - ((s->fk * v1) >> 12) - v2;
        }
        x += fall ? 0 : tone;
        if (d->drive)
            x = softclip((x * drive) >> 4);
        if (s->crush) {
            if (--s->holdn <= 0) {
                s->holdn = hold;
                s->held = (int32_t)((uint32_t)(x >> bits) << bits);
            }
            x = s->held;
        }
        out[i] = ((mulq15(x, s->gain) >> 2) * s->lg) >> 8;
        a = a0 + ((ra * (int32_t)(i + 1u)) >> CTL_LOG2);   /* (|r| < 2^17: no overflow) */
        z = z0 + ((rn * (int32_t)(i + 1u)) >> CTL_LOG2);
        c = c0 + ((rc * (int32_t)(i + 1u)) >> CTL_LOG2);
        inc += di;
    }
    s->t += n;
    ds_control(s);
    return ds_alive(s);
}


const uint8_t sl_lane_note[SL_LANES]={36,38,39,42,46,43,48,49,51,70,63,37,56,75,35,40};
unsigned sl_lane_of_note(unsigned note,int32_t *semi){if(!semi)return 0;return ds_lane(note,semi);}
int sl_voice_on(sl_voice *v,const sl_sound *sounds,unsigned crush,unsigned note,unsigned vel,int32_t tune16,unsigned decay,uint32_t noise){
 if(!v||!sounds||note>127||!vel||vel>127||crush>255||tune16< -128||tune16>112||decay>7)return 0;
 int32_t semi;unsigned lane=sl_lane_of_note(note,&semi);
 sl_rng_term=noise;ds_on(&v->state,&sounds[lane],crush,note,vel,tune16,decay);v->offset=v->available=0;v->active=v->more=1;return 1;
}
unsigned sl_voice_render(sl_voice *v,int32_t *out,unsigned frames){
 if(!sl_voice_cache_valid(v)||(!out&&frames)||!v->active)return 0;
 unsigned done=0;
 while(done<frames&&v->active){
  if(!v->available){v->more=(uint8_t)ds_render(&v->state,v->block,SL_CTL);v->offset=0;v->available=SL_CTL;}
  unsigned n=frames-done;if(n>v->available)n=v->available;
  memcpy(out+done,v->block+v->offset,n*sizeof(*out));done+=n;v->offset=(uint8_t)(v->offset+n);v->available=(uint8_t)(v->available-n);
  if(!v->available&&!v->more)v->active=0;
 }
 return done;
}

/* Immutable dictionary record has the same lifetime as the bound bank. */
int sl_voice_on_sound(sl_voice *v,const sl_sound *sound,unsigned crush,unsigned note,unsigned vel,int32_t tune16,unsigned decay,uint32_t noise){
 if(!v||!sound||note>127||!vel||vel>127||crush>255||tune16< -128||tune16>112||decay>7)return 0;
 sl_rng_term=noise;ds_on(&v->state,sound,crush,note,vel,tune16,decay);v->offset=v->available=0;v->active=v->more=1;return 1;
}
