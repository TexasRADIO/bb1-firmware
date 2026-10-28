/* SPDX-License-Identifier: GPL-3.0-only */
/* OFFLINE atomic portable-preset extension. Different guard from normal wire.
 * All patches and following commands validate BEFORE any private mutation.
 * Existing audio_drum_sound mapping is supplied unchanged by the integration.
 * Same540B wire budget;24following commands,overflow refused,not truncated.
 * Not a control-router/HAL implementation or target timing acceptance.
 */
#ifndef BB_PO_PATCH_EXPERIMENT_H
#define BB_PO_PATCH_EXPERIMENT_H
#include "owner.h"
#include "bb_drum_sound.h"
#define PO_PATCH_GUARD UINT32_C(0x50505432)
enum { PO_PATCH_COMMANDS=24 };
typedef struct {
    bb_drum_sound sound[2];po_command after[PO_PATCH_COMMANDS];uint32_t mask;
} po_patch_payload;
typedef struct {
    uint32_t guard,ticket,kits,frames,previous_state,count;
    po_patch_payload payload;uint32_t check;
} po_patch_request;
_Static_assert(sizeof(bb_drum_sound)==158,"current portable schema");
_Static_assert(sizeof(po_patch_payload)==512,"same fixed payload budget");
_Static_assert(sizeof(po_patch_request)==sizeof(po_request),"no wire-size growth");
static uint32_t po_patch_hash(const po_patch_request *r) {
    return bb_c1_drum_hash(PO_PATCH_GUARD,r,offsetof(po_patch_request,check));
}
static int po_sound_valid(const bb_drum_sound *s) {
    if(s->kit_level>127||s->kit_accent>127||s->edited>1)return 0;
    for(unsigned sound=0;sound<BB_DRUM_SOUNDS;sound++)for(unsigned pot=0;pot<BB_DRUM_POTS;pot++)
        if(s->pot[sound][pot]>(pot==D8P_DIST?6u:127u))return 0;
    for(unsigned track=0;track<BB_DRUM_TRACKS;track++)
        if(s->mode[track]>(track>=2&&track<=6?1u:0u))return 0;
    return 1;
}
static int po_zero(const void *data,size_t n) {
    const unsigned char *p=data;while(n--)if(*p++)return 0;return 1;
}
/* Fixed integration function, no function pointer comes from a request. */
static void po_sound_apply(drum808_t *,const bb_drum_sound *);
static int po_patch_run(po_owner *o,const po_patch_request *wire,po_result *result) {
    if(!o)return 0;
    if(!wire||!result||!bb_rt_separate(o,sizeof *o,wire,sizeof *wire)||
       !bb_rt_separate(o,sizeof *o,result,sizeof *result)||!bb_rt_separate(wire,sizeof *wire,result,sizeof *result))return po_fail(o);
    if(o->phase!=PO_READY||!po_shape(o))return po_fail(o);
    union {po_patch_request patch;po_request normal;} local;
    bb_c1_drum_copy(&local.patch,wire,sizeof local.patch);
    po_patch_request *r=&local.patch;
    if(r->guard!=PO_PATCH_GUARD||r->check!=po_patch_hash(r)||!r->ticket||o->ticket==UINT32_MAX||
       r->ticket!=o->ticket+1u||r->kits!=o->payload.kits||!r->frames||r->frames>128||
       r->count>PO_PATCH_COMMANDS||r->previous_state!=o->state||o->state!=po_state_hash(&o->payload)||
       !r->payload.mask||(r->payload.mask&~((1u<<r->kits)-1u)))return po_fail(o);
    for(unsigned k=0;k<r->kits;k++)if(!bb_group_state_valid(&o->payload.groups[k]))return po_fail(o);
    for(unsigned k=0;k<2;k++) {
        if(r->payload.mask&(1u<<k)){if(!po_sound_valid(&r->payload.sound[k]))return po_fail(o);}
        else if(!po_zero(&r->payload.sound[k],sizeof r->payload.sound[k]))return po_fail(o);
    }
    for(unsigned i=0;i<PO_PATCH_COMMANDS;i++) {
        if(i<r->count){if(!po_command_valid(&r->payload.after[i],r->kits))return po_fail(o);}
        else if(!po_zero(&r->payload.after[i],sizeof r->payload.after[i]))return po_fail(o);
    }
    /* At this point all fields for BOTH kits and ALL events are accepted. */
    unsigned count=r->count;
    for(unsigned k=0;k<r->kits;k++)if(r->payload.mask&(1u<<k))
        po_sound_apply(&o->payload.kit[k],&r->payload.sound[k]);
    o->state=po_state_hash(&o->payload);
    /* Nonoverlapping intra-union copy: destination192B max,source starts316B.
     * Do not erase the compact patch/events before saving every event. */
    _Static_assert(offsetof(po_patch_payload,after)>=PO_PATCH_COMMANDS*sizeof(po_command),"event copy regions separate");
    bb_c1_drum_copy(local.normal.commands,r->payload.after,count*sizeof(po_command));
    for(unsigned i=count;i<PO_COMMANDS;i++)local.normal.commands[i]=(po_command){0};
    local.normal.guard=PO_GUARD;local.normal.previous_state=o->state;local.normal.count=count;
    local.normal.check=po_request_hash(&local.normal);
    return po_run(o,&local.normal,result);
}
#endif
