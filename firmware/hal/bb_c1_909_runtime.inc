#include "group_validate.h"
/* SPDX-License-Identifier: GPL-3.0-only */
/* Fixed HAL storage and mailbox only. First binding is PRE-AUDIO after
 * qualification. A future interactive rebind requires real audio suspension,
 * idle proof and its own lifecycle checks; this API does not fake that proof.
 * CPU0 may use returned buses only until release, before publishing any job. */
int bb909_audio_control_reset(void);
static uint32_t bb_c1_909_generation;
/* CPU0-only borrow. Cleared on failure before a suspended caller returns. */
static gg_state *bb_c1_909_group_live;
static struct {
    uint32_t bound,layer,busy,loan,ticket,frames,failed,jobs;
    uint32_t submitted_ticks,service_ticks,wait_ticks,result;
} bb_c1_909_runtime;
static int bb_c1_909_route_fail(uint32_t reason) {
    bb_c1_909_group_live=0;
    bb_c1_909_runtime.failed=reason;bb_c1_909_runtime.loan=0;
    return bb_c1_live_fail(reason); /* verified hold, no retry or serial replay */
}
static int bb_c1_909_bind_boot(unsigned layer) {
    BB_C1_SYNC();
    if(layer>=4u||bb_c1_phase!=BB_C1_PHASE_RELEASED||!bb_c1_live.ready
       ||bb_c1_live.failed||bb_c1_live.busy||bb_c1_909_runtime.bound
       ||bb_c1_909_owner_armed||bb_c1_mail.bad||bb_c1_mail.job!=bb_c1_mail.done
       ||bb_c1_909_generation==UINT32_MAX||!bb_c1_released(BB_C1_CON))return 0;
    /* CPU1 is idle, CPU0 is the only publisher, and audio has not started.
     * No core re-start or voltage/clock change is used here. */
    if(!bb909_owner_init(&bb_c1_private_pool.owner,bb_c1_mail.job,
                        (const void *)(uintptr_t)BB909_PACK_XIP_ADDR,BB909_PACK_BYTES))return 0;
    bb_c1_909_runtime.layer=layer;bb_c1_909_runtime.bound=1;
    bb_c1_909_owner_armed=1;bb_c1_909_generation++;
    if(!bb909_audio_control_reset())return bb_c1_909_route_fail(BB_C1_LIVE_PREPARE_BAD);
    BB_C1_SYNC();return 1;
}
static int bb_c1_909_begin(const bb909_request *request,unsigned layer,const gg_state *groups) {
    BB_C1_SYNC();
    if(!bb_c1_909_runtime.bound||bb_c1_909_runtime.failed||!bb_c1_909_owner_armed
       ||bb_c1_phase!=BB_C1_PHASE_RUNTIME||!bb_c1_live.ready||bb_c1_live.failed
       ||bb_c1_live.busy||bb_c1_909_runtime.busy||bb_c1_909_runtime.loan||layer!=bb_c1_909_runtime.layer
       ||!request||(uintptr_t)request%_Alignof(bb909_request)
       ||((uintptr_t)request<=(uintptr_t)&bb_c1_909_owner_request?
           (uintptr_t)&bb_c1_909_owner_request-(uintptr_t)request<sizeof *request:
           (uintptr_t)request-(uintptr_t)&bb_c1_909_owner_request<sizeof bb_c1_909_owner_request)
       ||((uintptr_t)request<=(uintptr_t)&bb_c1_private_pool.owner?
           (uintptr_t)&bb_c1_private_pool.owner-(uintptr_t)request<sizeof *request:
           (uintptr_t)request-(uintptr_t)&bb_c1_private_pool.owner<sizeof bb_c1_private_pool.owner)
       ||!bb_c1_released(BB_C1_CON))
        return bb_c1_909_route_fail(BB_C1_LIVE_TARGET_BAD);
    uint32_t ticket=bb_c1_mail.job+1u;
    if(!ticket)return bb_c1_909_route_fail(BB_C1_LIVE_TICKET_END);
    if(bb_c1_mail.bad||bb_c1_mail.job!=bb_c1_mail.done||request->ticket!=ticket
       ||!request->frames||request->frames>128||request->count>32
       ||request->check!=bb909_request_check(request))return bb_c1_909_route_fail(BB_C1_LIVE_PREPARE_BAD);
    /* Stable, audio-owned source; producer never modifies the shared fixed
     * request while CPU1 might consume it. Event preflight also runs there. */
    /* Before the first publication only, CPU1 is idle and this is the sole
     * publisher. Preserve an already muted or partially ramped mix rather
     * than starting a newly initialized actor at unity. Never reseed a live
     * actor; subsequent state comes exclusively from its own rendering. */
    if(!bb_c1_909_runtime.jobs){
        if(!groups||(uintptr_t)groups%_Alignof(gg_state)||!gg_valid(groups)
           ||bb_c1_private_pool.owner.frames
           ||bb_c1_private_pool.owner.last_ticket!=bb_c1_mail.done)
            return bb_c1_909_route_fail(BB_C1_LIVE_PREPARE_BAD);
        bb_c1_private_pool.owner.groups=*groups;
    }
    bb_c1_909_owner_request=*request;
    bb_c1_909_runtime.ticket=ticket;bb_c1_909_runtime.frames=request->frames;
    bb_c1_909_runtime.busy=1;bb_c1_live.busy=1;
    bb_c1_live.ticket=ticket;bb_c1_live.frames=request->frames;bb_c1_live.selected=layer;
    bb_c1_live.active_mask=1u<<layer;bb_c1_909_runtime.submitted_ticks=fm1_ticks();
    if(!bb_c1_publish(&bb_c1_mail,BB_C1_909_OWNER_JOB,ticket))return bb_c1_909_route_fail(BB_C1_LIVE_PUBLISH_BAD);
    return 1;
}
static const bb909_owner *bb_c1_909_finish(unsigned layer,unsigned frames) {
    if(!bb_c1_909_runtime.busy||bb_c1_909_runtime.failed||!bb_c1_live.ready
       ||!bb_c1_live.busy||bb_c1_live.failed||bb_c1_909_runtime.loan
       ||layer!=bb_c1_909_runtime.layer||frames!=bb_c1_909_runtime.frames) {
        bb_c1_909_route_fail(BB_C1_LIVE_TARGET_BAD);return 0;
    }
    uint32_t value=0,t0=fm1_ticks();int status;
    /* Preserve the established one-ms join guard. Whole-block timing,
     * including synth/FX/UI work, remains a separate acceptance gate. */
    while((status=bb_c1_complete(&bb_c1_mail,bb_c1_909_runtime.ticket,&value))==0
          && fm1_ticks()-t0<24000u)BB_C1_SYNC();
    bb_c1_909_runtime.wait_ticks=fm1_ticks()-t0;
    bb_c1_909_runtime.service_ticks=fm1_ticks()-bb_c1_909_runtime.submitted_ticks;
    if(status!=1) {bb_c1_909_route_fail(status<0?BB_C1_LIVE_RESULT_BAD:BB_C1_LIVE_TIMEOUT);return 0;}
    if(bb_c1_private_pool.owner.frames!=frames
       ||!bb909_owner_accept(&bb_c1_private_pool.owner,bb_c1_909_runtime.ticket,value)) {
        bb_c1_909_route_fail(BB_C1_LIVE_PAYLOAD_BAD);return 0;
    }
    /* Scan all four buses before exposing any sample. No floating-point
     * evaluation of NaNs; reject even a self-consistent corrupt payload. */
    for(unsigned b=0;b<4u;b++)for(unsigned i=0;i<frames;i++) {
        union {uint32_t u;float f;} bits;
        __builtin_memcpy(&bits.u,&bb_c1_private_pool.owner.bus[b][i],sizeof bits.u);
        if((bits.u&UINT32_C(0x7f800000))==UINT32_C(0x7f800000)) {
            bb_c1_909_route_fail(BB_C1_LIVE_PAYLOAD_BAD);return 0;
        }
    }
    bb_c1_909_runtime.result=value;bb_c1_909_runtime.loan=1;
    bb_c1_909_runtime.busy=0;bb_c1_live.busy=0;return &bb_c1_private_pool.owner;
}
static int bb_c1_909_release(const bb909_owner *view) {
    if(view!=&bb_c1_private_pool.owner||!bb_c1_909_runtime.loan||bb_c1_909_runtime.busy
       ||bb_c1_909_runtime.failed||!bb_c1_live.ready||bb_c1_live.failed
       ||!bb_c1_909_owner_armed||bb_c1_phase!=BB_C1_PHASE_RUNTIME
       ||bb_c1_mail.bad||bb_c1_mail.job!=bb_c1_mail.done
       ||bb_c1_mail.done!=bb_c1_909_runtime.ticket)return bb_c1_909_route_fail(BB_C1_LIVE_TARGET_BAD);
    bb_c1_909_runtime.loan=0;bb_c1_909_runtime.jobs++;return 1;
}
