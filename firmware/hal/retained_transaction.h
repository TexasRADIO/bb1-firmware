/* SPDX-License-Identifier: GPL-3.0-only */
/* HOST EXPERIMENT, NOT A HAL INTEGRATION.
 * One original event-span snapshot/result, at most two real <=64-frame
 * fragments. The caller proves publication/completion/hold externally.
 * After prepare, CPU0 must never read/write the private DSP payload until
 * FINAL checked completion. Progress is metadata, NOT an output result.
 * No midpoint state copy, input-state rescan, output scan or live commit.
 * This has the original whole-transaction boundary checks, NOT the stronger
 * repeated full-snapshot checks of the rejected independent-64-job design.
 * Exclusive lifetime and fixed pointers MUST be proven before HAL use.
 */
#ifndef BB_RETAINED_TRANSACTION_H
#define BB_RETAINED_TRANSACTION_H
#include "bb_c1_drum_job.h"
#define BB_RT_GUARD UINT32_C(0x52385458)
enum {BB_RT_EMPTY,BB_RT_PREPARED,BB_RT_ACTIVE,BB_RT_DONE,BB_RT_COMMITTED,BB_RT_FAILED};
enum {BB_RT_SNAPSHOT=1,BB_RT_INPUT,BB_RT_FRAGMENT,BB_RT_OUTPUT,BB_RT_COMMIT};
#ifndef BB_RT_OBSERVE
#define BB_RT_OBSERVE(stage) ((void)(stage))
#endif
typedef struct {
    uint32_t guard,phase,transaction,kits,frames,offset,next_job,last_job,output_check,check;
} bb_rt_state;
typedef struct {
    uint32_t transaction,job,kits,frames,offset,count,check;
} bb_rt_request;
_Static_assert(sizeof(bb_rt_state)==40,"fixed metadata only");
_Static_assert(sizeof(bb_rt_request)==28,"no pointers in request");
static uint32_t bb_rt_state_check(const bb_rt_state *s){
    return bb_c1_drum_hash(UINT32_C(2166136261),s,offsetof(bb_rt_state,check));
}
static uint32_t bb_rt_request_check(const bb_rt_request *r){
    return bb_c1_drum_hash(UINT32_C(0x52545131),r,offsetof(bb_rt_request,check));
}
static void bb_rt_seal(bb_rt_state *s){s->check=bb_rt_state_check(s);}
/* Cold/verified-idle initialization only. Never use this to retry a fault. */
static void bb_rt_init(bb_rt_state *s){
    *s=(bb_rt_state){0};s->guard=BB_RT_GUARD;bb_rt_seal(s);
}
static int bb_rt_valid(const bb_rt_state *s){
    if(!s||s->guard!=BB_RT_GUARD||s->check!=bb_rt_state_check(s)||s->phase>BB_RT_FAILED)return 0;
    if(s->phase==BB_RT_FAILED)return 1; /* refusal stays latched */
    if(s->phase==BB_RT_EMPTY)return !(s->transaction|s->kits|s->frames|s->offset|s->next_job|s->last_job|s->output_check);
    if(!s->transaction||!s->kits||s->kits>BB_C1_DRUM_KITS||!s->frames||s->frames>128u)return 0;
    if(s->transaction>UINT32_MAX-(s->frames>64u?1u:0u))return 0;
    if(s->phase==BB_RT_PREPARED)
        return !s->offset&&s->next_job==s->transaction&&!s->last_job&&!s->output_check;
    if(s->phase==BB_RT_ACTIVE)
        return s->frames>64u&&s->offset==64u&&s->last_job==s->transaction&&s->next_job==s->transaction+1u&&!s->output_check;
    return s->offset==s->frames&&!s->next_job&&s->last_job==s->transaction+(s->frames>64u?1u:0u);
}
static int bb_rt_fail(bb_rt_state *s){
    s->guard=BB_RT_GUARD;s->phase=BB_RT_FAILED;bb_rt_seal(s);return 0;
}
static int bb_rt_separate(const void *a,size_t an,const void *b,size_t bn){
    uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;
    if(an>UINTPTR_MAX-x||bn>UINTPTR_MAX-y)return 0;
    return x<=y?an<=y-x:bn<=x-y;
}
/* CPU0, only with no outstanding job and no existing private lease. */
static int bb_rt_prepare(bb_rt_state *s,bb_c1_drum_job *j,const drum808_t *live,const gg_state *groups,
                         unsigned kits,unsigned frames,uint32_t transaction){
    if(!s||!j||!live||!groups||!bb_rt_separate(s,sizeof *s,j,sizeof *j)||!bb_rt_valid(s)||
       (s->phase!=BB_RT_EMPTY&&s->phase!=BB_RT_COMMITTED)||!transaction||transaction<=s->transaction||transaction<=s->last_job||
       !kits||kits>BB_C1_DRUM_KITS||!frames||frames>128u||transaction>UINT32_MAX-(frames>64u?1u:0u))return 0;
    if(!bb_rt_separate(live,kits*sizeof *live,j,sizeof *j)||!bb_rt_separate(groups,kits*sizeof *groups,j,sizeof *j)||
       !bb_rt_separate(live,kits*sizeof *live,s,sizeof *s)||!bb_rt_separate(groups,kits*sizeof *groups,s,sizeof *s)||
       !bb_rt_separate(live,kits*sizeof *live,groups,kits*sizeof *groups))return 0;
    if(!bb_c1_drum_prepare(j,live,groups,kits,frames,transaction))return 0;
    BB_RT_OBSERVE(BB_RT_SNAPSHOT);
    *s=(bb_rt_state){BB_RT_GUARD,BB_RT_PREPARED,transaction,kits,frames,0,transaction,0,0,0};
    bb_rt_seal(s);return 1;
}
/* Pure CPU0 request construction from its own transaction record; it need
 * not inspect private DSP state or the worker's context at a midpoint. */
static bb_rt_request bb_rt_request_make(uint32_t transaction,uint32_t job,unsigned kits,unsigned frames,unsigned offset,unsigned count){
    bb_rt_request r={transaction,job,kits,frames,offset,count,0};r.check=bb_rt_request_check(&r);return r;
}
static uint32_t bb_rt_progress(const bb_rt_request *r){
    /* Distinct progress domain; never label this a DSP output checksum. */
    return bb_rt_request_check(r)^UINT32_C(0x50524731);
}
/* Worker-only fixed private objects. The transport validates each physical
 * mailbox ticket and fixed entry BEFORE this call and acknowledges AFTER it.
 * No completion, waiting, hardware access or transport reset occurs here. */
static int bb_rt_run(bb_rt_state *s,bb_c1_drum_job *j,const bb_rt_request *wire,uint32_t physical_job,uint32_t *result){
    if(!s||!j||!bb_rt_separate(s,sizeof *s,j,sizeof *j))return 0;
    if(!wire||!result)return bb_rt_fail(s);
    if(!bb_rt_separate(wire,sizeof *wire,s,sizeof *s)||!bb_rt_separate(wire,sizeof *wire,j,sizeof *j)||
       !bb_rt_separate(result,sizeof *result,s,sizeof *s)||!bb_rt_separate(result,sizeof *result,j,sizeof *j)||
       !bb_rt_separate(result,sizeof *result,wire,sizeof *wire))return bb_rt_fail(s);
    bb_rt_request r;bb_c1_drum_copy(&r,wire,sizeof r);
    if(!bb_rt_valid(s)||s->phase==BB_RT_FAILED)return bb_rt_fail(s);
    if((s->phase!=BB_RT_PREPARED&&s->phase!=BB_RT_ACTIVE)||r.check!=bb_rt_request_check(&r)||
       !physical_job||physical_job!=r.job||r.job!=s->next_job||r.transaction!=s->transaction||
       r.kits!=s->kits||r.frames!=s->frames||r.offset!=s->offset||r.offset>=r.frames||
       !r.count||r.count>64u||r.count!=(r.frames-r.offset>64u?64u:r.frames-r.offset)||
       !bb_c1_drum_valid(j,s->transaction)||j->kits!=s->kits||j->frames!=s->frames)return bb_rt_fail(s);
    if(s->phase==BB_RT_PREPARED){
        BB_RT_OBSERVE(BB_RT_INPUT);
        if(j->input_check!=bb_c1_drum_state_check(j))return bb_rt_fail(s);
        for(unsigned k=0;k<j->kits;k++)if(!bb_group_state_valid(&j->groups[k]))return bb_rt_fail(s);
        for(unsigned k=0;k<j->kits;k++)for(unsigned b=0;b<3;b++)
            for(unsigned i=0;i<j->frames;i++)j->bus[k][b][i]=0;
    }
    BB_RT_OBSERVE(BB_RT_FRAGMENT);
    for(unsigned k=0;k<j->kits;k++)bb_proposal_group_span(&j->kit[k],&j->groups[k],
        j->bus[k][0]+r.offset,j->bus[k][1]+r.offset,j->bus[k][2]+r.offset,(int)r.count);
    s->offset+=r.count;s->last_job=r.job;
    if(s->offset==s->frames){
        BB_RT_OBSERVE(BB_RT_OUTPUT);
        s->output_check=bb_c1_drum_output_check(j);s->phase=BB_RT_DONE;s->next_job=0;
        *result=s->output_check;
    }else{
        s->phase=BB_RT_ACTIVE;s->next_job=r.job+1u;*result=bb_rt_progress(&r);
    }
    bb_rt_seal(s);return 1;
}
/* CPU0 only AFTER checked FINAL mailbox completion. A confirmed hold on a
 * failed fragment is NOT permission to call commit. No midpoint publish. */
static int bb_rt_commit(bb_rt_state *s,const bb_c1_drum_job *j,uint32_t transaction,
                        unsigned kits,unsigned frames,uint32_t physical_job,uint32_t result,
                        drum808_t *live,gg_state *groups,float out[BB_C1_DRUM_KITS][3][BB_C1_DRUM_FRAMES]){
    if(!s||!j||!bb_rt_separate(s,sizeof *s,j,sizeof *j))return 0;
    if(!bb_rt_valid(s))return bb_rt_fail(s);
    if(s->phase!=BB_RT_DONE)return 0;
    if(transaction!=s->transaction||kits!=s->kits||frames!=s->frames||physical_job!=s->last_job||result!=s->output_check)
        return bb_rt_fail(s);
    if(!live||!groups||!out||!bb_rt_separate(live,kits*sizeof *live,j,sizeof *j)||
       !bb_rt_separate(groups,kits*sizeof *groups,j,sizeof *j)||!bb_rt_separate(out,kits*sizeof out[0],j,sizeof *j)||
       !bb_rt_separate(live,kits*sizeof *live,s,sizeof *s)||!bb_rt_separate(groups,kits*sizeof *groups,s,sizeof *s)||
       !bb_rt_separate(out,kits*sizeof out[0],s,sizeof *s)||!bb_rt_separate(live,kits*sizeof *live,groups,kits*sizeof *groups)||
       !bb_rt_separate(out,kits*sizeof out[0],live,kits*sizeof *live)||!bb_rt_separate(out,kits*sizeof out[0],groups,kits*sizeof *groups))return 0;
    BB_RT_OBSERVE(BB_RT_COMMIT);
    if(!bb_c1_drum_commit(j,transaction,kits,frames,result,live,groups,out))return bb_rt_fail(s);
    s->phase=BB_RT_COMMITTED;bb_rt_seal(s);return 1;
}
#endif
