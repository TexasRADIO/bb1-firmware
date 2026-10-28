/* SPDX-License-Identifier: GPL-3.0-only */
/* OFFLINE compact actual-event protocol. Normal raw-float po_request remains
 * unchanged. This distinct-guard packet encodes actual seven-bit bb_hit values,
 * descriptor-bounded pots and group targets; it does not quantize raw floats.
 */
#ifndef BB_PO_COMPACT_EXPERIMENT_H
#define BB_PO_COMPACT_EXPERIMENT_H
#include "route.h"
#define PC_GUARD UINT32_C(0x50435433)
enum { PC_EVENTS=64 };
typedef struct {uint8_t tag,param,value;} pc_event;
typedef struct {bb_drum_sound sound[2];pc_event events[PC_EVENTS];uint32_t mask;} pc_payload;
typedef struct {
    uint32_t guard,ticket,kits,frames,previous_state,count;
    pc_payload payload;uint32_t check;
} pc_request;
_Static_assert(sizeof(pc_event)==3,"three-byte semantic event,not a packed pointer");
_Static_assert(sizeof(pc_payload)==512&&sizeof(pc_request)==540,"unchanged wire budget");
_Static_assert(offsetof(pc_payload,events)==316,"two full portable patches precede events");
static uint32_t pc_hash(const pc_request *r) {
    return bb_c1_drum_hash(PC_GUARD,r,offsetof(pc_request,check));
}
static int pc_decode(const pc_event *e,unsigned kits,po_command *out) {
    if(!e||!out||!kits||kits>2)return 0;
    unsigned type=e->tag>>5,kit=(e->tag>>4)&1u,index=e->tag&15u;
    if(kit>=kits)return 0;
    po_command c={(uint8_t)type,(uint8_t)kit,(uint8_t)index,e->param,e->value};
    if(type==PO_HIT) {
        if(index>=D8_NUM||e->param||e->value>127)return 0;
        float velocity=e->value==127?1.0f:e->value*(D8_VEL_NORMAL/88.0f);
        __builtin_memcpy(&c.value,&velocity,sizeof c.value);
    } else if(type==PO_GROUP) {
        if(index>=GG_COUNT||e->param>1||e->value>127)return 0;
        c.param=0;c.value=e->value|((uint32_t)e->param<<8);
    } else if(type!=PO_SET)return 0;
    if(!po_command_valid(&c,kits))return 0;*out=c;return 1;
}
static __attribute__((noinline)) int pc_run(po_owner *o,const pc_request *wire,po_result *result) {
    if(!o)return 0;
    if(!wire||!result||!bb_rt_separate(o,sizeof *o,wire,sizeof *wire)||
       !bb_rt_separate(o,sizeof *o,result,sizeof *result)||!bb_rt_separate(wire,sizeof *wire,result,sizeof *result))return po_fail(o);
    if(o->phase!=PO_READY||!po_shape(o))return po_fail(o);
    union {pc_request compact;po_request normal;} local;
    bb_c1_drum_copy(&local.compact,wire,sizeof local.compact);
    pc_request *r=&local.compact;
    if(r->guard!=PC_GUARD||r->check!=pc_hash(r)||!r->ticket||o->ticket==UINT32_MAX||
       r->ticket!=o->ticket+1u||r->kits!=o->payload.kits||!r->frames||r->frames>128||
       r->count>PC_EVENTS||r->previous_state!=o->state||o->state!=po_state_hash(&o->payload)||
       !r->payload.mask||(r->payload.mask&~((1u<<r->kits)-1u)))return po_fail(o);
    for(unsigned k=0;k<r->kits;k++)if(!bb_group_state_valid(&o->payload.groups[k]))return po_fail(o);
    for(unsigned k=0;k<2;k++) {
        if(r->payload.mask&(1u<<k)){if(!po_sound_valid(&r->payload.sound[k]))return po_fail(o);}
        else if(!po_zero(&r->payload.sound[k],sizeof r->payload.sound[k]))return po_fail(o);
    }
    for(unsigned i=0;i<PC_EVENTS;i++) {
        po_command decoded;
        if(i<r->count){if(!pc_decode(&r->payload.events[i],r->kits,&decoded))return po_fail(o);}
        else if(!po_zero(&r->payload.events[i],sizeof r->payload.events[i]))return po_fail(o);
    }
    unsigned count=r->count,kits=r->kits;
    for(unsigned k=0;k<kits;k++)if(r->payload.mask&(1u<<k))
        po_sound_apply(&o->payload.kit[k],&r->payload.sound[k]);
    o->state=po_state_hash(&o->payload);
    /* Forward expansion in the SAME540B local object. Before every write
     * except the last, all later compact events must remain beyond it.
     * Current316B offset exceeds63*(8-3)=315B; load current event first.
     */
    _Static_assert(offsetof(pc_payload,events)>=(PC_EVENTS-1u)*(sizeof(po_command)-sizeof(pc_event)),"forward expansion must not destroy unread events");
    for(unsigned i=0;i<count;i++) {
        pc_event event;po_command decoded;
        __builtin_memcpy(&event,&local.compact.payload.events[i],sizeof event);
        if(!pc_decode(&event,kits,&decoded))return po_fail(o);
        local.normal.commands[i]=decoded;
    }
    for(unsigned i=count;i<PO_COMMANDS;i++)local.normal.commands[i]=(po_command){0};
    local.normal.guard=PO_GUARD;local.normal.previous_state=o->state;local.normal.count=count;
    local.normal.check=po_request_hash(&local.normal);
    return po_run_snapshot(o,&local.normal,result);
}
/* CPU0-only builder. Error is sticky and invalidates partial wire; callers
 * cannot accidentally seal/publish the first64events after an overflow.
 * Closing a builder prevents further writes to a possibly published packet.
 */
typedef struct {pc_request *wire;const po_routing *route;uint32_t failed,closed;} pc_builder;
static int pc_builder_init(pc_builder *b,pc_request *wire,const po_routing *route,
                            uint32_t ticket,uint32_t state,unsigned frames) {
    if(!b||!wire||!po_routing_valid(route)||!ticket||!frames||frames>128||
       !bb_rt_separate(b,sizeof *b,wire,sizeof *wire)||!bb_rt_separate(b,sizeof *b,route,sizeof *route)||
       !bb_rt_separate(wire,sizeof *wire,route,sizeof *route))return 0;
    __builtin_memset(wire,0,sizeof *wire);wire->guard=PC_GUARD;wire->ticket=ticket;
    wire->kits=route->kits;wire->frames=frames;wire->previous_state=state;
    *b=(pc_builder){wire,route,0,0};return 1;
}
static int pc_builder_error(pc_builder *b) {
    if(b&&!b->closed){b->failed=1;if(b->wire){b->wire->guard=0;b->wire->check=0;}}
    return PO_ROUTE_REFUSED;
}
static int pc_builder_slot(pc_builder *b,unsigned machine,unsigned *slot) {
    if(!b||b->closed||b->failed)return PO_ROUTE_REFUSED;
    if(!b->wire||b->wire->guard!=PC_GUARD||!po_routing_valid(b->route)||b->wire->kits!=b->route->kits)
        return pc_builder_error(b);
    int status=po_route_slot(b->route,machine,slot);
    return status<0?pc_builder_error(b):status;
}
static int pc_builder_event(pc_builder *b,unsigned slot,unsigned type,unsigned index,unsigned param,unsigned value) {
    if(!b||b->closed||b->failed)return PO_ROUTE_REFUSED;
    if(slot>=b->wire->kits||index>15||type>7||param>255||value>255||b->wire->count>=PC_EVENTS)
        return pc_builder_error(b);
    pc_event event={(uint8_t)((type<<5)|(slot<<4)|index),(uint8_t)param,(uint8_t)value};po_command c;
    if(!pc_decode(&event,b->wire->kits,&c))return pc_builder_error(b);
    b->wire->payload.events[b->wire->count++]=event;b->wire->check=0;return PO_ROUTE_OK;
}
static int pc_builder_hit(pc_builder *b,const bb_hit *hit) {
    if(!hit)return pc_builder_error(b);
    unsigned slot=0;int status=pc_builder_slot(b,hit->machine,&slot);
    if(status!=PO_ROUTE_OK)return status;
    return pc_builder_event(b,slot,PO_HIT,hit->voice,0,hit->velocity);
}
static int pc_builder_group(pc_builder *b,unsigned machine,unsigned group,unsigned level,unsigned muted) {
    unsigned slot=0;int status=pc_builder_slot(b,machine,&slot);
    if(status!=PO_ROUTE_OK)return status;
    return pc_builder_event(b,slot,PO_GROUP,group,muted,level);
}
static int pc_builder_set(pc_builder *b,unsigned machine,unsigned track,unsigned parameter,unsigned value) {
    unsigned slot=0;int status=pc_builder_slot(b,machine,&slot);
    if(status!=PO_ROUTE_OK)return status;
    return pc_builder_event(b,slot,PO_SET,track,parameter,value);
}
static int pc_builder_patch(pc_builder *b,unsigned machine,const bb_drum_sound *sound) {
    unsigned slot=0;int status=pc_builder_slot(b,machine,&slot);
    if(status!=PO_ROUTE_OK)return status;
    if(!sound||!po_sound_valid(sound)||b->wire->count||
       (b->wire->payload.mask&(1u<<slot))||!bb_rt_separate(b->wire,sizeof *b->wire,sound,sizeof *sound))
        return pc_builder_error(b);
    bb_c1_drum_copy(&b->wire->payload.sound[slot],sound,sizeof *sound);
    b->wire->payload.mask|=1u<<slot;b->wire->check=0;return PO_ROUTE_OK;
}
static int pc_builder_seal(pc_builder *b) {
    if(!b||b->closed||b->failed||!b->wire||b->wire->guard!=PC_GUARD||!b->wire->payload.mask)return 0;
    b->wire->check=pc_hash(b->wire);b->closed=1;return 1;
}
#endif
