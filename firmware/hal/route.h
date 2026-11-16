/* SPDX-License-Identifier: GPL-3.0-only */
/* OFFLINE CPU0 request builders. Actor-slot routing is fixed for a lease's
 * lifetime; an unowned machine explicitly falls through to the other owner.
 * No publication here. Seal exactly once after completing a span batch.
 */
#ifndef BB_PO_ROUTE_EXPERIMENT_H
#define BB_PO_ROUTE_EXPERIMENT_H
#include "patch.h"
#include "bb_core.h"
enum { PO_ROUTE_UNOWNED=0,PO_ROUTE_OK=1,PO_ROUTE_REFUSED=-1,PO_NO_SLOT=255 };
typedef struct {uint32_t guard;uint8_t kits,slot[4],reserved[3];uint32_t check;} po_routing;
_Static_assert(sizeof(po_routing)==16,"bounded routing metadata");
static uint32_t po_routing_hash(const po_routing *r) {
    return bb_c1_drum_hash(PO_GUARD^0x524F5554u,r,offsetof(po_routing,check));
}
static int po_routing_valid(const po_routing *r) {
    if(!r||r->guard!=PO_GUARD||!r->kits||r->kits>2||r->check!=po_routing_hash(r)||
       r->reserved[0]||r->reserved[1]||r->reserved[2])return 0;
    unsigned seen=0;
    for(unsigned k=0;k<4;k++)if(r->slot[k]!=PO_NO_SLOT) {
        if(r->slot[k]>=r->kits||(seen&(1u<<r->slot[k])))return 0;
        seen|=1u<<r->slot[k];
    }
    return seen==((1u<<r->kits)-1u);
}
static int po_routing_init(po_routing *out,unsigned kits,const uint8_t slot[4]) {
    if(!out||!slot||!kits||kits>2||!bb_rt_separate(out,sizeof *out,slot,4))return 0;
    po_routing r={0};r.guard=PO_GUARD;r.kits=(uint8_t)kits;
    for(unsigned k=0;k<4;k++)r.slot[k]=slot[k];r.check=po_routing_hash(&r);
    if(!po_routing_valid(&r))return 0;*out=r;return 1;
}
static int po_route_slot(const po_routing *routing,unsigned machine,unsigned *slot) {
    if(!po_routing_valid(routing)||machine>=4||!slot)return PO_ROUTE_REFUSED;
    if(routing->slot[machine]==PO_NO_SLOT)return PO_ROUTE_UNOWNED;
    *slot=routing->slot[machine];return PO_ROUTE_OK;
}
static int po_route_hit_value(const po_routing *routing,const bb_hit *hit,po_command *out) {
    if(!hit||!out||hit->voice>=D8_NUM||hit->velocity>127)return PO_ROUTE_REFUSED;
    unsigned slot=0;int status=po_route_slot(routing,hit->machine,&slot);
    if(status!=PO_ROUTE_OK)return status;
    /* Exact current audio_hit law, NOT velocity/127. kit/frame/step metadata
     * must not silently select another engine or re-time a scheduled event. */
    float velocity=hit->velocity==127?1.0f:hit->velocity*(D8_VEL_NORMAL/88.0f);
    uint32_t bits;__builtin_memcpy(&bits,&velocity,sizeof bits);
    *out=(po_command){PO_HIT,(uint8_t)slot,hit->voice,0,bits};return PO_ROUTE_OK;
}
static int po_route_group_value(const po_routing *routing,unsigned machine,unsigned group,
                                 unsigned level,unsigned muted,po_command *out) {
    if(!out||group>=GG_COUNT||level>127||muted>1)return PO_ROUTE_REFUSED;
    unsigned slot=0;int status=po_route_slot(routing,machine,&slot);
    if(status!=PO_ROUTE_OK)return status;
    *out=(po_command){PO_GROUP,(uint8_t)slot,(uint8_t)group,0,level|(muted<<8)};
    return PO_ROUTE_OK;
}
static int po_route_append(po_request *r,const po_routing *routing,const po_command *c) {
    if(!r||!c||!po_routing_valid(routing)||r->guard!=PO_GUARD||r->kits!=routing->kits||
       r->count>=PO_COMMANDS||!po_command_valid(c,r->kits))return PO_ROUTE_REFUSED;
    r->commands[r->count++]=*c;r->check=0;return PO_ROUTE_OK;
}
static int po_route_patch_append(po_patch_request *r,const po_routing *routing,const po_command *c) {
    if(!r||!c||!po_routing_valid(routing)||r->guard!=PO_PATCH_GUARD||r->kits!=routing->kits||
       r->count>=PO_PATCH_COMMANDS||!po_command_valid(c,r->kits))return PO_ROUTE_REFUSED;
    r->payload.after[r->count++]=*c;r->check=0;return PO_ROUTE_OK;
}
static int po_route_patch_sound(po_patch_request *r,const po_routing *routing,unsigned machine,const bb_drum_sound *sound) {
    if(!r||!sound||!po_sound_valid(sound)||!po_routing_valid(routing)||
       r->guard!=PO_PATCH_GUARD||r->kits!=routing->kits||r->count||
       !bb_rt_separate(r,sizeof *r,sound,sizeof *sound))return PO_ROUTE_REFUSED;
    unsigned slot=0;int status=po_route_slot(routing,machine,&slot);
    if(status!=PO_ROUTE_OK)return status;
    /* No late prepending or coalescing of repeated patches: either can alter
     * active-state/control order. Caller must preserve or defer whole events. */
    if(r->payload.mask&(1u<<slot))return PO_ROUTE_REFUSED;
    bb_c1_drum_copy(&r->payload.sound[slot],sound,sizeof *sound);
    r->payload.mask|=1u<<slot;r->check=0;return PO_ROUTE_OK;
}
#endif
