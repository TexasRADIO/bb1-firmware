/* SPDX-License-Identifier: GPL-3.0-only */
#include "bb_phrase_index.h"
#include <string.h>
static int disjoint(const void *a,size_t n,const void *b,size_t m) {
    uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;
    return a&&b&&x<=UINTPTR_MAX-n&&y<=UINTPTR_MAX-m&&(x+n<=y||y+m<=x);
}
int bb_phrase_index_valid(const bb_phrase_index *p,uint32_t playable) {
    if(!p||!p->phrase.length||p->phrase.length>BB_STEPS||
       p->phrase.count>BB_FACTORY_DRUM_EVENTS||p->phrase.lattice>BB_FDL_TRIPLETS||p->phrase.bars>4)return 0;
    unsigned per=p->phrase.lattice==BB_FDL_SIXTEENTHS?16:p->phrase.lattice==BB_FDL_EIGHTHS?8:24;
    if((p->phrase.bars&&p->phrase.length!=p->phrase.bars*per)||p->start[0])return 0;
    if(p->start[p->phrase.length]!=p->phrase.count)return 0;
    /* This walk already proves every ordinal appears exactly once. Derive the
     * same global-accent mask here instead of decoding every event once more
     * in controls_prepare. Keep its header/control-modifier rules unchanged. */
    uint64_t accents=0;uint32_t voices=0;
    uint8_t seen[16]={0};
    for(unsigned step=1;step<=BB_STEPS;step++) {
        unsigned first=p->start[step-1],last=p->start[step],previous=0;
        if(first>last||last>p->phrase.count||(step>p->phrase.length&&first!=last))return 0;
        for(unsigned at=first;at<last;at++) {
            unsigned i=p->ordinal[at];bb_phrase_event e;
            if(i>=p->phrase.count||!bb_phrase_read(&p->phrase,i,&e)||e.step!=step||
               (seen[i>>3]&(1u<<(i&7)))||(at>first&&i<=previous))return 0;
            if(e.voice==BB_FDV_ACCENT||e.voice==BB_FDV_ACC){
                if(e.modifiers)return 0;
                accents|=UINT64_C(1)<<(e.step-1u);
            }else {
                if(!(playable&(UINT32_C(1)<<e.voice)))return 0;
                voices|=UINT32_C(1)<<e.voice;
            }
            if((e.modifiers&BB_FDM_RETRIGGER_LENGTH)&&!(e.modifiers&BB_FDM_RETRIGGER_RATE))return 0;
            seen[i>>3]|=(uint8_t)(1u<<(i&7));previous=i;
        }
    }
    if(accents!=p->controls.accents||voices!=p->voices)return 0;
    /* Coverage follows from count unique in-range ordinals and final sentinel. */
    for(unsigned i=p->phrase.count;i<BB_FACTORY_DRUM_EVENTS;i++)if(p->ordinal[i])return 0;
    return 1;
}
int bb_phrase_index_prepare(const bb_phrase *p,uint32_t playable,bb_phrase_index *out) {
    if(!disjoint(p,sizeof *p,out,sizeof *out))return 0;
    bb_phrase_controls controls;
    if(!bb_phrase_controls_prepare(p,&controls))return 0;
    /* Validate the entire object before publishing any destination byte. */
    uint32_t voices=0;
    for(unsigned i=0;i<p->count;i++) {
        bb_phrase_event e;if(!bb_phrase_read(p,i,&e))return 0;
        if(e.voice!=BB_FDV_ACCENT&&e.voice!=BB_FDV_ACC){
            if(!(playable&(UINT32_C(1)<<e.voice)))return 0;
            voices|=UINT32_C(1)<<e.voice;
        }
        if((e.modifiers&BB_FDM_RETRIGGER_LENGTH)&&!(e.modifiers&BB_FDM_RETRIGGER_RATE))return 0;
    }
    memset(out,0,sizeof *out);out->phrase=*p;out->controls=controls;out->voices=voices;
    /* Counting buckets, then stable insertion. Preparation is main-owner work.
     * start[length] is a sentinel; uint8_t represents the full count of 128.
     */
    for(unsigned i=0;i<p->count;i++) {
        unsigned step=((p->events[i]>>5)&63)+1;out->start[step]++;
    }
    for(unsigned step=1;step<=BB_STEPS;step++)out->start[step]+=out->start[step-1];
    uint8_t next[BB_STEPS];memcpy(next,out->start,sizeof next);
    for(unsigned i=0;i<p->count;i++) {
        unsigned step=(p->events[i]>>5)&63;out->ordinal[next[step]++]=(uint8_t)i;
    }
    return 1;
}
int bb_phrase_index_delays(const bb_phrase_index *p,unsigned step,uint64_t frame,
    uint32_t step_frames,uint32_t sample_rate,bb_delayed *q) {
    if(!disjoint(p,sizeof *p,q,sizeof *q)||!step||step>p->phrase.length||
       p->phrase.length>BB_STEPS||!step_frames||sample_rate<8000||sample_rate>192000||
       p->phrase.count>BB_FACTORY_DRUM_EVENTS)return BB_DELAYED_INVALID;
    unsigned first=p->start[step-1],last=p->start[step];
    if(first>last||last>p->phrase.count)return BB_DELAYED_INVALID;
    bb_delayed_event batch[BB_DELAYED_CAPACITY];unsigned count=0;
    for(unsigned at=first;at<last;at++) {
        unsigned i=p->ordinal[at];bb_phrase_event e;
        if(!bb_phrase_read(&p->phrase,i,&e)||e.step!=step)return BB_DELAYED_INVALID;
        if((e.modifiers&BB_FDM_RETRIGGER_LENGTH)&&!(e.modifiers&BB_FDM_RETRIGGER_RATE))return BB_DELAYED_INVALID;
        for(unsigned kind=1;kind<=3;kind++) {
            if(kind==1?!(e.modifiers&BB_FDM_FLAM):!(e.modifiers&BB_FDM_RETRIGGER_RATE))continue;
            if(count==BB_DELAYED_CAPACITY)return BB_DELAYED_FULL;
            uint64_t offset=kind==1?((uint64_t)sample_rate+39)/40:
                ((uint64_t)step_frames*(kind-1)+2)/3;
            if(offset>UINT64_MAX-frame)return BB_DELAYED_INVALID;
            batch[count++]=(bb_delayed_event){frame+offset,
                (uint32_t)p->phrase.events[i]|(i<<16)|(kind<<23),q->generation};
        }
    }
    return count?bb_delayed_offer(q,frame,batch,count):BB_DELAYED_OK;
}
