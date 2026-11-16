/* SPDX-License-Identifier: GPL-3.0-only
 * IMA arithmetic adapted from Felucca/Sloop eng_sample.c,
 * Copyright (C) 2026 Leo Kuroshita, Huegelton Instruments.
 * Big Beat isolated one-shot voice candidate. Not wired to firmware.
 * Caller owns immutable sample bytes until every referencing voice ends.
 * No allocation, flash address, voice stealing, mutable bank global or IRQ work.
 */
#ifndef BB_SAMPLE_VOICE_R1_H
#define BB_SAMPLE_VOICE_R1_H
#include <stdint.h>
#include <stddef.h>
#include "bb_sample_ima_tables_r1.h"
#include "bb_sample_ima_delta_r438.h"
typedef struct {
    const uint8_t *data;
    uint32_t frames, pos, fraction, rate;
    int32_t predictor, index, previous, current;
    unsigned active;
} bb_sample_voice;
/* Rejected starts preserve the currently sounding voice. The rate is source
 * frames per 44100-Hz output frame, Q16; at most five decodes on the
 * first tick (including priming), then four per tick. */
static int bb_sample_start(bb_sample_voice *v,const uint8_t *data,size_t bytes,
                           uint32_t frames,uint32_t rate) {
    if(!v||!data||!frames||frames>162816u||!rate||rate>262144u||
       (frames+1u)/2u>bytes)return 0;
    *v=(bb_sample_voice){.data=data,.frames=frames,.rate=rate,.active=1};
    return 1;
}
static void bb_sample_stop(bb_sample_voice *v) {if(v)v->active=0;}
static int32_t bb_sample_decode(bb_sample_voice *v) {
    uint32_t p=v->pos++,byte=v->data[p>>1],code=p&1?byte>>4:byte&15;
    int32_t difference=BB_SAMPLE_IMA_DELTA[v->index][code&7];
    int32_t predictor=v->predictor+((code&8)?-difference:difference);
    v->predictor=predictor<-32768?-32768:predictor>32767?32767:predictor;
    int32_t index=v->index+BB_SAMPLE_IMA_IDX[code&7];
    v->index=index<0?0:index>88?88:index;
    return v->predictor;
}
/* Exact interpolation floor without signed division in the sample loop. */
#include "bb_sample_floor_q15_r1.h"
#ifndef BB_SAMPLE_HALF_RATE_FAST
#define BB_SAMPLE_HALF_RATE_FAST 1
#endif
static int32_t bb_sample_tick(bb_sample_voice *v) {
    if(!v||!v->active)return 0;
    if(!v->pos)v->current=bb_sample_decode(v);
#if BB_SAMPLE_HALF_RATE_FAST
    /* Every voice in the current classic-three pack uses exactly 1/2 rate.
     * Its phase can only alternate 0 and 32768: skip the general loop while
     * retaining priming, interpolation floor and final-frame behavior. The
     * generic path remains for other rates or a noncanonical restored phase. */
    if(v->rate==32768u && (v->fraction==0u || v->fraction==32768u)) {
        if(!v->fraction){
            v->fraction=32768u;
            return v->previous+bb_sample_floor_q15((v->current-v->previous)*16384);
        }
        v->fraction=0;v->previous=v->current;
        if(v->pos>=v->frames){v->current=0;v->active=0;}
        else v->current=bb_sample_decode(v);
        return v->previous;
    }
#endif
    uint32_t fraction=v->fraction+v->rate;
    while(fraction>=65536u) {
        fraction-=65536u;v->previous=v->current;
        if(v->pos>=v->frames) {
            v->current=0;v->active=0;break;
        }
        v->current=bb_sample_decode(v);
    }
    /* At rates above unity an end can leave multiple fractional units.
     * Do not extrapolate outside the final sample range. */
    if(fraction>=65536u)fraction=65535u;
    v->fraction=fraction;
    return v->previous+bb_sample_floor_q15((v->current-v->previous)*(int32_t)(fraction>>1));
}
#endif
