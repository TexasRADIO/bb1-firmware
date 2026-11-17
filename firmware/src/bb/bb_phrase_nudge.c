/* SPDX-License-Identifier: GPL-3.0-only */
#include "bb_phrase_nudge.h"
#include <string.h>

_Static_assert(sizeof(bb_phrase_nudges)==34,"bounded nudge sidecar cost");

enum {
    BB_NG_PRIMARY,
    BB_NG_FLAM,
    BB_NG_RETRIGGER_1,
    BB_NG_RETRIGGER_2,
    BB_NG_RATCHET
};
#define BB_NG_KIND_SHIFT 15u
#define BB_NG_COUNT_SHIFT 18u
#define BB_NG_PART_SHIFT 20u
#define BB_NG_FIELDS UINT32_C(0x003fffff)

static unsigned key_of(unsigned voice,unsigned step){return (step<<5)|voice;}
static unsigned entry_key(uint16_t entry){return entry&0x7ffu;}
static int entry_nudge(uint16_t entry){return (int)((entry>>11)&15u)-8;}
static uint16_t entry_make(unsigned key,int nudge){
    return (uint16_t)(key|((unsigned)(nudge+8)<<11));
}
static int disjoint(const void *a,size_t n,const void *b,size_t m){
    uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;
    return a&&b&&x<=UINTPTR_MAX-n&&y<=UINTPTR_MAX-m&&(x+n<=y||y+m<=x);
}
static int phrase_cell(const bb_phrase *p,unsigned voice,unsigned step){
    if(!p||voice>=BB_PHRASE_VOICES||voice==BB_FDV_ACCENT||voice==BB_FDV_ACC||
       step>=p->length)return 0;
    for(unsigned i=0;i<p->count;i++){
        bb_phrase_event e;
        if(!bb_phrase_read(p,i,&e))return 0;
        if(e.voice==voice&&e.step==step+1u)return 1;
    }
    return 0;
}
static int cell_ratchet_groups(const bb_phrase *p,unsigned voice,unsigned step,
                               unsigned *out){
    if(!out||!phrase_cell(p,voice,step))return 0;
    unsigned classes=0,groups=0;
    for(unsigned i=0;i<p->count;i++){
        bb_phrase_event event;if(!bb_phrase_read(p,i,&event))return 0;
        if(event.voice==voice&&event.step==step+1u){
            unsigned count=bb_phrase_ratchet_count(p,i);
            if(count>1u)classes|=1u<<count;
        }
    }
    for(unsigned count=2;count<=4;count++)if(classes&(1u<<count))groups+=count-1u;
    *out=groups;return 1;
}
int bb_phrase_nudges_valid(const bb_phrase *p,const bb_phrase_nudges *n){
    if(!p||!n||!bb_phrase_ratchets_valid(p)||n->count>BB_PHRASE_NUDGE_EDITS||n->reserved)return 0;
#if BB_WITH_STEP_CONDITIONS
    if(!bb_phrase_conditions_valid(p))return 0;
#endif
    unsigned previous=0;
    for(unsigned i=0;i<BB_PHRASE_NUDGE_EDITS;i++){
        uint16_t entry=n->cell[i];
        if(i>=n->count){if(entry)return 0;continue;}
        if(entry&0x8000u)return 0;
        unsigned key=entry_key(entry),voice=key&31u,step=key>>5;
        int value=entry_nudge(entry);
        if(!value||!phrase_cell(p,voice,step)||(i&&key<=previous))return 0;
        previous=key;
    }
    return 1;
}
int bb_phrase_nudge_get(const bb_phrase *p,const bb_phrase_nudges *n,
                        unsigned voice,unsigned step,int *out){
    if(!out||!bb_phrase_nudges_valid(p,n)||!phrase_cell(p,voice,step))return 0;
    unsigned key=key_of(voice,step);int value=0;
    for(unsigned i=0;i<n->count;i++)if(entry_key(n->cell[i])==key){value=entry_nudge(n->cell[i]);break;}
    *out=value;return 1;
}
int bb_phrase_nudge_prepared(const bb_phrase_nudges *n,unsigned voice,unsigned step){
    if(!n||voice>=BB_PHRASE_VOICES||step>=BB_STEPS||n->count>BB_PHRASE_NUDGE_EDITS||n->reserved)return 0;
    unsigned key=key_of(voice,step);
    for(unsigned i=0;i<n->count;i++){
        uint16_t entry=n->cell[i];unsigned at=entry_key(entry);
        if(entry&0x8000u||!entry_nudge(entry)||(i&&at<=entry_key(n->cell[i-1])))return 0;
        if(at==key)return entry_nudge(entry);
        if(at>key)break;
    }
    return 0;
}
int bb_phrase_nudge_set(const bb_phrase *p,bb_phrase_nudges *n,
                        unsigned voice,unsigned step,int value){
    if(!bb_phrase_nudges_valid(p,n)||value<-8||value>7||!phrase_cell(p,voice,step))return 0;
    unsigned key=key_of(voice,step),at=0;
    while(at<n->count&&entry_key(n->cell[at])<key)at++;
    int found=at<n->count&&entry_key(n->cell[at])==key;
    if(!value){
        if(!found)return 1;
        for(unsigned i=at;i+1u<n->count;i++)n->cell[i]=n->cell[i+1u];
        n->cell[--n->count]=0;return 1;
    }
    if(found){n->cell[at]=entry_make(key,value);return 1;}
    if(n->count==BB_PHRASE_NUDGE_EDITS)return 0;
    for(unsigned i=n->count;i>at;i--)n->cell[i]=n->cell[i-1u];
    n->cell[at]=entry_make(key,value);n->count++;return 1;
}
int bb_phrase_nudges_rebase(const bb_phrase *before,
                            const bb_phrase_nudges *old,
                            const bb_phrase *after,
                            bb_phrase_nudges *out){
    bb_phrase_nudges next={0};
    if(!out||!bb_phrase_nudges_valid(before,old)||
       !bb_phrase_nudges_valid(after,&next)||
       !disjoint(out,sizeof *out,before,sizeof *before)||
       !disjoint(out,sizeof *out,after,sizeof *after))return 0;
    for(unsigned i=0;i<old->count;i++){
        unsigned key=entry_key(old->cell[i]),voice=key&31u,step=key>>5;
        if(phrase_cell(after,voice,step)&&
           !bb_phrase_nudge_set(after,&next,voice,step,entry_nudge(old->cell[i])))return 0;
    }
    *out=next;return 1;
}
int bb_phrase_nudge_primary(const bb_phrase *p,const bb_phrase_nudges *n,
                            unsigned voice,unsigned step,unsigned opening){
    int value;
    if(opening>1u||!bb_phrase_nudge_get(p,n,voice,step,&value))return -1;
    return !value||(value<0&&opening);
}
static int event_before(const bb_phrase_nudge_event *a,const bb_phrase_nudge_event *b){
    if(a->frame!=b->frame)return a->frame<b->frame;
    unsigned ak=key_of(a->voice,a->step),bk=key_of(b->voice,b->step);
    return ak<bk||(ak==bk&&a->early>b->early);
}
int bb_phrase_nudge_plan(const bb_phrase *p,const bb_phrase_nudges *n,
                         unsigned step,uint64_t frame,uint32_t step_frames,
                         uint32_t next_step_frames,uint32_t period_frames,
                         bb_phrase_nudge_event *out,
                         unsigned capacity,unsigned *count){
    if(!count||!bb_phrase_nudges_valid(p,n)||step>=p->length||!step_frames||
       !next_step_frames||period_frames<16u||capacity>BB_PHRASE_NUDGE_EDITS||
       (capacity&&!disjoint(out,capacity*sizeof *out,p,sizeof *p))||
       (capacity&&!disjoint(out,capacity*sizeof *out,n,sizeof *n)))return 0;
    unsigned next=(step+1u)%p->length,needed=0;
    for(unsigned i=0;i<n->count;i++){
        unsigned key=entry_key(n->cell[i]),at=key>>5;int value=entry_nudge(n->cell[i]);
        if((at==step&&value>0)||(at==next&&value<0)){
            uint64_t amount=(uint64_t)(value>0?value:-value)*period_frames/16u;
            uint64_t offset=value>0?amount:step_frames>amount?step_frames-amount:1u;
            if(!offset||offset>UINT64_MAX-frame)return 0;
            needed++;
        }
    }
    if(needed>capacity)return 0;
    unsigned written=0;
    for(unsigned i=0;i<n->count;i++){
        unsigned key=entry_key(n->cell[i]),at=key>>5,voice=key&31u;int value=entry_nudge(n->cell[i]);
        if(!((at==step&&value>0)||(at==next&&value<0)))continue;
        uint64_t amount=(uint64_t)(value>0?value:-value)*period_frames/16u;
        uint64_t offset=value>0?amount:step_frames>amount?step_frames-amount:1u;
        bb_phrase_nudge_event event={frame+offset,value<0?next_step_frames:step_frames,
            (uint8_t)voice,(uint8_t)at,(uint8_t)(value<0),0};
        unsigned pos=written;
        while(pos&&event_before(&event,&out[pos-1])){out[pos]=out[pos-1];pos--;}
        out[pos]=event;written++;
    }
    *count=written;return 1;
}
int bb_phrase_nudge_group(const bb_phrase *p,const bb_phrase_nudges *n,
                          const bb_phrase_nudge_event *event,uint32_t loop,
                          unsigned fill,uint32_t *out){
    if(!event||!out||!disjoint(out,sizeof *out,p,sizeof *p)||
       !disjoint(out,sizeof *out,n,sizeof *n)||
       !disjoint(out,sizeof *out,event,sizeof *event)||
       fill>1u||event->reserved||event->early>1u||
       !bb_phrase_nudges_valid(p,n)||event->voice>=BB_PHRASE_VOICES||
       event->step>=p->length)return 0;
    int value;if(!bb_phrase_nudge_get(p,n,event->voice,event->step,&value)||
       !value||event->early!=(unsigned)(value<0))return 0;
    *out=BB_NUDGE_GROUP|event->step|(event->voice<<6)|
        ((loop&3u)<<11)|((unsigned)(loop==0)<<13)|(fill<<14);return 1;
}
int bb_phrase_nudge_group_hit(const bb_phrase *p,const bb_phrase_nudges *n,
                              uint32_t group,unsigned ordinal,uint32_t *out){
    if(!out||!disjoint(out,sizeof *out,p,sizeof *p)||
       !disjoint(out,sizeof *out,n,sizeof *n)||
       !(group&BB_NUDGE_GROUP)||(group&~(BB_NUDGE_GROUP|BB_NG_FIELDS))||
       !bb_phrase_nudges_valid(p,n)||ordinal>=p->count)return -1;
    unsigned step=group&63u,voice=(group>>6)&31u;
    unsigned kind=(group>>BB_NG_KIND_SHIFT)&7u;
    unsigned count=1u+((group>>BB_NG_COUNT_SHIFT)&3u);
    unsigned part=(group>>BB_NG_PART_SHIFT)&3u;int value;
    if(step>=p->length||voice>=BB_PHRASE_VOICES||
       !bb_phrase_nudge_get(p,n,voice,step,&value)||!value)return -1;
    if(kind>BB_NG_RATCHET||
       (kind!=BB_NG_RATCHET&&(count!=1u||part))||
       (kind==BB_NG_RATCHET&&(count<2u||count>4u||!part||part>=count)))return -1;
    bb_phrase_event event;if(!bb_phrase_read(p,ordinal,&event))return -1;
    if(event.step!=step+1u||event.voice!=voice)return 0;
#if BB_WITH_STEP_CONDITIONS
    unsigned phase=(group>>11)&3u,first=(group>>13)&1u,fill=(group>>14)&1u;
    if(first&&phase)return -1;
    uint32_t loop=first?0u:(phase?phase:4u);
    if(!bb_phrase_condition_play(bb_phrase_condition_prepared(p,ordinal),loop,fill))return 0;
#endif
    if(kind==BB_NG_FLAM&&!(event.modifiers&BB_FDM_FLAM))return 0;
    if((kind==BB_NG_RETRIGGER_1||kind==BB_NG_RETRIGGER_2)&&
       !(event.modifiers&BB_FDM_RETRIGGER_RATE))return 0;
    if(kind==BB_NG_RATCHET&&bb_phrase_ratchet_count(p,ordinal)!=count)return 0;
    uint32_t payload=(uint32_t)p->events[ordinal]|(ordinal<<16);
    if(kind==BB_NG_FLAM)payload|=UINT32_C(1)<<23;
    else if(kind==BB_NG_RETRIGGER_1||kind==BB_NG_RETRIGGER_2)payload|=kind<<23;
    else if(kind==BB_NG_RATCHET)payload|=(UINT32_C(2)<<23)|UINT32_C(0x02000000);
    *out=payload;return 1;
}

static int add_group(bb_delayed_event *batch,unsigned capacity,unsigned *count,uint64_t origin,
                     uint64_t offset,uint32_t base,unsigned kind,unsigned n,
                     unsigned part,uint32_t generation){
    if(*count==capacity)return BB_DELAYED_FULL;
    if(offset>UINT64_MAX-origin)return BB_DELAYED_INVALID;
    uint32_t payload=base|(kind<<BB_NG_KIND_SHIFT);
    if(kind==BB_NG_RATCHET)payload|=((n-1u)<<BB_NG_COUNT_SHIFT)|(part<<BB_NG_PART_SHIFT);
    batch[(*count)++]=(bb_delayed_event){origin+offset,payload,generation};return BB_DELAYED_OK;
}
static int build_one(const bb_phrase *p,const bb_phrase_nudges *n,
                     const bb_phrase_nudge_event *event,unsigned source_step,
                     uint32_t loop,unsigned fill,uint32_t sample_rate,
                     uint32_t generation,bb_delayed_event *batch,
                     unsigned capacity,unsigned *written){
    unsigned expected=event->early?(source_step+1u)%p->length:source_step;
    if(event->step!=expected||!event->source_frames)return BB_DELAYED_INVALID;
    uint32_t event_loop=loop;
    if(event->early&&event->step==0u){
        if(loop==UINT32_MAX)return BB_DELAYED_INVALID;
        event_loop++;
    }
    uint32_t payload;
    if(!bb_phrase_nudge_group(p,n,event,event_loop,fill,&payload))return BB_DELAYED_INVALID;
    unsigned flam=0,retrigger=0,classes=0;
    for(unsigned ordinal=0;ordinal<p->count;ordinal++){
        bb_phrase_event source;if(!bb_phrase_read(p,ordinal,&source))return BB_DELAYED_INVALID;
        if(source.step!=event->step+1u||source.voice!=event->voice)continue;
        flam|=!!(source.modifiers&BB_FDM_FLAM);
        retrigger|=!!(source.modifiers&BB_FDM_RETRIGGER_RATE);
        unsigned r=bb_phrase_ratchet_count(p,ordinal);if(r>1u)classes|=1u<<r;
    }
    int status=add_group(batch,capacity,written,event->frame,0,payload,BB_NG_PRIMARY,1,0,generation);
    if(status!=BB_DELAYED_OK)return status;
    if(flam){
        status=add_group(batch,capacity,written,event->frame,((uint64_t)sample_rate+39u)/40u,
                         payload,BB_NG_FLAM,1,0,generation);
        if(status!=BB_DELAYED_OK)return status;
    }
    if(retrigger)for(unsigned part=1;part<=2;part++){
        status=add_group(batch,capacity,written,event->frame,
                         ((uint64_t)event->source_frames*part+2u)/3u,
                         payload,part==1?BB_NG_RETRIGGER_1:BB_NG_RETRIGGER_2,
                         1,0,generation);
        if(status!=BB_DELAYED_OK)return status;
    }
    for(unsigned r=2;r<=4;r++)if(classes&(1u<<r))for(unsigned part=1;part<r;part++){
        status=add_group(batch,capacity,written,event->frame,
                         ((uint64_t)event->source_frames*part+r-1u)/r,
                         payload,BB_NG_RATCHET,r,part,generation);
        if(status!=BB_DELAYED_OK)return status;
    }
    return BB_DELAYED_OK;
}
int bb_phrase_nudge_offer(const bb_phrase *p,const bb_phrase_nudges *n,
                          const bb_phrase_nudge_event *plan,unsigned count,
                          unsigned source_step,uint32_t loop,unsigned fill,
                          uint32_t sample_rate,uint64_t now,bb_delayed *queue){
    if(!queue||!plan||!count||count>BB_PHRASE_NUDGE_EDITS||
       !disjoint(queue,sizeof *queue,p,sizeof *p)||
       !disjoint(queue,sizeof *queue,n,sizeof *n)||
       !disjoint(queue,sizeof *queue,plan,count*sizeof *plan)||
       !bb_phrase_nudges_valid(p,n)||source_step>=p->length||fill>1u||
       sample_rate<8000u||sample_rate>192000u)return BB_DELAYED_INVALID;
    bb_delayed_event batch[BB_DELAYED_CAPACITY];unsigned written=0;
    for(unsigned i=0;i<count;i++){
        if(plan[i].frame<now)return BB_DELAYED_INVALID;
        int status=build_one(p,n,&plan[i],source_step,loop,fill,sample_rate,
                             queue->generation,batch,BB_DELAYED_CAPACITY,&written);
        if(status!=BB_DELAYED_OK)return status;
    }
    return bb_delayed_offer(queue,now,batch,written);
}
int bb_phrase_nudge_schedule_scratch(const bb_phrase *p,const bb_phrase_nudges *n,
    unsigned source_step,uint64_t frame,uint32_t step_frames,
    uint32_t next_step_frames,uint32_t period_frames,uint32_t sample_rate,
    uint32_t loop,unsigned fill,bb_delayed *queue){
    if(!queue||!disjoint(queue,sizeof *queue,p,sizeof *p)||
       !disjoint(queue,sizeof *queue,n,sizeof *n)||
       !bb_phrase_nudges_valid(p,n)||source_step>=p->length||!step_frames||
       !next_step_frames||period_frames<16u||sample_rate<8000u||sample_rate>192000u||fill>1u)return BB_DELAYED_INVALID;
    unsigned next=(source_step+1u)%p->length;
    for(unsigned i=0;i<n->count;i++){
        unsigned key=entry_key(n->cell[i]),step=key>>5,voice=key&31u;int value=entry_nudge(n->cell[i]);
        if(!((step==source_step&&value>0)||(step==next&&value<0)))continue;
        uint64_t amount=(uint64_t)(value>0?value:-value)*period_frames/16u;
        uint64_t offset=value>0?amount:step_frames>amount?step_frames-amount:1u;
        if(!offset||offset>UINT64_MAX-frame)return BB_DELAYED_INVALID;
        bb_phrase_nudge_event event={frame+offset,value<0?next_step_frames:step_frames,
            (uint8_t)voice,(uint8_t)step,(uint8_t)(value<0),0};
        bb_delayed_event batch[7];unsigned written=0;
        int status=build_one(p,n,&event,source_step,loop,fill,sample_rate,
                             queue->generation,batch,7,&written);
        if(status!=BB_DELAYED_OK)return status;
        status=bb_delayed_offer(queue,frame,batch,written);
        if(status!=BB_DELAYED_OK)return status;
    }
    return BB_DELAYED_OK;
}
int bb_phrase_nudge_queue_measure(const bb_phrase *p,const bb_phrase_nudges *n,
                                  bb_phrase_nudge_queue_profile *out){
    if(!out||!bb_phrase_nudges_valid(p,n))return 0;
    uint8_t repeats[BB_STEPS]={0},flams[BB_STEPS]={0},groups[BB_STEPS]={0};
    for(unsigned step=0;step<p->length;step++){
        unsigned classes=0;
        for(unsigned i=0;i<p->count;i++)if(((p->events[i]>>5)&63u)==step){
            unsigned mods=p->events[i]>>13,voice=p->events[i]&31u;
            flams[step]+=(uint8_t)!!(mods&BB_FDM_FLAM);
            repeats[step]+=(uint8_t)(2u*!!(mods&BB_FDM_RETRIGGER_RATE));
            if(voice!=BB_FDV_ACCENT&&voice!=BB_FDV_ACC){
                unsigned count=bb_phrase_ratchet_count(p,i);if(count>1u)classes|=1u<<count;
            }
        }
        for(unsigned count=2;count<=4;count++)if(classes&(1u<<count))groups[step]+=(uint8_t)(count-1u);
    }
    bb_phrase_nudge_queue_profile profile={0};
    for(unsigned step=0;step<p->length;step++){
        unsigned base=groups[step],nudges=0;
        for(unsigned back=0;back<22;back++)base+=repeats[(step+p->length-back%p->length)%p->length];
        for(unsigned back=0;back<3;back++)base+=flams[(step+p->length-back%p->length)%p->length];
        unsigned next=(step+1u)%p->length;
        unsigned previous=step?step-1u:p->length-1u;
        for(unsigned i=0;i<n->count;i++){
            unsigned key=entry_key(n->cell[i]),at=key>>5,voice=key&31u;
            int value=entry_nudge(n->cell[i]);unsigned ratchet_groups=0;
            if(!cell_ratchet_groups(p,voice,at,&ratchet_groups))return 0;
            if((at==step&&value>0)||(at==next&&value<0))nudges+=1u+ratchet_groups;
            /* A late previous-step origin can carry its ratchet subdivisions
             * over this boundary.  Legacy flam/retrigger rolling sums already
             * reserve their longer horizons; retained ratchet groups do not. */
            if(at==previous&&value>0)nudges+=ratchet_groups;
        }
        unsigned combined=base+nudges;
        if(combined>profile.combined){
            profile.base=(uint16_t)base;profile.nudges=(uint16_t)nudges;
            profile.combined=(uint16_t)combined;profile.step=(uint8_t)step;
        }
    }
    *out=profile;return 1;
}
