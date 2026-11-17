/* SPDX-License-Identifier: GPL-3.0-only */
#include "bb_phrase_index.h"
#include <string.h>
static int disjoint(const void *a,size_t n,const void *b,size_t m) {
    uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;
    return a&&b&&x<=UINTPTR_MAX-n&&y<=UINTPTR_MAX-m&&(x+n<=y||y+m<=x);
}
int bb_phrase_index_valid(const bb_phrase_index *p,uint32_t playable) {
    if(!p||!bb_phrase_ratchets_valid(&p->phrase)||!p->phrase.length||p->phrase.length>BB_STEPS||
       p->phrase.count>BB_FACTORY_DRUM_EVENTS||p->phrase.lattice>BB_FDL_TRIPLETS||p->phrase.bars>4)return 0;
#if BB_WITH_STEP_CONDITIONS
    if(!bb_phrase_conditions_valid(&p->phrase))return 0;
#endif
#if BB_WITH_PHRASE_NUDGE
    if(!bb_phrase_nudges_valid(&p->phrase,&p->nudges))return 0;
#endif
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
#if BB_WITH_PHRASE_NUDGE
int bb_phrase_index_nudge_edit(const bb_phrase_index *before,unsigned voice,
                               unsigned step,int value,bb_phrase_index *after){
    if(!disjoint(before,sizeof *before,after,sizeof *after)||
       !bb_phrase_index_valid(before,UINT32_MAX))return 0;
    bb_phrase_index scratch=*before;
    if(!bb_phrase_nudge_set(&scratch.phrase,&scratch.nudges,voice,step,value))return 0;
    bb_phrase_nudge_queue_profile profile;
    if(!bb_phrase_nudge_queue_measure(&scratch.phrase,&scratch.nudges,&profile)||
       profile.combined>BB_DELAYED_CAPACITY)return 0;
    *after=scratch;return 1;
}
#endif
int bb_phrase_index_prepare(const bb_phrase *p,uint32_t playable,bb_phrase_index *out) {
    if(!disjoint(p,sizeof *p,out,sizeof *out))return 0;
#if BB_WITH_STEP_CONDITIONS
    if(!bb_phrase_conditions_valid(p))return 0;
#endif
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
int bb_phrase_index_hit_enabled(const bb_phrase_index *p,unsigned ordinal,uint32_t loop,unsigned fill){
    if(!p||ordinal>=p->phrase.count)return 0;
#if BB_WITH_STEP_CONDITIONS
    return bb_phrase_condition_play(bb_phrase_condition_prepared(&p->phrase,ordinal),loop,fill);
#else
    (void)loop;(void)fill;return 1;
#endif
}
static int delays(const bb_phrase_index *p,unsigned step,uint64_t frame,
    uint32_t step_frames,uint32_t sample_rate,uint32_t loop,unsigned fill,
    bb_delayed *q,unsigned skip_nudged,unsigned opening) {
    if(!disjoint(p,sizeof *p,q,sizeof *q)||!step||step>p->phrase.length||
       p->phrase.length>BB_STEPS||!step_frames||sample_rate<8000||sample_rate>192000||
       p->phrase.count>BB_FACTORY_DRUM_EVENTS)return BB_DELAYED_INVALID;
    unsigned first=p->start[step-1],last=p->start[step];
    if(first>last||last>p->phrase.count)return BB_DELAYED_INVALID;
    /* Keep stack growth independent of queue capacity: at most16 retained
     * source modifiers plus six count/fraction groups for x2/x3/x4. */
    enum {LEGACY_BATCH=BB_PHRASE_MODIFIER_BATCH_LIMIT,USER_GROUPS=6};
    bb_delayed_event batch[LEGACY_BATCH+USER_GROUPS];unsigned count=0;
    for(unsigned at=first;at<last;at++) {
        unsigned i=p->ordinal[at];bb_phrase_event e;
        if(!bb_phrase_read(&p->phrase,i,&e)||e.step!=step)return BB_DELAYED_INVALID;
        if(!bb_phrase_index_hit_enabled(p,i,loop,fill))continue;
#if BB_WITH_PHRASE_NUDGE
        int nudge=bb_phrase_nudge_prepared(&p->nudges,e.voice,e.step-1u);
        if(skip_nudged&&nudge&&!(opening&&nudge<0))continue;
#else
        (void)skip_nudged;(void)opening;
#endif
        if((e.modifiers&BB_FDM_RETRIGGER_LENGTH)&&!(e.modifiers&BB_FDM_RETRIGGER_RATE))return BB_DELAYED_INVALID;
        for(unsigned kind=1;kind<=3;kind++) {
            if(kind==1?!(e.modifiers&BB_FDM_FLAM):!(e.modifiers&BB_FDM_RETRIGGER_RATE))continue;
            if(count==LEGACY_BATCH)return BB_DELAYED_FULL;
            uint64_t offset=kind==1?((uint64_t)sample_rate+39)/40:
                ((uint64_t)step_frames*(kind-1)+2)/3;
            if(offset>UINT64_MAX-frame)return BB_DELAYED_INVALID;
            batch[count++]=(bb_delayed_event){frame+offset,
                (uint32_t)p->phrase.events[i]|(i<<16)|(kind<<23),q->generation};
        }
    }
    unsigned classes=0;
    for(unsigned at=first;at<last;at++){
        unsigned i=p->ordinal[at];bb_phrase_event e;
        if(!bb_phrase_read(&p->phrase,i,&e))return BB_DELAYED_INVALID;
        if(!bb_phrase_index_hit_enabled(p,i,loop,fill))continue;
#if BB_WITH_PHRASE_NUDGE
        int nudge=bb_phrase_nudge_prepared(&p->nudges,e.voice,e.step-1u);
        if(skip_nudged&&nudge&&!(opening&&nudge<0))continue;
#endif
        if(e.voice==BB_FDV_ACCENT||e.voice==BB_FDV_ACC)continue;
        unsigned n=bb_phrase_ratchet_count(&p->phrase,i);if(n>1u)classes|=1u<<n;
    }
    for(unsigned n=2;n<=4;n++)if(classes&(1u<<n))for(unsigned k=1;k<n;k++){
        uint64_t offset=((uint64_t)step_frames*k+n-1u)/n;
        if(offset>=step_frames||offset>UINT64_MAX-frame)return BB_DELAYED_INVALID;
        batch[count++]=(bb_delayed_event){frame+offset,
            BB_RATCHET_GROUP|(step-1u)|((n-1u)<<6)|(k<<8),q->generation};
    }
    return count?bb_delayed_offer(q,frame,batch,count):BB_DELAYED_OK;
}
int bb_phrase_index_delays(const bb_phrase_index *p,unsigned step,uint64_t frame,
    uint32_t step_frames,uint32_t sample_rate,uint32_t loop,unsigned fill,bb_delayed *q) {
    return delays(p,step,frame,step_frames,sample_rate,loop,fill,q,0,0);
}
#if BB_WITH_PHRASE_NUDGE
int bb_phrase_index_delays_nudged(const bb_phrase_index *p,unsigned step,
    uint64_t frame,uint32_t step_frames,uint32_t next_step_frames,
    uint32_t period_frames,uint32_t sample_rate,uint32_t loop,unsigned fill,
    unsigned opening,bb_delayed *q){
    if(!p||!q||!disjoint(p,sizeof *p,q,sizeof *q)||!step||step>p->phrase.length||
       opening>1u||(opening&&step!=1u))return BB_DELAYED_INVALID;
    bb_delayed scratch=*q;
    int status=delays(p,step,frame,step_frames,sample_rate,loop,fill,&scratch,1,opening);
    if(status!=BB_DELAYED_OK)return status;
    status=bb_phrase_nudge_schedule_scratch(&p->phrase,&p->nudges,step-1u,frame,
        step_frames,next_step_frames,period_frames,sample_rate,loop,fill,&scratch);
    if(status!=BB_DELAYED_OK)return status;
    *q=scratch;return BB_DELAYED_OK;
}
#endif

static void ratchet_snapshot(const bb_phrase_index *p,unsigned step,uint32_t loop,
                             unsigned fill,unsigned skip_nudged,unsigned opening,
                             bb_ratchet_snapshot *out){
    memset(out,0,sizeof *out);out->step=(uint8_t)step;
    if(step<p->phrase.length)for(unsigned at=p->start[step];at<p->start[step+1];at++){
        unsigned ordinal=p->ordinal[at];if(!bb_phrase_index_hit_enabled(p,ordinal,loop,fill))continue;
#if BB_WITH_PHRASE_NUDGE
        bb_phrase_event event;if(!bb_phrase_read(&p->phrase,ordinal,&event))continue;
        int nudge=bb_phrase_nudge_prepared(&p->nudges,event.voice,event.step-1u);
        if(skip_nudged&&nudge&&!(opening&&nudge<0))continue;
#else
        (void)skip_nudged;(void)opening;
#endif
        unsigned count=bb_phrase_ratchet_count(&p->phrase,ordinal),shift=2u*(ordinal&3u);
        out->event_counts[ordinal>>2]|=(uint8_t)((count-1u)<<shift);
        if(count>1u)out->active=1;
    }
}
void bb_phrase_ratchet_snapshot(const bb_phrase_index *p,unsigned step,uint32_t loop,unsigned fill,bb_ratchet_snapshot *out){
    ratchet_snapshot(p,step,loop,fill,0,0,out);
}
#if BB_WITH_PHRASE_NUDGE
void bb_phrase_ratchet_snapshot_nudged(const bb_phrase_index *p,unsigned step,
    uint32_t loop,unsigned fill,unsigned opening,bb_ratchet_snapshot *out){
    if(opening>1u||(opening&&step)){
        if(out)memset(out,0,sizeof *out);return;
    }
    ratchet_snapshot(p,step,loop,fill,1,opening,out);
}
#endif
int bb_phrase_ratchet_group_hit(const bb_phrase_index *p,const bb_ratchet_snapshot *snapshot,
    uint32_t group,unsigned ordinal,uint32_t *payload){
    if(!p||!snapshot||!payload||!(group&BB_RATCHET_GROUP)||(group&~(BB_RATCHET_GROUP|UINT32_C(0x3ff)))||
       !snapshot->active||ordinal>=p->phrase.count)return -1;
    unsigned step=group&63u,n=1u+((group>>6)&3u),k=(group>>8)&3u;
    if(step!=snapshot->step||step>=p->phrase.length||n<2u||!k||k>=n)return -1;
    bb_phrase_event e;if(!bb_phrase_read(&p->phrase,ordinal,&e))return -1;
    if(e.step!=step+1u||e.voice==BB_FDV_ACCENT||e.voice==BB_FDV_ACC)return 0;
    unsigned count=1u+((snapshot->event_counts[ordinal>>2]>>(2u*(ordinal&3u)))&3u);
    if(count!=n)return 0;
    *payload=(uint32_t)p->phrase.events[ordinal]|(ordinal<<16)|(2u<<23)|BB_RATCHET_STRIKE;
    return 1;
}
