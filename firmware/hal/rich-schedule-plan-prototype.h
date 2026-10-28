/* SPDX-License-Identifier: GPL-3.0-only */
/* OFFLINE: private scheduler staging, not a live mailbox or admission API.
 * Prepared immutable index and validated owner state are prerequisites.
 * Failure discards staging: caller must never commit it on a zero result. */
#ifndef BB_RICH_SCHEDULE_PLAN_PROTOTYPE_H
#define BB_RICH_SCHEDULE_PLAN_PROTOTYPE_H
#include "bb_phrase_index.h"
#include "bb_swing.h"
#if !defined(BB_GROUP_TARGET_CANDIDATE)
#include <string.h>
#endif
typedef struct {
 bb_delayed queue;
 uint32_t phase,generation,primary,delayed;
 uint8_t step,pending,odd,swing,sounded;
} bb_rsp_state;
typedef struct {uint32_t payload;uint16_t offset;} bb_rsp_event;
typedef struct {
 bb_rsp_state next;
 unsigned count;
 bb_rsp_event events[192];
} bb_rsp_plan;
static int bb_rsp_add(bb_rsp_plan *out,unsigned offset,uint32_t payload){
 if(!out)return 1; /* recovery advances time without collecting missed strikes */
 if(out->count>=192)return 0;
 out->events[out->count++]=(bb_rsp_event){payload,(uint16_t)offset};return 1;
}
static int bb_rsp_advance(const bb_phrase_index *p,const bb_rsp_state *state,
 unsigned rate,unsigned bpm,unsigned swing,unsigned frames,uint64_t first,
 unsigned running,bb_rsp_state *next,bb_rsp_plan *out){
 if(!p||!state||!next||!frames||frames>128||UINT64_MAX-first<frames||
    rate<8000||rate>192000||bpm<30||bpm>300||!bb_swing_valid(swing)||
    !p->phrase.length||state->step>=p->phrase.length||state->odd>1||
    !bb_swing_valid(state->swing))return 0;
 /* Each accepted event is overwritten before count publishes it. Unused
  * staging entries are never consumed or hashed; preserve full next state. */
 if(out)out->count=0;*next=*state;
 if(!running)return 1;
 bb_rsp_state *s=next;bb_delayed *q=&s->queue;
 unsigned offset=0;
 while(offset<frames){
  uint64_t frame=first+offset;bb_delayed_event event;int status;
  while((status=bb_delayed_peek(q,&event))==BB_DELAYED_OK&&event.frame==frame){
   if(bb_delayed_pop(q,frame,&event)!=BB_DELAYED_OK||!bb_rsp_add(out,offset,event.payload))return 0;
  }
  if(status!=BB_DELAYED_OK&&status!=BB_DELAYED_WAIT)return 0;
  if(status==BB_DELAYED_OK&&event.frame<frame)return 0;
  unsigned lattice=p->phrase.lattice;
  uint32_t increment=bpm*(lattice==BB_FDL_EIGHTHS?200u:lattice==BB_FDL_TRIPLETS?600u:400u);
  if(s->pending){
   if(!s->odd)s->swing=(uint8_t)swing;
   uint32_t denom=bb_swing_denominator(rate,s->swing,s->odd);
   if(s->phase>=denom)return 0;
   uint32_t duration=(denom-s->phase+increment-1)/increment;
   for(unsigned at=p->start[s->step];at<p->start[s->step+1];at++){
    unsigned ordinal=p->ordinal[at];
    if(!bb_rsp_add(out,offset,p->phrase.events[ordinal]|(ordinal<<16)))return 0;
   }
   if(bb_phrase_index_delays(p,s->step+1,frame,duration,rate,q)!=BB_DELAYED_OK)return 0;
   s->sounded=1;s->pending=0;status=bb_delayed_peek(q,&event);
  }
  uint32_t denom=bb_swing_denominator(rate,s->swing,s->odd);
  if(s->phase>=denom)return 0;
  unsigned n=(denom-s->phase+increment-1)/increment;
  if(n>frames-offset)n=frames-offset;
  if(status==BB_DELAYED_OK&&event.frame<frame+n)n=(unsigned)(event.frame-frame);
  if(!n)return 0;
  s->phase+=n*increment;
  if(s->phase>=denom){s->phase-=denom;s->pending=1;s->odd^=1;s->step=(uint8_t)((s->step+1u)%p->phrase.length);}
  offset+=n;q->floor=first+offset;
 }
 return 1;
}
/* Normal render stages events; recovery uses the identical scheduler without DSP. */
static int bb_rsp_prepare(const bb_phrase_index *p,const bb_rsp_state *state,
 unsigned rate,unsigned bpm,unsigned swing,unsigned frames,uint64_t first,
 unsigned running,bb_rsp_plan *out){
 if(!out)return 0;
 return bb_rsp_advance(p,state,rate,bpm,swing,frames,first,running,&out->next,out);
}
static int bb_rsp_skip(const bb_phrase_index *p,const bb_rsp_state *state,
 unsigned rate,unsigned bpm,unsigned swing,unsigned frames,uint64_t first,bb_rsp_state *next){
 return bb_rsp_advance(p,state,rate,bpm,swing,frames,first,1,next,0);
}
#endif
