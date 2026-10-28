#include "group_validate.h"
/* SPDX-License-Identifier: GPL-3.0-only */
#include "bb909_audio_route.h"
#include "bb909_controls.h"
static bb909_controls bb_c1_909_controls;
static uint32_t bb_c1_909_control_epoch,bb_c1_909_control_rejected;
static bb909_request bb_c1_909_pending;
/* The CPU0-only borrowed pointer never enters the request or worker. */
static int bb909_group_overlap(const void *a,size_t an,const void *b,size_t bn){
    uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;return x<=y?y-x<an:x-y<bn;
}
static int bb909_group_memory_ok(const void *p,size_t bytes){
    if(!p)return 0;
#define OUTSIDE(object) if(bb909_group_overlap(p,bytes,&object,sizeof object))return 0
    OUTSIDE(bb_c1_private_pool);OUTSIDE(bb_c1_909_owner_request);
    OUTSIDE(bb_c1_909_pending);OUTSIDE(bb_c1_mail);OUTSIDE(bb_c1_live);
    OUTSIDE(bb_c1_909_runtime);OUTSIDE(bb_c1_909_controls);
#undef OUTSIDE
    return 1;
}
int bb909_audio_layer(void) {
    /* A failed bound owner remains that family, never falls back to 808. */
    return bb_c1_909_runtime.bound?(int)bb_c1_909_runtime.layer:-1;
}
/* Main-loop activation only, after a new actor is initialized and before
 * the first job. Never reset a running mirror to unmodified defaults. */
int bb909_audio_control_reset(void) {
    if(!bb_c1_909_runtime.bound||bb_c1_909_runtime.jobs||bb_c1_909_runtime.busy
       ||bb_c1_909_runtime.loan||bb_c1_909_runtime.failed||bb_c1_909_pending.count)return 0;
    if(!bb_c1_909_generation||bb_c1_909_control_epoch==bb_c1_909_generation)return 0;
    bb909_controls_default(&bb_c1_909_controls);bb_c1_909_control_epoch=bb_c1_909_generation;return 1;
}
int bb909_audio_parameter(unsigned layer,unsigned voice,unsigned parameter,unsigned value) {
    if(!bb_c1_909_runtime.bound||bb_c1_909_control_epoch!=bb_c1_909_generation||layer!=bb_c1_909_runtime.layer
       ||!bb909_control_valid(voice,parameter,value))return 0;
    if(bb_c1_909_runtime.busy||bb_c1_909_runtime.loan||bb_c1_909_runtime.failed
       ||bb_c1_909_pending.count>=32u)return bb_c1_909_route_fail(BB_C1_LIVE_PREPARE_BAD);
    unsigned current=bb_c1_909_controls.value[voice][parameter];
    for(unsigned i=0;i<bb_c1_909_pending.count;i++) {
        const bb909_event *e=&bb_c1_909_pending.events[i];
        if(e->kind==BB909_EVENT_SET&&e->voice==voice&&e->parameter==parameter)current=e->value;
    }
    if(current==value)return 1; /* preserve untouched engineering defaults */
    bb_c1_909_pending.events[bb_c1_909_pending.count++]=(bb909_event){0,BB909_EVENT_SET,voice,parameter,value};
    return 1; /* acknowledged by release, not optimistic UI state */
}
uint32_t bb909_audio_epoch(void){return bb_c1_909_control_epoch;}
int bb909_audio_parameter_epoch(unsigned layer,unsigned voice,unsigned parameter,unsigned value,uint32_t epoch) {
    if(!epoch||epoch!=bb_c1_909_control_epoch
       ||!bb909_audio_parameter(layer,voice,parameter,value)) {
        bb_c1_909_control_rejected++;return 0;
    }
    return 1;
}
int bb909_audio_hit(unsigned layer,unsigned voice,unsigned velocity) {
    if(!bb_c1_909_runtime.bound||bb_c1_909_control_epoch!=bb_c1_909_generation||layer!=bb_c1_909_runtime.layer||voice>=11u||velocity>127u
       ||bb_c1_909_runtime.busy||bb_c1_909_runtime.loan||bb_c1_909_runtime.failed
       ||bb_c1_909_pending.count>=32u)return bb_c1_909_route_fail(BB_C1_LIVE_PREPARE_BAD);
    bb_c1_909_pending.events[bb_c1_909_pending.count++]=(bb909_event){0,BB909_EVENT_TRIGGER,voice,0,velocity};
    return 1;
}
int bb909_audio_begin(unsigned frames,gg_state *groups) {
    if(!frames||frames>128||bb_c1_909_runtime.busy||bb_c1_909_runtime.loan
       ||bb_c1_909_runtime.failed||bb_c1_909_group_live
       ||(uintptr_t)groups%_Alignof(gg_state)
       ||!bb909_group_memory_ok(groups,sizeof *groups)||!gg_valid(groups))
        return bb_c1_909_route_fail(BB_C1_LIVE_PREPARE_BAD);
    for(unsigned g=0;g<GG_COUNT;g++){
        bb_c1_909_pending.groups[g].level=groups->fader[g].level;
        bb_c1_909_pending.groups[g].muted=groups->fader[g].muted;
    }
    bb_c1_909_pending.ticket=bb_c1_mail.job+1u;bb_c1_909_pending.frames=frames;
    bb_c1_909_pending.check=bb909_request_check(&bb_c1_909_pending);
    if(!bb_c1_909_begin(&bb_c1_909_pending,bb_c1_909_runtime.layer,groups))return 0;
    bb_c1_909_group_live=groups;return 1;
}
int bb909_audio_finish(unsigned frames,bb909_audio_view *out) {
    if(!out||(uintptr_t)out%_Alignof(bb909_audio_view)||!bb_c1_909_group_live
       ||!bb909_group_memory_ok(out,sizeof *out)
       ||bb909_group_overlap(out,sizeof *out,bb_c1_909_group_live,sizeof(gg_state)))
        return bb_c1_909_route_fail(BB_C1_LIVE_TARGET_BAD);
    const bb909_owner *view=bb_c1_909_finish(bb_c1_909_runtime.layer,frames);
    if(!view)return 0;
    *out=(bb909_audio_view){view->bus[0],view->bus[1],view->bus[2],view->bus[3]};return 1;
}
int bb909_audio_release(gg_state *groups) {
    if(!groups||groups!=bb_c1_909_group_live)
        return bb_c1_909_route_fail(BB_C1_LIVE_TARGET_BAD);
    /* No commands are admitted during a span. Detect changed targets rather
     * than overwriting a newer control with a completed older job. */
    for(unsigned g=0;g<GG_COUNT;g++)
        if(groups->fader[g].level!=bb_c1_909_pending.groups[g].level
           ||groups->fader[g].muted!=bb_c1_909_pending.groups[g].muted)
            return bb_c1_909_route_fail(BB_C1_LIVE_TARGET_BAD);
    if(!bb_c1_909_release(&bb_c1_private_pool.owner))return 0;
    for(unsigned i=0;i<bb_c1_909_pending.count;i++) {
        const bb909_event *e=&bb_c1_909_pending.events[i];
        if(e->kind==BB909_EVENT_SET)
            (void)bb909_controls_applied(&bb_c1_909_controls,e->voice,e->parameter,e->value);
    }
    /* Finish validated this exclusively borrowed payload; release verified
     * the unchanged transport ticket. Only now commit the live fader state. */
    *groups=bb_c1_private_pool.owner.groups;
    bb_c1_909_group_live=0;bb_c1_909_pending.count=0;return 1;
}
