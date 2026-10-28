/* SPDX-License-Identifier: GPL-3.0-only */
/* Big Beat guarded CPU1 bootstrap and private live worker. Startup register sequence and stack guards
 * adapted from retained fm1-x0x 47f8e4c4, after Melodee/JieLi AC79 startup.
 * Fixed checked code entry and private payloads; no CPU1 live-engine access.
 * No rail or PLL/divider changes. Startup failure holds CPU1 before audio. */
#pragma once
#include "bb_c1_probe_protocol.h"
/* Diagnostic-only stage trace, never a completion acknowledgement.
 * Startup CPU0 qualification may also write it while the worker is idle;
 * interpret runtime failure snapshots only after live_failed and held. */
#if defined(BB_C1_DRUM_PHASE_TRACE) && BB_C1_DRUM_PHASE_TRACE
extern volatile uint32_t bb_c1_trace;
static void bb_private_phase_stamp(uint32_t stage);
#define BB_C1_DRUM_PHASE(stage) do { \
    bb_c1_trace=UINT32_C(0xC1100000)|(uint32_t)(stage); \
    BB_C1_SYNC(); \
    bb_private_phase_stamp((uint32_t)(stage)); \
} while(0)
#endif
#include "bb_c1_drum_job.h"
#undef BB_C1_DRUM_PHASE
#include "bb_trio.h"
#include "bb909_owner.h"
#include "pack_constants.h"
#define BB_C1_CON (*(volatile uint32_t *)0x1EEE004u)
#define BB_C1_CLOCK_GATE (*(volatile uint32_t *)0x00010008u)
#define BB_C1_MARK UINT32_C(0x43314D4B)
extern void bb_c1_entry(void);
extern uint32_t _bb_c1_ustack[],_bb_c1_ustack_top[],_bb_c1_sstack_top[];
/* Mailbox resides in aligned persistent lease below. */
/* Phase observer follows relocated mailbox below. */
volatile uint32_t bb_c1_trace;
enum { BB_C1_NOT_RUN,BB_C1_PASS,BB_C1_START_TIMEOUT,BB_C1_JOB_TIMEOUT,
       BB_C1_MAILBOX_BAD,BB_C1_RESULT_BAD,BB_C1_HOLD_BAD };
static struct {
    uint32_t status,alive,verified_jobs,start_ticks,max_job_ticks,trace,stack_bytes,held;
} bb_c1_report;

#define BB_C1_FLOAT_JOB UINT32_C(0xD5100001)
#define BB_C1_DRUM_JOB UINT32_C(0xD5100002)
#include "lease-view.h"
#include "rich-job-transaction-prototype.h"
/* Separate candidate, not yet selected by audio_span. CPU0 staging never
 * overlaps worker writes or persistent completion mailbox cache lines. */
typedef struct {bb_rjt_job job;bb_rsp_plan staging;} bb_c1_rich_pool;
_Static_assert(sizeof(bb_c1_rich_pool)<=sizeof(bb_c1_drum_job),"rich scratch exceeds retained allocation");
_Static_assert(sizeof(bb_c1_rich_pool)<=offsetof(po_lease,mail),"rich scratch overlaps mailbox");
typedef union {bb_c1_drum_job eight;bb909_owner owner;po_lease persistent;bb_c1_rich_pool rich;} bb_c1_private_pool_t;
_Static_assert(sizeof(bb_c1_private_pool_t)==sizeof(po_lease),"single worker allocation");
_Static_assert(sizeof(bb909_owner)<=offsetof(po_lease,mail),"boot scratch cannot overwrite mailbox");
static bb_c1_private_pool_t bb_c1_private_pool __attribute__((aligned(64)));
#define bb_c1_drum_private bb_c1_private_pool.eight
#define bb_c1_mail bb_c1_private_pool.persistent.mail
#include "private_phase_cost.h"
#include "retained_chain.h"
/* New protocol: one mailbox ticket, one/two logical fragment tokens. */
#define BB_C1_RETAINED_JOB UINT32_C(0xD5100007)
#define BB_C1_RICH_JOB UINT32_C(0xD5100008)
/* Immutable request copy for worker; CPU0 lease/commit state stays separate.
 * No caller publishes this new job until owner integration is qualified. */
static bb_rjt_lease bb_c1_rich_request;
static bb_rt_state bb_c1_retained_state;
static bb_rt_chain bb_c1_retained_request;
/* CPU0-only lease metadata. Midpoint only advances CPU0's slice ledger;
 * it is NOT a worker completion or permission to inspect private objects. */
static struct {uint32_t active,transaction,offset,last_fragment;} bb_c1_retained_control;
/* The reference is diagnostic scratch, not general low-RAM state. Keep it
 * in the explicitly budgeted audio pool and preserve normal RAM headroom. */
#define BB_C1_TRIO_PHASE 1
#define BB_C1_RELEASE_CHECK 1
/* Released means the SDK hold bit is clear, not that start-bit3 persists.
 * Full checked boot parity/alive/tickets remains mandatory before arming;
 * subsequent jobs still require bounded verified RAM completion. Bit24 is
 * deliberately not assigned a meaning or used as an execution guarantee. */
static inline int bb_c1_released(uint32_t control) {return (control&2u)==0u;}
enum {BB_C1_PHASE_BOOT,BB_C1_PHASE_RUNNING,BB_C1_PHASE_RELEASED,BB_C1_PHASE_RUNTIME};
static uint32_t bb_c1_phase,bb_c1_trio_bound;
/* CPU0's reference exists only during pre-audio qualification. Runtime
 * staging may use these bytes only after that lifetime has ended. */
#include "group_phase_layout.h"
typedef bb_group_phase_pool bb_c1_phase_pool_t;
static bb_c1_phase_pool_t bb_c1_phase_pool __attribute__((aligned(64),section(".pool")));
#define bb_c1_drum_reference bb_c1_phase_pool.reference
static struct {
    uint32_t status,float_jobs,drum_jobs,float_max_ticks,drum_max_ticks,cpu0_max_ticks,exact_cases;
} bb_c1_dsp_report;


#define BB_C1_909_OWNER_JOB UINT32_C(0xD5100005)
static volatile uint32_t bb_c1_909_owner_armed,bb_c1_909_owner_worker_ticks;
static bb909_request bb_c1_909_owner_request __attribute__((aligned(64)));

/* Only this fixed XIP entry can be called from the RAM worker. It never
 * publishes completion. The RAM caller acknowledges AFTER this returns. */
static int __attribute__((noinline,section(".text.bb_c1_fixed_work")))
bb_c1_fixed_work(uint32_t seed,uint32_t nonce,uint32_t job,uint32_t *result) {
    po_lease *l=&bb_c1_private_pool.persistent;
    /* Once adopted, no legacy payload/909 job may overwrite the live owner. */
    if(l->phase) {
        if(l->phase!=PL_INFLIGHT||bb_c1_phase!=BB_C1_PHASE_RUNTIME||
           bb_c1_909_owner_armed||nonce!=job||l->wire.ticket!=job||
           seed!=l->wire.guard||(seed!=PA_GUARD&&seed!=PA_CONTROL_GUARD)||
           !plc_run(&l->owner,&l->wire,&l->reply))return 0;
        *result=l->reply.check;return 1;
    }
    if((bb_c1_retained_state.phase==BB_RT_PREPARED || bb_c1_retained_state.phase==BB_RT_ACTIVE ||
        bb_c1_retained_state.phase==BB_RT_DONE) && seed!=BB_C1_RETAINED_JOB)return 0;
    if(seed==BB_C1_FLOAT_JOB)*result=bb_c1_float_calculate(nonce);
    else if(seed==BB_C1_RICH_JOB) {
        if(bb_c1_909_owner_armed || bb_c1_phase!=BB_C1_PHASE_RUNTIME || nonce!=job ||
           bb_c1_rich_request.ticket!=job || bb_c1_rich_request.committed)return 0;
        #if BB_AUDIO_PROFILE
        uint32_t started=fm1_ticks();
        int ok=bb_rjt_run(&bb_c1_private_pool.rich.job,bb_c1_rich_request,result);
        uint32_t elapsed=fm1_ticks()-started;
        bb_c1_mail.timed_job=job;bb_c1_mail.timed_ticks=elapsed;
        bb_c1_mail.timed_check=bb_c1_result_check(job,elapsed);
        /* RAM caller publishes done only AFTER this returns and csync. */
        return ok;
#else
        return bb_rjt_run(&bb_c1_private_pool.rich.job,bb_c1_rich_request,result);
#endif
    }
    else if(seed==BB_C1_RETAINED_JOB) {
        if(bb_c1_909_owner_armed || bb_c1_phase!=BB_C1_PHASE_RUNTIME || nonce!=job)return 0;
        return bb_rt_chain_run(&bb_c1_retained_state,&bb_c1_drum_private,&bb_c1_retained_request,job,result);
    }
    else if(seed==BB_C1_DRUM_JOB) {
        if(bb_c1_909_owner_armed || nonce!=job || !bb_c1_drum_run(&bb_c1_drum_private,job,result))return 0;
    }
#if BB_WITH_909_ROUTE
    else if(seed==BB_C1_909_OWNER_JOB) {
        if(nonce!=job || (bb_c1_phase!=BB_C1_PHASE_RUNNING && bb_c1_phase!=BB_C1_PHASE_RUNTIME) || !bb_c1_909_owner_armed)return 0;
        uint32_t t0=fm1_ticks();
        int ok=bb909_owner_run(&bb_c1_private_pool.owner,&bb_c1_909_owner_request,job,result);
        bb_c1_909_owner_worker_ticks=fm1_ticks()-t0;return ok;
    }
#else
    /* Disabled engines must not retain their DSP through worker dispatch. */
    else if(seed==BB_C1_909_OWNER_JOB)return 0;
#endif
    else *result=bb_c1_calculate(seed,nonce);
    return 1;
}

/* Fixed dispatcher; no caller-provided pointers or executable addresses. */
static inline __attribute__((always_inline)) int bb_c1_dsp_worker_step(uint32_t *last_job) {
    uint32_t job=bb_c1_mail.job;BB_C1_SYNC();
    if(job==*last_job)return 0;
    uint32_t seed=bb_c1_mail.seed,nonce=bb_c1_mail.nonce,check=bb_c1_mail.check,result;
    if(job!=*last_job+1u || check!=bb_c1_request_check(job,seed,nonce))goto bad;
    /* RAM/XIP is outside the direct-call range. This local volatile
     * function value has one fixed initializer and no mailbox-derived target. */
    int (*volatile invoke)(uint32_t,uint32_t,uint32_t,uint32_t *)=bb_c1_fixed_work;
    int (*target)(uint32_t,uint32_t,uint32_t,uint32_t *)=invoke;
    if(target!=bb_c1_fixed_work)goto bad;
    /* Keep the checked value in a register, without re-reading an unchecked
     * stack pointer or folding it back into an out-of-range direct call. */
    __asm__ volatile("" : "+r"(target));
    /* Diagnostic phase only. Result and completion guards are unchanged.
     * These two extra synchronization points need target timing comparison. */
    bb_c1_trace=0xC1000010u;BB_C1_SYNC(); /* entering the fixed work call */
    int work_ok=target(seed,nonce,job,&result);
    bb_c1_trace=0xC1000011u;BB_C1_SYNC(); /* work returned, not yet published */
    if(!work_ok)goto bad;
    bb_c1_mail.result=result;bb_c1_mail.result_check=bb_c1_result_check(job,result);
    *last_job=job;bb_c1_trace=0xC1000012u;BB_C1_SYNC();bb_c1_mail.done=job;BB_C1_SYNC();return 1;
bad:
    bb_c1_mail.bad_job=job;BB_C1_SYNC();bb_c1_mail.bad=1;BB_C1_SYNC();return -1;
}
static int bb_c1_dsp_exchange(uint32_t seed,uint32_t nonce,uint32_t expected,uint32_t limit,uint32_t *elapsed) {
    uint32_t job=bb_c1_mail.job+1u,t0;int result;
    if(!bb_c1_publish(&bb_c1_mail,seed,nonce))return BB_C1_MAILBOX_BAD;
    t0=fm1_ticks();
    while((result=bb_c1_verify(&bb_c1_mail,job,expected))==0 && fm1_ticks()-t0<limit)BB_C1_SYNC();
    *elapsed=fm1_ticks()-t0;
    return result==1?BB_C1_PASS:result<0?BB_C1_RESULT_BAD:BB_C1_JOB_TIMEOUT;
}
static int bb_c1_dsp_bytes_equal(const void *a,const void *b,unsigned n) {
    const unsigned char *x=a,*y=b;
    while(n--)if(*x++!=*y++)return 0;
    return 1;
}
/* CPU0-only PRE-AUDIO cost records. These never run from audio or CPU1.
 * Clock reads add overhead; report phases separately, do not subtract an
 * assumed calibration or claim these are CPU1 renderer timings. */
#define BB_C1_BOOT_COST 1
typedef struct bb_c1_cost_record {
    uint32_t valid,kits,frames,prepare_ticks,clone_ticks,input_ticks;
    uint32_t render_ticks,output_ticks,run_ticks,exchange_ticks;
} bb_c1_cost_record;
static bb_c1_cost_record bb_c1_boot_cost[8];
static int bb_c1_dsp_boot_jobs(void) {
    if(bb_c1_phase!=BB_C1_PHASE_RUNNING)return BB_C1_RESULT_BAD;
    gg_state boot_groups[BB_C1_DRUM_KITS]; /* 160 bytes; target stack review required */
    uint32_t ticks,status;
    bb_c1_dsp_report.status=BB_C1_PASS;
    for(unsigned i=0;i<8u;i++) {
        uint32_t nonce=47u+127u*i,expected=bb_c1_float_calculate(nonce);
        status=bb_c1_dsp_exchange(BB_C1_FLOAT_JOB,nonce,expected,24000u,&ticks);
        if(ticks>bb_c1_dsp_report.float_max_ticks)bb_c1_dsp_report.float_max_ticks=ticks;
        if(status!=BB_C1_PASS){bb_c1_dsp_report.status=status;return status;}
        bb_c1_dsp_report.float_jobs++;
    }
    for(unsigned c=0;c<8u;c++) {
        const unsigned sizes[4]={1,7,32,128};
        unsigned kits=1u+c/4u,frames=sizes[c%4u];
        uint32_t ticket=bb_c1_mail.job+1u,expected,t0;
        for(unsigned k=0;k<kits;k++) {
            drum808_t *d=&bb_c1_drum_reference.kit[k];drum808_init(d);
            gg_init(&boot_groups[k]);
            for(int v=0;v<D8_NUM;v++) {
                for(int p=0;p<drum808_nparams(v);p++) {
                    const char *name=drum808_param(v,p)->name;
                    if(name[0]=='R' && name[1]=='e' && name[2]=='v' && !name[3])drum808_set(d,v,p,48);
                    if(name[0]=='D' && name[1]=='l' && name[2]=='y' && !name[3])drum808_set(d,v,p,64);
                }
                drum808_trigger(d,v,k?D8_VEL_NORMAL:1.0f);
            }
        }
        uint32_t prepare_start=fm1_ticks();
        if(!bb_group_reference_prepare(&bb_c1_drum_reference,boot_groups,kits,frames,ticket)) {
            bb_c1_dsp_report.status=BB_C1_MAILBOX_BAD;return BB_C1_MAILBOX_BAD;
        }
        bb_c1_boot_cost[c].prepare_ticks=fm1_ticks()-prepare_start;
        uint32_t clone_start=fm1_ticks();
        /* Clone timing now includes validated worker preparation, not a
         * raw structure assignment; do not compare it as the old metric. */
        if(!bb_c1_drum_prepare(&bb_c1_drum_private,bb_c1_drum_reference.kit,boot_groups,kits,frames,ticket)) {
            bb_c1_dsp_report.status=BB_C1_MAILBOX_BAD;return BB_C1_MAILBOX_BAD;
        }
        bb_c1_boot_cost[c].clone_ticks=fm1_ticks()-clone_start;
        t0=fm1_ticks();
        if(!bb_group_reference_run(&bb_c1_drum_reference,boot_groups,ticket,&expected)) {
            bb_c1_dsp_report.status=BB_C1_MAILBOX_BAD;return BB_C1_MAILBOX_BAD;
        }
        ticks=fm1_ticks()-t0;
        if(ticks>bb_c1_dsp_report.cpu0_max_ticks)bb_c1_dsp_report.cpu0_max_ticks=ticks;
        /* Generous BOOT-only bound: this is NOT the audio deadline. */
        status=bb_c1_dsp_exchange(BB_C1_DRUM_JOB,ticket,expected,480000u,&ticks);
        bb_c1_boot_cost[c].exchange_ticks=ticks;
        if(ticks>bb_c1_dsp_report.drum_max_ticks)bb_c1_dsp_report.drum_max_ticks=ticks;
        if(status!=BB_C1_PASS){bb_c1_dsp_report.status=status;return status;}
        bb_c1_dsp_report.drum_jobs++;
        /* Checked completion already happened: payload is no longer written
         * by CPU1. Compare every state byte and sample, not only a hash. */
        for(unsigned k=0;k<kits;k++) {
            if(!bb_c1_dsp_bytes_equal(&bb_c1_drum_private.kit[k],&bb_c1_drum_reference.kit[k],sizeof(drum808_t)))goto mismatch;
            if(!bb_c1_dsp_bytes_equal(&bb_c1_drum_private.groups[k],&boot_groups[k],sizeof(gg_state)))goto mismatch;
            for(unsigned b=0;b<3u;b++)
                if(!bb_c1_dsp_bytes_equal(bb_c1_drum_private.bus[k][b],bb_c1_drum_reference.bus[k][b],frames*sizeof(float)))goto mismatch;
        }
        bb_c1_dsp_report.exact_cases++;
    }
    return BB_C1_PASS;
mismatch:
    bb_c1_dsp_report.status=BB_C1_RESULT_BAD;return BB_C1_RESULT_BAD;
}


/* Completed 909 boot comparison retained in recovery package. */

/* Registers ONLY: safe before BSS clear, including warm-reset leftovers. */
static inline void bb_c1_halt(void) {
    BB_C1_CON|=2u;BB_C1_CON&=~8u;BB_C1_SYNC();
}

void __attribute__((section(".c1_text"),noreturn,used)) bb_c1_worker(void) {
    uint32_t lo=(uint32_t)(uintptr_t)_bb_c1_ustack+256u;
    uint32_t hi=(uint32_t)(uintptr_t)_bb_c1_sstack_top-1u,last_job=0;
    *(volatile uint32_t *)0x1EEF2D8u=hi;
    *(volatile uint32_t *)0x1EEF2DCu=lo;
    *(volatile uint32_t *)0x1EEF2E0u=hi;
    *(volatile uint32_t *)0x1EEF2E4u=lo;
    *(volatile uint32_t *)0x1EEF2D0u=
        (*(volatile uint32_t *)0x1EEF2D0u&~((1u<<2)|(0x1Fu<<16)))|(1u<<3);
    bb_c1_trace=0xC1000002u;
    bb_c1_mail.alive=BB_C1_ALIVE;BB_C1_SYNC();
    for(;;)(void)bb_c1_dsp_worker_step(&last_job);
}

#include "bb_c1_live_hooks.inc"
#include "bb_c1_retained_hooks.inc"
#include "bb_c1_rich_hooks.inc"
#include "bb_c1_909_runtime.inc"
#include "bb_c1_909_audio.inc"
#include "bb_c1_909_switch.inc"

/* CPU0 keeps guard changes local to this pre-audio probe. Core1's ROM start
 * needs the PC windows temporarily open. Restore their exact prior values. */
/* CPU0 snapshots exactly the values consumed by admission, before
 * a failed admission holds CPU1. No register/memory writes or retry here. */
static uint32_t bb_c1_arm_con,bb_c1_arm_bad,bb_c1_arm_job,bb_c1_arm_done;
static void bb_c1_boot_probe(void) {
    if(bb_c1_phase!=BB_C1_PHASE_BOOT) {bb_c1_live_fail(BB_C1_LIVE_BOOT_BAD);return;}
    bb_c1_phase=BB_C1_PHASE_RUNNING;
    uint32_t pc0lo=FM1_PC_LIMIT0_L,pc0hi=FM1_PC_LIMIT0_H;
    uint32_t pc1lo=FM1_PC_LIMIT1_L,pc1hi=FM1_PC_LIMIT1_H;
    uint32_t top=FM1_C0_WR_LIMIT_EN&4u,gate=BB_C1_CLOCK_GATE;
    uint32_t t0,i;
    bb_c1_halt();
    const unsigned stack_words=(unsigned)(((uintptr_t)_bb_c1_ustack_top-
        (uintptr_t)_bb_c1_ustack)/sizeof(uint32_t));
    for(i=0;i<stack_words;i++)_bb_c1_ustack[i]=BB_C1_MARK;
    fm1_guard_unlock_top();fm1__dbg_unlock();
    FM1_PC_LIMIT0_L=FM1_PC_LIMIT1_L=0u;
    FM1_PC_LIMIT0_H=FM1_PC_LIMIT1_H=UINT32_MAX;fm1__dbg_lock();
    for(i=0;i<32u;i++)*(volatile uint32_t *)(0x1EEF300u+4u*i)=0;
    *(volatile uint32_t *)0x01C7FFF8u=(uint32_t)(uintptr_t)&bb_c1_entry;
    /* Only the vendor startup gate bit is temporarily asserted, then restored.
     * This does NOT select a new PLL, divider or CPU frequency. */
    BB_C1_CLOCK_GATE=gate|8u;BB_C1_SYNC();
    BB_C1_CON|=8u;BB_C1_CON&=~2u;t0=fm1_ticks();
    while(bb_c1_mail.alive!=BB_C1_ALIVE && fm1_ticks()-t0<24000u*20u)BB_C1_SYNC();
    bb_c1_report.start_ticks=fm1_ticks()-t0;
    BB_C1_CLOCK_GATE=gate;BB_C1_SYNC();
    if(bb_c1_mail.alive!=BB_C1_ALIVE)bb_c1_report.status=BB_C1_START_TIMEOUT;
    else {
        bb_c1_report.alive=1;bb_c1_report.status=BB_C1_PASS;
        for(i=0;i<32u;i++) {
            uint32_t seed=UINT32_C(0x12345678)^(i*UINT32_C(0x1020304));
            uint32_t nonce=UINT32_C(0xFEDCBA98)+i;
            uint32_t expected=bb_c1_calculate(seed,nonce),job=bb_c1_mail.job+1u;
            if(!bb_c1_publish(&bb_c1_mail,seed,nonce)){bb_c1_report.status=BB_C1_MAILBOX_BAD;break;}
            t0=fm1_ticks();int result;
            while((result=bb_c1_verify(&bb_c1_mail,job,expected))==0 && fm1_ticks()-t0<24000u)BB_C1_SYNC();
            uint32_t ticks=fm1_ticks()-t0;
            if(ticks>bb_c1_report.max_job_ticks)bb_c1_report.max_job_ticks=ticks;
            if(result!=1){bb_c1_report.status=result<0?BB_C1_RESULT_BAD:BB_C1_JOB_TIMEOUT;break;}
            bb_c1_report.verified_jobs++;
        }
    }
    if(bb_c1_report.status==BB_C1_PASS)bb_c1_report.status=bb_c1_dsp_boot_jobs();
    /* Only full boot parity plus an idle RAM acknowledgement arms live jobs. */
    BB_C1_SYNC();
    bb_c1_arm_bad=bb_c1_mail.bad;bb_c1_arm_job=bb_c1_mail.job;
    bb_c1_arm_done=bb_c1_mail.done;bb_c1_arm_con=BB_C1_CON;
    bb_c1_live.ready=bb_c1_report.status==BB_C1_PASS
        && bb_c1_report.verified_jobs==32u && bb_c1_dsp_report.status==BB_C1_PASS
        && bb_c1_dsp_report.float_jobs==8u && bb_c1_dsp_report.drum_jobs==8u
        && bb_c1_dsp_report.exact_cases==8u && !bb_c1_arm_bad
        && !bb_c1_909_owner_armed
        && bb_c1_arm_job==bb_c1_arm_done && bb_c1_released(bb_c1_arm_con);
    if(!bb_c1_live.ready)bb_c1_halt();
    bb_c1_report.held=(BB_C1_CON&10u)==2u;
    bb_c1_live.held=bb_c1_report.held;
    if(!bb_c1_live.ready && !bb_c1_report.held) {
        bb_c1_report.status=BB_C1_HOLD_BAD;
        /* Fail closed: no audio/USB/flash startup while hold reads wrong.
         * The existing watchdog/failed-boot recovery owns a stalled boot. */
        for(;;)bb_c1_halt();
    }
    bb_c1_report.trace=bb_c1_trace;
    for(i=0;i<stack_words && _bb_c1_ustack[i]==BB_C1_MARK;i++);
    bb_c1_report.stack_bytes=stack_words*sizeof(uint32_t)-i*sizeof(uint32_t);
    fm1__dbg_unlock();
    FM1_PC_LIMIT0_L=pc0lo;FM1_PC_LIMIT0_H=pc0hi;
    FM1_PC_LIMIT1_L=pc1lo;FM1_PC_LIMIT1_H=pc1hi;
    if(top)FM1_C0_WR_LIMIT_EN|=4u;else FM1_C0_WR_LIMIT_EN&=~4u;
    fm1__dbg_lock();BB_C1_SYNC();
    bb_c1_phase=BB_C1_PHASE_RELEASED;BB_C1_SYNC();
}
/* CPU0 startup only; failed qualification must prove hold. Once taken,
 * neither this function nor either boot comparison may reuse the union. */
static bb_group_runtime *bb_c1_runtime_take(void) {
    if(bb_c1_phase!=BB_C1_PHASE_RELEASED || bb_c1_live.busy || bb_c1_909_owner_armed)return 0;
    BB_C1_SYNC();
    if(bb_c1_live.ready) {
        if(!bb_c1_released(BB_C1_CON) || bb_c1_mail.bad || bb_c1_mail.job!=bb_c1_mail.done)return 0;
    } else if(!bb_c1_report.held || (BB_C1_CON&10u)!=2u)return 0;
    /* Verified pre-audio lifetime transition only; never a failed-job retry. */
    bb_rt_init(&bb_c1_retained_state);
    bb_c1_retained_control=(__typeof__(bb_c1_retained_control)){0};
    /* Release playback leaves per-sample diagnostic tracing unarmed.
     * Input/output checksums, finite output and CPU0-owned immutable
     * coefficient comparison still precede every worker-state adoption. */
    bb_c1_phase=BB_C1_PHASE_RUNTIME;BB_C1_SYNC();
    return &bb_c1_phase_pool.runtime;
}

