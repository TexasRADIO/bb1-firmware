#define _POSIX_C_SOURCE 200809L
#include <stdlib.h>
#include <assert.h>
#include <stdio.h>
#include "bb_audio.h"
#include "bb_factory.h"
#include "bb_factory_drum_launch.h"
#include "bb_browser.h"
#include "bb_factory_pattern.h"
#include <string.h>
#ifndef FORCE_SERIAL
#define FORCE_SERIAL 0
#endif
#ifndef AUDIO_ONLY
#define AUDIO_ONLY 0
#endif
#define BB_GROUP_JOB_HOST_TEST 1
#include "@SRC@/firmware/hal/bb_c1_drum_job.h"
#include "@SRC@/firmware/hal/rich-job-transaction-prototype.h"
#define BB_C1_SYNC() ((void)0)
#define BB_C1_PHASE_RUNTIME 3
#define BB_C1_RICH_JOB 8
enum {BB_C1_LIVE_BUSY=1,BB_C1_LIVE_RESULT_BAD,BB_C1_LIVE_TICKET_END,BB_C1_LIVE_PREPARE_BAD,
 BB_C1_LIVE_PUBLISH_BAD,BB_C1_LIVE_TARGET_BAD,BB_C1_LIVE_PAYLOAD_BAD};
static struct {unsigned failed,ready,held,busy,selected,frames,ticket,active_mask,submitted_ticks,jobs;} bb_c1_live;
static struct {unsigned job,done,bad;} bb_c1_mail;
static struct {unsigned active;} bb_c1_retained_control;
static struct {struct {bb_rjt_job job;bb_rsp_plan staging;} rich;struct {unsigned phase;} persistent;} bb_c1_private_pool;
static unsigned bb_c1_phase=3,bb_c1_909_owner_armed,fail_publish,fail_wait;
static bb_rjt_lease bb_c1_rich_request;
#ifndef BB_FIXA
#define BB_FIXA 0
#endif
#if BB_FIXA /* fix A: the stand-in runs what CPU1 runs, the rich job then kit A's metal (bb_c1_rich_hooks.inc) */
static int bb_c1_rich_run(uint32_t job,uint32_t *result);
#define ORACLE_RICH_RUN(j,ticket) bb_c1_rich_run(ticket,&completion)
extern uint32_t bb_fixa_jobs,bb_fixa_ineligible,bb_fixa_trips,bb_fixa_misses,bb_p1b_guard_trips,bb_fixa_why[8];
#else
#define ORACLE_RICH_RUN(j,ticket) bb_rjt_run(j,bb_c1_rich_request,&completion)
#endif
static unsigned injection;
static uint32_t completion,bb_c1_retired,BB_C1_CON;
static int bb_c1_cpu0_available(void) {
    if(!*(volatile uint32_t *)&bb_c1_live.failed)
        return *(volatile uint32_t *)&bb_c1_live.ready!=0;
    return bb_c1_retired && bb_c1_live.held && !bb_c1_live.busy &&
           (BB_C1_CON&10u)==2u;
}

static uint32_t fm1_ticks(void){return 1;}
static int bb_c1_live_fail(unsigned reason){bb_c1_live.failed=reason;bb_c1_live.ready=0;bb_c1_live.busy=0;bb_c1_live.held=1;BB_C1_CON=2;return 0;}
static unsigned run_failed,jobs_run,inject_next;
static int bb_c1_publish(void *mail,unsigned seed,unsigned ticket){
 (void)mail;assert(seed==8);if(fail_publish)return 0;
 bb_c1_mail.job=ticket;bb_rjt_job *j=&bb_c1_private_pool.rich.job;
 unsigned mode=inject_next;inject_next=0;
 if(mode==3)((unsigned char*)&j->render.kit)[1001]^=0x10u; /* corrupted after seal, before worker */
 run_failed=!ORACLE_RICH_RUN(j,ticket);jobs_run++;
 if(mode==1)j->render.bus[0][0]+=.25f;
 if(mode==2){j->render.kit.cb.hp.a=.5f;completion=bb_rjt_hash(j,1);}
 if(mode==4)((unsigned char*)&j->next)[40]^=0x01u;
 if(mode==5){uint32_t nan=0x7fc00000u;memcpy(&j->render.bus[2][j->render.frames-1u],&nan,4);completion=bb_rjt_hash(j,1);}
 if(mode==6)fail_wait=1;
 bb_c1_mail.done=ticket;return 1;
}
static int bb_retained_wait(uint32_t *result){
 if(fail_wait||run_failed){fail_wait=0;return bb_c1_live_fail(BB_C1_LIVE_RESULT_BAD);}
 *result=completion;return 1;
}
static int injected_seal(bb_rjt_job *j,bb_rjt_lease *l){
 if(injection==6)return 0;return bb_rjt_seal(j,l);
}
#define bb_rjt_seal injected_seal
#include "@SRC@/firmware/hal/bb_c1_rich_hooks.inc"
#undef bb_rjt_seal
