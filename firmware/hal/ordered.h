/* SPDX-License-Identifier: GPL-3.0-only */
/* Ordered portable-control protocol. Baseline sounds are PENDING, not eagerly
 * applied. Recall/edit changes stay pending until a hit, explicit sync or end
 * of span, matching the retained audio owner's sound revision synchronization.
 * Same540B wire; distinct guard. No UI/core pointers or extra persistent RAM.
 * Not yet actual FIFO/HAL integration or target timing/stack qualification.
 */
#ifndef BB_PO_ORDERED_EXPERIMENT_H
#define BB_PO_ORDERED_EXPERIMENT_H
#include "compact.h"
#define PA_GUARD UINT32_C(0x50414F35)
#define PA_CONTROL_GUARD UINT32_C(0x50414331)
enum { PA_EDIT=4,PA_RECALL=5,PA_SYNC=6,PA_GROUP_EDIT=7 };
typedef pc_request pa_request;
static uint32_t pa_hash(const pa_request *r) {
    return bb_c1_drum_hash(PA_GUARD,r,offsetof(pa_request,check));
}
static uint32_t pa_control_hash(const pa_request *r) {
    return bb_c1_drum_hash(PA_CONTROL_GUARD,r,offsetof(pa_request,check));
}
/* Validate semantic edits against a private portable shadow. No DSP writes. */
static int pa_event(pc_request *r,const pc_event *e) {
    unsigned type=e->tag>>5,k=(e->tag>>4)&1u,index=e->tag&15u;
    if(k>=r->kits)return 0;
    if(type==PO_HIT||type==PO_GROUP){po_command c;return pc_decode(e,r->kits,&c);}
    if(type==PA_EDIT)return bb_drum_parameter_set(&r->payload.sound[k],index,e->param,e->value);
    if(type==PA_RECALL)return !index&&!e->param&&bb_drum_sound_recall(&r->payload.sound[k],e->value);
    if(type==PA_GROUP_EDIT)return index<GG_COUNT&&e->param<=1&&e->value<=(e->param?1u:127u);
    return type==PA_SYNC&&!index&&!e->param&&!e->value;
}
static void pa_sync(po_owner *o,pa_request *r,unsigned k,unsigned *dirty) {
    if(*dirty&(1u<<k)) {
        po_sound_apply(&o->payload.kit[k],&r->payload.sound[k]);*dirty&=~(1u<<k);
    }
}
static __attribute__((noinline)) int pa_execute(po_owner *o,const pa_request *wire,po_result *result,unsigned control_only) {
    if(!o)return 0;
    if(!wire||!result||!bb_rt_separate(o,sizeof *o,wire,sizeof *wire)||
       !bb_rt_separate(o,sizeof *o,result,sizeof *result)||
       !bb_rt_separate(wire,sizeof *wire,result,sizeof *result))return po_fail(o);
    if(o->phase!=PO_READY||!po_shape(o))return po_fail(o);
    pa_request r;bb_c1_drum_copy(&r,wire,sizeof r);
    if(r.guard!=(control_only?PA_CONTROL_GUARD:PA_GUARD)||
       r.check!=(control_only?pa_control_hash(&r):pa_hash(&r))||!r.ticket||o->ticket==UINT32_MAX||
       r.ticket!=o->ticket+1u||r.kits!=o->payload.kits||
       (control_only?r.frames!=0:(!r.frames||r.frames>128))||
       r.count>PC_EVENTS||r.previous_state!=o->state||o->state!=po_state_hash(&o->payload)||
       r.payload.mask!=((1u<<r.kits)-1u))return po_fail(o);
    for(unsigned k=0;k<2;k++) {
        if(k<r.kits) {
            if(!po_sound_valid(&r.payload.sound[k])||!bb_group_state_valid(&o->payload.groups[k]))return po_fail(o);
        } else if(!po_zero(&r.payload.sound[k],sizeof r.payload.sound[k]))return po_fail(o);
    }
    for(unsigned i=0;i<PC_EVENTS;i++) {
        if(i<r.count){if(!pa_event(&r,&r.payload.events[i]))return po_fail(o);}
        else if(!po_zero(&r.payload.events[i],sizeof r.payload.events[i]))return po_fail(o);
    }
    /* Restore original portable baseline after validation-only simulation.
     * Reuse this same local540B object; no second complete request on stack.
     * Caller must already hold the immutable request lease throughout.
     */
    /* Validation changes only portable sounds. Header and event bytes in the
     * local snapshot remain intact; do not recopy those224 unchanged bytes.
     * The immutable publication lease still covers the source sound bytes. */
    bb_c1_drum_copy(r.payload.sound,wire->payload.sound,sizeof r.payload.sound);
    unsigned dirty=r.payload.mask;
    for(unsigned i=0;i<r.count;i++) {
        const pc_event *e=&r.payload.events[i];
        unsigned type=e->tag>>5,k=(e->tag>>4)&1u;
        if(type==PA_EDIT||type==PA_RECALL) {
            (void)pa_event(&r,e);dirty|=1u<<k;
        } else if(type==PA_SYNC)pa_sync(o,&r,k,&dirty);
        else if(type==PA_GROUP_EDIT) {
            unsigned group=e->tag&15u;gg_fader *g=&o->payload.groups[k].fader[group];
            (void)gg_set(&o->payload.groups[k],group,e->param?g->level:e->value,e->param?e->value:g->muted);
        }
        else {
            if(type==PO_HIT)pa_sync(o,&r,k,&dirty);
            po_command c;(void)pc_decode(e,r.kits,&c);po_apply(o->payload.kit,o->payload.groups,&c);
        }
    }
    /* Control flush must not implicitly synchronize pending edits on other
     * kits. CPU0 retains portable state and supplies it in the next baseline.
     * Only explicit syncs and hits apply those sounds before audio advances. */
    if(!control_only)for(unsigned k=0;k<r.kits;k++)pa_sync(o,&r,k,&dirty);
    return po_render(o,r.ticket,r.frames,result);
}
static int pa_run(po_owner *o,const pa_request *wire,po_result *result) {
    return pa_execute(o,wire,result,0);
}
static int pa_control_run(po_owner *o,const pa_request *wire,po_result *result) {
    return pa_execute(o,wire,result,1);
}
/* Separate zero-frame acceptance: no bus output, no elapsed audio time.
 * Same external completion/immutable lease requirement as po_accept. */
static int pa_control_accept(po_owner *o,const po_result *reply,uint32_t ticket,
                             unsigned kits,uint32_t *expected_state) {
    if(!o||!reply||!expected_state)return 0;
    if(!bb_rt_separate(o,sizeof *o,reply,sizeof *reply)||
       !bb_rt_separate(o,sizeof *o,expected_state,sizeof *expected_state)||
       !bb_rt_separate(reply,sizeof *reply,expected_state,sizeof *expected_state))return po_fail(o);
    if(o->phase!=PO_DONE||!po_shape(o)||!ticket||!kits||kits>2||
       reply->check!=po_result_hash(reply)||reply->ticket!=ticket||reply->kits!=kits||reply->frames||
       o->ticket!=ticket||o->payload.ticket!=ticket||o->payload.kits!=kits||o->payload.frames||
       o->state!=reply->state||reply->buses!=po_bus_hash(&o->payload,reply->state))return po_fail(o);
    *expected_state=reply->state;o->phase=PO_READY;return 1;
}
#endif
