/* SPDX-License-Identifier: GPL-3.0-only */
/* Included by the target HAL. Only CPU0 calls these hooks, from its audio
 * owner. CPU1 sees the fixed private payload only, never a live pointer.
 * Successful completion is acknowledged from the RAM loop after XIP returns.
 * No retry/replay after any uncertain work; reject output until reboot. */
#include "bb_c1_completion.h"
enum { BB_C1_LIVE_BOOT_BAD=1,BB_C1_LIVE_BUSY,BB_C1_LIVE_TICKET_END,
       BB_C1_LIVE_PREPARE_BAD,BB_C1_LIVE_PUBLISH_BAD,BB_C1_LIVE_RESULT_BAD,
       BB_C1_LIVE_TIMEOUT,BB_C1_LIVE_PAYLOAD_BAD,BB_C1_LIVE_TARGET_BAD,
       BB_C1_LIVE_TRANSITION_STOP };
static struct {
    uint32_t ready,failed,busy,jobs,wait_max_ticks,selected,frames,ticket,held;
    uint32_t submitted_ticks,service_ticks,wait_ticks,active_mask;
    uint32_t fail_ticket,fail_done,fail_bad,fail_frames,fail_selected,fail_mask;
} bb_c1_live;
/* Historical failure remains latched after a verified CPU0-only retirement. */
static volatile uint32_t bb_c1_retired;
static int bb_c1_cpu0_available(void) {
    if(!*(volatile uint32_t *)&bb_c1_live.failed)
        return *(volatile uint32_t *)&bb_c1_live.ready!=0;
    return bb_c1_retired && bb_c1_live.held && !bb_c1_live.busy &&
           (BB_C1_CON&10u)==2u;
}
#include "bb_first_fault_context.h"
static bb_first_fault_context bb_first_fault;
static void bb_capture_first_fault(uint32_t reason);
static int bb_c1_live_fail(uint32_t reason) {
    bb_c1_live.failed=reason;bb_c1_live.ready=0;
    bb_c1_halt();bb_c1_live.held=(BB_C1_CON&10u)==2u;
    if(!bb_c1_live.held)for(;;)bb_c1_halt(); /* never return into flash/audio */
    bb_capture_first_fault(reason);
    /* Read completion only AFTER a verified hold. No payload scan or
     * replay of uncertain work. These fields remain latched until restart. */
    bb_c1_live.fail_ticket=bb_c1_live.ticket;bb_c1_live.fail_done=bb_c1_mail.done;
    bb_c1_live.fail_bad=bb_c1_mail.bad;bb_c1_live.fail_frames=bb_c1_live.frames;
    bb_c1_live.fail_selected=bb_c1_live.selected;bb_c1_live.fail_mask=bb_c1_live.active_mask;
    bb_c1_live.busy=0;return 0;
}
#ifndef BB_C1_CPU0_CLI
#define BB_C1_CPU0_CLI() __asm__ volatile("cli")
#endif
static void bb_c1_transition_fence(void) {
    BB_C1_CPU0_CLI();
    (void)bb_c1_live_fail(BB_C1_LIVE_TRANSITION_STOP);
}
/* IEEE binary32 buses: reject NaN and either infinity even if the worker
 * supplied a self-consistent checksum. Check bits, not floating arithmetic,
 * so signaling NaNs are never evaluated. Reuse this scan for bus hashing;
 * do not add a duplicate full-payload checksum or change worker maths. */
_Static_assert(sizeof(float)==sizeof(uint32_t),"drum buses require binary32");
static int bb_c1_live_payload_ok(const bb_c1_drum_job *j,uint32_t result) {
    for(unsigned k=0;k<j->kits;k++)if(!bb_group_state_valid(&j->groups[k]))return 0;
    uint32_t h=bb_c1_drum_state_check(j);
    for(unsigned k=0;k<j->kits;k++)for(unsigned b=0;b<3u;b++)
        for(unsigned i=0;i<j->frames;i++) {
            union {float f;uint32_t u;} bits;
            __builtin_memcpy(&bits.u,&j->bus[k][b][i],sizeof bits.u);
            if((bits.u&UINT32_C(0x7F800000))==UINT32_C(0x7F800000))return 0;
            h=bb_c1_drum_hash_word(h,bits.u);
        }
    return h==result;
}
int bb_parallel_begin(const drum808_t *kit,const gg_state *groups,unsigned count,unsigned frames) {
    /* CPU0 must not copy an 808 snapshot over the continuing 909 actor. */
    if(bb_c1_909_owner_armed){bb_c1_live_fail(BB_C1_LIVE_TARGET_BAD);return -2;}
    if(bb_c1_live.failed)return -2;
    if(!bb_c1_live.ready)return -1; /* startup held: existing serial path */
    if(!kit || !groups || !count || count>4u || !frames || frames>128u || bb_c1_live.busy) {
        bb_c1_live_fail(BB_C1_LIVE_BUSY);return -2;
    }
    BB_C1_SYNC();
    if(bb_c1_mail.bad || bb_c1_mail.done!=bb_c1_mail.job) {
        bb_c1_live_fail(BB_C1_LIVE_RESULT_BAD);return -2;
    }
    int selected=-1;
    for(unsigned k=0;k<count;k++)if(drum808_active(&kit[k])){selected=(int)k;break;}
    if(selected<0)return -1;
    uint32_t ticket=bb_c1_mail.job+1u;
    if(!ticket){bb_c1_live_fail(BB_C1_LIVE_TICKET_END);return -2;}
    if(!bb_c1_drum_prepare(&bb_c1_drum_private,&kit[selected],&groups[selected],1u,frames,ticket)) {
        bb_c1_live_fail(BB_C1_LIVE_PREPARE_BAD);return -2;
    }
    bb_c1_live.selected=(uint32_t)selected;bb_c1_live.frames=frames;
    bb_c1_live.ticket=ticket;bb_c1_live.busy=1;
    bb_c1_live.active_mask=1u<<(unsigned)selected;
    bb_c1_live.submitted_ticks=fm1_ticks();
    if(!bb_c1_publish(&bb_c1_mail,BB_C1_DRUM_JOB,ticket)) {
        bb_c1_live_fail(BB_C1_LIVE_PUBLISH_BAD);return -2;
    }
    return selected;
}
int bb_parallel_finish(unsigned selected,drum808_t *live,gg_state *groups,float *dry,float *rev,float *dly,unsigned frames) {
    if(bb_c1_retained_control.active)return bb_c1_live_fail(BB_C1_LIVE_TARGET_BAD);
    if(!bb_c1_live.ready || !bb_c1_live.busy || !live || !groups || !dry || !rev || !dly
       || selected!=bb_c1_live.selected || frames!=bb_c1_live.frames)
        return bb_c1_live_fail(BB_C1_LIVE_TARGET_BAD);
    uint32_t result=0,t0=fm1_ticks();int status;
    /* CPU0 join bound, not a promise that the complete audio half fits. */
    while((status=bb_c1_complete(&bb_c1_mail,bb_c1_live.ticket,&result))==0
          && fm1_ticks()-t0<24000u)BB_C1_SYNC();
    uint32_t elapsed=fm1_ticks()-t0;
    bb_c1_live.wait_ticks=elapsed;
    bb_c1_live.service_ticks=(t0+elapsed)-bb_c1_live.submitted_ticks;
    if(elapsed>bb_c1_live.wait_max_ticks)bb_c1_live.wait_max_ticks=elapsed;
    if(status!=1)return bb_c1_live_fail(status<0?BB_C1_LIVE_RESULT_BAD:BB_C1_LIVE_TIMEOUT);
    if(!bb_c1_drum_valid(&bb_c1_drum_private,bb_c1_live.ticket)
       || bb_c1_drum_private.kits!=1u || bb_c1_drum_private.frames!=frames
       || !bb_c1_live_payload_ok(&bb_c1_drum_private,result))
        return bb_c1_live_fail(BB_C1_LIVE_PAYLOAD_BAD);
    /* All acceptance checks precede every live engine and sample write. */
    bb_c1_drum_copy(live,&bb_c1_drum_private.kit[0],sizeof *live);
    bb_c1_drum_copy(groups,&bb_c1_drum_private.groups[0],sizeof *groups);
    for(unsigned i=0;i<frames;i++) {
        dry[i]=bb_c1_drum_private.bus[0][0][i];
        rev[i]=bb_c1_drum_private.bus[0][1][i];
        dly[i]=bb_c1_drum_private.bus[0][2][i];
    }
    bb_c1_live.busy=0;bb_c1_live.jobs++;return 1;
}

/* CPU0-only scratch. No audio owner/cache pointer crosses to CPU1.
 * Three other kits maximum; retain original k-order mixing after rendering.
 * Capture activity BEFORE rendering: a tail may become idle in this span,
 * but its final samples still have to be mixed. Ordinary RAM, not pool. */
#define BB_C1_OVERLAP_CACHE 1
static struct {
    float bus[3][3][128];
    uint32_t mask,count,frames,worker,ticket,pending,retained,rendered;
    uint8_t slot[4];
} bb_c1_other;
int bb_parallel_prepare_others(drum808_t *kit,gg_state *groups,unsigned count,unsigned frames,unsigned worker) {
    if(!kit || !groups || count>4u || worker>=count || !frames || frames>128u ||
       !bb_c1_live.ready || !bb_c1_live.busy || bb_c1_live.failed ||
       worker!=bb_c1_live.selected || frames!=bb_c1_live.frames ||
       bb_c1_other.ticket==bb_c1_live.ticket)
        return bb_c1_live_fail(BB_C1_LIVE_TARGET_BAD);
    for(unsigned k=0;k<count;k++)if(!bb_group_state_valid(&groups[k]))
        return bb_c1_live_fail(BB_C1_LIVE_TARGET_BAD);
    bb_c1_other.mask=0;bb_c1_other.count=count;bb_c1_other.frames=frames;
    bb_c1_other.worker=worker;bb_c1_other.ticket=bb_c1_live.ticket;
    bb_c1_other.retained=0;bb_c1_other.rendered=frames;
    for(unsigned k=0;k<count;k++) {
        bb_c1_other.slot[k]=255;
        if(drum808_active(&kit[k]))bb_c1_other.mask|=1u<<k;
    }
    bb_c1_live.active_mask=bb_c1_other.mask;
    bb_c1_other.pending=bb_c1_other.mask&~(1u<<worker);
    unsigned slot=0;
    for(unsigned k=0;k<count;k++)if(k!=worker && (bb_c1_other.mask&(1u<<k))) {
        bb_c1_other.slot[k]=(uint8_t)slot;
        for(unsigned b=0;b<3;b++)for(unsigned i=0;i<frames;i++)bb_c1_other.bus[slot][b][i]=0;
        bb_proposal_group_span(&kit[k],&groups[k],bb_c1_other.bus[slot][0],
            bb_c1_other.bus[slot][1],bb_c1_other.bus[slot][2],(int)frames);
        slot++;
    }
    return 1;
}
int bb_parallel_cached_active(unsigned k) {
    return k<bb_c1_other.count && (bb_c1_other.mask&(1u<<k))!=0;
}
int bb_parallel_cached_other(unsigned k,float *dry,float *rev,float *dly,unsigned frames) {
    if(!bb_c1_live.ready || bb_c1_live.failed || !bb_c1_other.ticket ||
       bb_c1_other.ticket!=(bb_c1_other.retained?bb_c1_retained_control.transaction:bb_c1_live.ticket) ||
       bb_c1_other.rendered!=frames || k>=bb_c1_other.count || k==bb_c1_other.worker ||
       !(bb_c1_other.mask&(1u<<k)) || bb_c1_other.slot[k]>=3u ||
       !(bb_c1_other.pending&(1u<<k)) || frames!=bb_c1_other.frames || !dry || !rev || !dly)
        return bb_c1_live_fail(BB_C1_LIVE_TARGET_BAD);
    unsigned slot=bb_c1_other.slot[k];
    bb_c1_drum_copy(dry,bb_c1_other.bus[slot][0],frames*sizeof(float));
    bb_c1_drum_copy(rev,bb_c1_other.bus[slot][1],frames*sizeof(float));
    bb_c1_drum_copy(dly,bb_c1_other.bus[slot][2],frames*sizeof(float));
    bb_c1_other.pending&=~(1u<<k);return 1;
}
