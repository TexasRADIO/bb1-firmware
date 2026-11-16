/* SPDX-License-Identifier: GPL-3.0-only
 * Adapted from Felucca's spring algorithm, copyright (C) 2026
 * Leo Kuroshita (@kurogedelic), Hügelton Instruments.
 * Float adaptation by Big Beat; see EFFECTS_PROVENANCE.md. */
#include "bb_spring.h"
#include <string.h>
static float spring_read(const bb_spring *s,float delay) {
    unsigned age=(unsigned)delay;
    float fraction=delay-(float)age;
    unsigned near=(s->write-age)&(BB_SPRING_RING-1u);
    return s->line[near]+(s->line[(near-1u)&(BB_SPRING_RING-1u)]-s->line[near])*fraction;
}
/* Settled lengths retain identical interpolation arithmetic, but the three
 * float-to-index conversions belong to the block rather than each sample. */
static float spring_read_fixed(const bb_spring *s,unsigned age,float fraction) {
    unsigned near=(s->write-age)&(BB_SPRING_RING-1u);
    return s->line[near]+(s->line[(near-1u)&(BB_SPRING_RING-1u)]-s->line[near])*fraction;
}
int bb_spring_set(bb_spring *s,unsigned size,unsigned damping,unsigned wet) {
    if(!s || !s->sample_rate || size>100 || damping>100 || wet>100)return 0;
    s->target_length=(float)s->sample_rate*(0.03f+(float)size*0.0003f);
    /* Preserve the source's gain/damping range at 44.1 kHz. The float port
     * does not claim fixed-point equality; the allpass coefficient and gain
     * staging are retained, while length reads interpolate continuously. */
    s->feedback=(19661.0f+(float)size*(127.0f/100.0f)*85.0f)/32768.0f;
    s->damping=(26000.0f-(float)damping*(127.0f/100.0f)*160.0f)/32768.0f;
    s->wet=(float)wet*0.01f;
    return 1;
}
int bb_spring_init(bb_spring *s,unsigned sample_rate) {
    if(!s || sample_rate<8000 || sample_rate>48000)return 0;
    memset(s,0,sizeof *s);s->sample_rate=sample_rate;
    if(!bb_spring_set(s,55,55,35))return 0;
    s->length=s->target_length;s->pristine=1;return 1;
}
int bb_spring_process(bb_spring *restrict s,const float *restrict send,
                      float *restrict out,unsigned frames) {
    unsigned i,k;
    if(!s || !s->sample_rate || !send || !out || send==out || !frames || frames>128)return 0;
    /* Before the first excitation the line, filters and allpasses are exactly
     * zero. Keep sample-wise size slew and ring phase, without running the
     * silent filter chain. Never use this path again after excitation; a
     * quiet input must continue rendering an existing tail. */
    if(s->pristine) {
        for(i=0;i<frames && send[i]==0.0f;i++) {}
        if(i==frames) {
            for(i=0;i<frames;i++) {
                float delta=s->target_length-s->length;
                if(delta>1.0f/128.0f)delta=1.0f/128.0f;
                if(delta<-1.0f/128.0f)delta=-1.0f/128.0f;
                s->length+=delta;out[i]=0.0f;
            }
            s->write=(s->write+frames)&(BB_SPRING_RING-1u);
            return 1;
        }
        s->pristine=0;
    }
    const float target_length=s->target_length,damping=s->damping;
    const float feedback=s->feedback,wet=s->wet;
    const int settled=s->length==target_length;
    unsigned age[3]={0,0,0};float fraction[3]={0,0,0};
    if(settled) {
        const float delay[3]={s->length*0.5f,s->length*0.75f,s->length};
        for(k=0;k<3;k++){age[k]=(unsigned)delay[k];fraction[k]=delay[k]-(float)age[k];}
    }
    for(i=0;i<frames;i++) {
        unsigned j=(s->write&3u)*(BB_SPRING_STAGES+1u);
        float in=send[i],delta=target_length-s->length,r,p,x,y;
        if(!(in>=-64.0f && in<=64.0f)) {
            in=0;if(s->invalid_frames!=UINT32_MAX)s->invalid_frames++;
        }
        /* Match the nominal one-sample-per-128-frame source slew speed,
         * without making caller size a sound parameter. */
        if(settled) {
            y=spring_read_fixed(s,age[0],fraction[0])*4.0f+
              spring_read_fixed(s,age[1],fraction[1])*2.0f;
            r=spring_read_fixed(s,age[2],fraction[2]);
        } else {
            if(delta>1.0f/128.0f)delta=1.0f/128.0f;
            if(delta<-1.0f/128.0f)delta=-1.0f/128.0f;
            s->length+=delta;
            y=spring_read(s,s->length*0.5f)*4.0f+spring_read(s,s->length*0.75f)*2.0f;
            r=spring_read(s,s->length);
        }
        s->low+=(r-s->low)*damping;
        x=in*(2580.0f/32768.0f)+s->low*feedback;
        s->high+=(x-s->high)*(1.0f/64.0f);x-=s->high;
        p=s->allpass[j];s->allpass[j]=x;
        for(k=1;k<=BB_SPRING_STAGES;k++) {
            float old=s->allpass[j+k];
            x=(x-old)*(2867.0f/4096.0f)+p;
            p=old;s->allpass[j+k]=x;
        }
        if(x>1.0f)x=1.0f;if(x<-1.0f)x=-1.0f;
        s->line[s->write&(BB_SPRING_RING-1u)]=x;
        s->write=(s->write+1u)&(BB_SPRING_RING-1u);
        out[i]=y*wet;
    }
    return 1;
}
