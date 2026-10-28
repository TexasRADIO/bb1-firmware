/* SPDX-License-Identifier: GPL-3.0-only */
#include "bb909_owner.h"
#include "bb909.h"
#include "drum909.h"
#include <string.h>
#include "group_validate.h"
#define GUARD UINT32_C(0x9090A71C)
static int overlap(const void *a,size_t an,const void *b,size_t bn){
    uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;return x<=y?y-x<an:x-y<bn;
}
static uint32_t hash(uint32_t h,const void *p,size_t n){
    const unsigned char *b=p;while(n--)h=(h^*b++)*UINT32_C(16777619);return h;
}
uint32_t bb909_request_check(const bb909_request *r){
    if(!r||(uintptr_t)r%_Alignof(bb909_request)||!r->ticket||!r->frames||r->frames>128||r->count>32)return 0;
    uint32_t h=hash(UINT32_C(2166136261),&r->ticket,3*sizeof(uint32_t));
    for(unsigned g=0;g<GG_COUNT;g++)if(r->groups[g].level>127||r->groups[g].muted>1)return 0;
    h=hash(h,r->groups,sizeof r->groups);
    return hash(h,r->events,r->count*sizeof(bb909_event));
}
static int valid(const bb909_owner *o){
    return o&&(uintptr_t)o%_Alignof(bb909_owner)==0&&o->guard==GUARD&&o->end_guard==GUARD
        &&o->ready&&!o->failed&&gg_valid(&o->groups);
}
int bb909_owner_init(bb909_owner *o,uint32_t previous_ticket,const void *pack,size_t bytes){
    if(!o||(uintptr_t)o%_Alignof(bb909_owner)||!pack||previous_ticket==UINT32_MAX
       ||bb909_size()>sizeof o->state||bb909_alignment()>_Alignof(bb909_owner_state)
       ||overlap(o,sizeof *o,pack,bytes))return 0;
    memset(o,0,sizeof *o);
    if(!bb909_init_pack(o->state.bytes,sizeof o->state,44100,pack,bytes))return 0;
    gg_init(&o->groups);
    o->guard=o->end_guard=GUARD;o->last_ticket=previous_ticket;o->ready=1;return 1;
}
static int event_valid(const bb909_event *e,unsigned frames){
    if(e->offset>=frames)return 0;
    if(e->kind==BB909_EVENT_TRIGGER)return e->voice<DR_NUM&&e->parameter==0&&e->value<=127;
    if(e->kind!=BB909_EVENT_SET||e->voice>DR_KIT||e->parameter>=drum909_nparams(e->voice))return 0;
    const x0x_param_t *p=drum909_param(e->voice,e->parameter);return p&&e->value<=p->max;
}
static uint32_t output_check(const bb909_owner *o){
    uint32_t h=hash(UINT32_C(2166136261),&o->last_ticket,sizeof o->last_ticket);
    h=hash(h,&o->frames,sizeof o->frames);
    h=hash(h,&o->groups,sizeof o->groups);
    for(unsigned b=0;b<4;b++)h=hash(h,o->bus[b],o->frames*sizeof(float));return h;
}
static int render(bb909_owner *o,unsigned begin,unsigned end){
    return begin==end||bb909_render_groups(o->state.bytes,&o->groups,o->bus[0]+begin,o->bus[1]+begin,
                                   o->bus[2]+begin,o->bus[3]+begin,end-begin);
}
int bb909_owner_run(bb909_owner *o,const bb909_request *r,uint32_t ticket,uint32_t *result){
    if(!valid(o)||!r||(uintptr_t)r%_Alignof(bb909_request)||!result||(uintptr_t)result%_Alignof(uint32_t)
       ||overlap(o,sizeof *o,r,sizeof *r)||overlap(o,sizeof *o,result,sizeof *result)
       ||overlap(r,sizeof *r,result,sizeof *result)||!ticket||ticket!=r->ticket
       ||ticket!=o->last_ticket+1u||!r->frames||r->frames>128||r->count>32
       ||r->check!=bb909_request_check(r))return 0;
    /* Preflight ALL events before any engine mutation. */
    for(unsigned i=0;i<r->count;i++)
        if(!event_valid(&r->events[i],r->frames)||(i&&r->events[i].offset<r->events[i-1].offset))return 0;
    /* Targets apply once per job, after all event and target validation.
     * Unchanged targets do not restart ramps; voices keep running at zero. */
    for(unsigned g=0;g<GG_COUNT;g++){
        if(r->groups[g].level>127||r->groups[g].muted>1)return 0;
    }
    for(unsigned g=0;g<GG_COUNT;g++)
        (void)gg_set(&o->groups,g,r->groups[g].level,r->groups[g].muted);
    unsigned at=0;
    for(unsigned i=0;i<r->count;i++){
        const bb909_event *e=&r->events[i];
        if(!render(o,at,e->offset))goto failed;at=e->offset;
        if(e->kind==BB909_EVENT_TRIGGER){if(!bb909_trigger(o->state.bytes,e->voice,e->value))goto failed;}
        else if(!bb909_set(o->state.bytes,e->voice,e->parameter,e->value))goto failed;
    }
    if(!render(o,at,r->frames))goto failed;
    o->last_ticket=ticket;o->frames=r->frames;*result=output_check(o);return 1;
failed:o->ready=0;o->failed=1;return 0; /* Partially advanced DSP may not be replayed. */
}
int bb909_owner_accept(const bb909_owner *o,uint32_t ticket,uint32_t result){
    if(!valid(o)||!o->frames||o->frames>128||!ticket||ticket!=o->last_ticket||output_check(o)!=result)return 0;
    for(unsigned b=0;b<4;b++)for(unsigned i=0;i<o->frames;i++){
        uint32_t bits;memcpy(&bits,&o->bus[b][i],sizeof bits);
        if((bits&UINT32_C(0x7f800000))==UINT32_C(0x7f800000))return 0;
    }
    return 1;
}
