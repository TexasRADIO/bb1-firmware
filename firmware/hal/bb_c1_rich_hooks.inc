#if BB_AUDIO_PROFILE
void bb_profile_worker_wait(uint32_t elapsed);
void bb_profile_worker_run(uint32_t elapsed,int valid);
#endif
/* Candidate-only CPU0 owner hooks. Not yet called by audio_span.
 * One immutable request, private render state, one verified final join.
 * Caller holds the audio owner and supplies fixed disjoint live destinations. */
static bb_rjt_lease bb_c1_rich_lease;
static uint32_t bb_c1_rich_active;
/* CPU0 records failure provenance even when no job was published. This is
 * not permission to adopt or replay a payload; retirement still verifies
 * the physical hold and excludes every other worker owner. */
static uint32_t bb_c1_rich_prepublication_failed;
static int bb_c1_rich_fail_before_publish(uint32_t reason){
 bb_c1_rich_prepublication_failed=1;
 bb_c1_live_fail(reason);return -2;
}
/* Audio owner only, at a NEW block boundary after session holds are checked.
 * Never inspect/reuse the failed payload, replay its span or restart CPU1.
 * CPU0 live kit/player remain the last accepted state. */
int bb_rich_worker_recovery_pending(void){
 return !bb_c1_retired && bb_c1_live.failed && bb_c1_live.held && !bb_c1_live.busy &&
   (BB_C1_CON&10u)==2u && bb_c1_phase==BB_C1_PHASE_RUNTIME &&
   (bb_c1_rich_prepublication_failed || (bb_c1_rich_active && bb_c1_live.selected==1u)) && !bb_c1_909_owner_armed && !bb_c1_retained_control.active;
}
static int bb_c1_retire_rich_failed(void) {
 if(bb_c1_retired)return bb_c1_cpu0_available()?1:0;
 if(!bb_c1_live.failed || !bb_c1_live.held || bb_c1_live.busy ||
    (BB_C1_CON&10u)!=2u || bb_c1_phase!=BB_C1_PHASE_RUNTIME ||
    !(bb_c1_rich_prepublication_failed || (bb_c1_rich_active && bb_c1_live.selected==1u)) ||
    bb_c1_909_owner_armed || bb_c1_retained_control.active)return 0;
 bb_c1_rich_active=0;bb_c1_rich_prepublication_failed=0;
 /* Do not clear the failure reason, mailbox, lease or captured payload. */
 bb_c1_retired=1;return 2;
}
void bb_rich_worker_cleanup(void){
 /* Called on every audio-span exit, including failures before the join.
  * A failed worker remains held/latched; never touch or reuse its payload. */
 if(bb_c1_rich_active&&!bb_c1_live.failed)(void)bb_c1_live_fail(BB_C1_LIVE_TARGET_BAD);
}
#ifndef BB_FIXA
#define BB_FIXA 0
#endif
#if BB_FIXA
/* L3 fix A (TIMING-OPTIONS-R9.9.md): a call-level rich B job may also carry kit A's metal lanes (CB, CH, OH, CY).
 * CPU0 offers the job's inputs just before begin; begin attaches them under the same ticket, before publication.
 * CPU1 runs the rich job, then the metal job; one mailbox result covers both (bb_c1_fixa_result). CPU0 verifies
 * both before ANY live write; a CPU1 fault on either latches exactly like today's rich job. */
#include "bb_c1_metal_job.h"
static bb_c1_metal_job bb_c1_metal_private __attribute__((aligned(64)));
static uint32_t bb_c1_metal_request; /* ticket whose job carries kit A's metal; 0 none. CPU0 writes it before publish. */
static struct {const drum808_t *kit;const gg_state *groups;const uint16_t *spans;unsigned nspans;const bb_metal_hits *hits;} bb_c1_fixa_offered;
uint32_t bb_c1_fixa_attached,bb_c1_fixa_prepare_refused;
static uint32_t bb_c1_fixa_result(uint32_t rich,uint32_t metal){return rich^((metal<<7)|(metal>>25))^UINT32_C(0x9E3779B9);}
/* CPU0, audio owner, immediately before bb_rich_worker_begin. Pointers stay valid until begin returns. */
void bb_fixa_offer(const drum808_t *kit,const gg_state *groups,const uint16_t *spans,unsigned nspans,const bb_metal_hits *hits){
 bb_c1_fixa_offered.kit=kit;bb_c1_fixa_offered.groups=groups;bb_c1_fixa_offered.spans=spans;
 bb_c1_fixa_offered.nspans=nspans;bb_c1_fixa_offered.hits=hits;
}
/* CPU0: the job begin just published carries kit A's metal. */
int bb_fixa_job_attached(void){return bb_c1_metal_request!=0u && bb_c1_rich_active;}
/* CPU1 (and the host stand-in): the rich job, then kit A's metal when this ticket carries it. */
static int bb_c1_rich_run(uint32_t job,uint32_t *result){
 if(!bb_rjt_run(&bb_c1_private_pool.rich.job,bb_c1_rich_request,result))return 0;
 uint32_t m=bb_c1_metal_request;
 if(m){
  uint32_t r;
  if(m!=job||!bb_c1_metal_run(&bb_c1_metal_private,job,&r))return 0;
  *result=bb_c1_fixa_result(*result,r);
 }
 return 1;
}
/* CPU0 after verified completion: the result this ticket must carry. */
static int bb_c1_fixa_expected(uint32_t rich,uint32_t *expected){
 if(!bb_c1_metal_request){*expected=rich;return 1;}
 if(!bb_c1_metal_valid(&bb_c1_metal_private,bb_c1_metal_request) || bb_c1_metal_private.result<0 ||
    bb_c1_metal_private.result>1)return 0;
 *expected=bb_c1_fixa_result(rich,bb_c1_metal_output_check(&bb_c1_metal_private));return 1;
}
#endif
int bb_rich_worker_begin(const bb_phrase_index *phrase,const void *player,
 const drum808_t *kit,const gg_state *groups,unsigned machine,unsigned frames,
 uint64_t first,unsigned rate,unsigned bpm,unsigned swing,unsigned running,
 unsigned audible,unsigned epoch){
 if(bb_c1_retired)return bb_c1_cpu0_available()?-1:-2;
 if(bb_c1_live.failed)return -2;
 if(!bb_c1_live.ready)return -1;
 if(!phrase||!player||!kit||!groups||machine>=2||!frames||frames>128||
    bb_c1_phase!=BB_C1_PHASE_RUNTIME||bb_c1_909_owner_armed||bb_c1_live.busy||
    bb_c1_rich_active||bb_c1_retained_control.active||bb_c1_private_pool.persistent.phase){
  return bb_c1_rich_fail_before_publish(BB_C1_LIVE_BUSY);
 }
 BB_C1_SYNC();
 if(bb_c1_mail.bad||bb_c1_mail.job!=bb_c1_mail.done){return bb_c1_rich_fail_before_publish(BB_C1_LIVE_RESULT_BAD);}
 uint32_t ticket=bb_c1_mail.job+1u;
 if(!ticket){return bb_c1_rich_fail_before_publish(BB_C1_LIVE_TICKET_END);}
 bb_rjt_job *j=&bb_c1_private_pool.rich.job;
 bb_rsp_plan *plan=&bb_c1_private_pool.rich.staging;
#if BB_FIXA
 /* CPU1 has completed every earlier ticket (checked above), so this slot is CPU0's again. */
 const bb_metal_hits *fixa_hits=bb_c1_fixa_offered.hits;const drum808_t *fixa_kit=bb_c1_fixa_offered.kit;
 bb_c1_fixa_offered.kit=0;bb_c1_metal_request=0;
#endif
 /* The planner snapshots CPU0-owned state into its private staging. */
 /* Ineligible plans can return to serial ONLY before publication. Nothing
  * live was modified, and no worker has seen this private staging yet. */
 if(!bb_rsp_prepare(phrase,(const bb_rsp_state *)player,rate,bpm,swing,frames,first,running,plan))return -1;
 bb_c1_drum_copy(&j->render.kit,kit,sizeof *kit);
 bb_c1_drum_copy(&j->render.groups,groups,sizeof *groups);
 if(!bb_rnp_resolve(phrase,plan,machine,frames,audible,&j->render))return -1;
 bb_c1_drum_copy(&j->next,&plan->next,sizeof j->next);
 bb_c1_rich_lease=(bb_rjt_lease){ticket,epoch,frames,j->next.generation,0};
 if(!bb_rjt_seal(j,&bb_c1_rich_lease)){return bb_c1_rich_fail_before_publish(BB_C1_LIVE_PREPARE_BAD);}
#if BB_FIXA
 /* Kit A's metal rides this ticket only if it is fully representable; otherwise B alone, as before. */
 if(fixa_kit){
  if(bb_c1_metal_prepare(&bb_c1_metal_private,fixa_kit,bb_c1_fixa_offered.groups,bb_c1_fixa_offered.spans,
                         bb_c1_fixa_offered.nspans,fixa_hits,ticket)){bb_c1_metal_request=ticket;bb_c1_fixa_attached++;}
  else bb_c1_fixa_prepare_refused++;
 }
#endif
 bb_c1_rich_request=bb_c1_rich_lease;
 bb_c1_live.selected=machine;bb_c1_live.frames=frames;bb_c1_live.ticket=ticket;
 bb_c1_live.active_mask=1u<<machine;bb_c1_live.busy=1;bb_c1_rich_active=1;
 bb_c1_live.submitted_ticks=fm1_ticks();
 if(!bb_c1_publish(&bb_c1_mail,BB_C1_RICH_JOB,ticket)){bb_c1_live_fail(BB_C1_LIVE_PUBLISH_BAD);return -2;}
 return (int)machine;
}
int bb_rich_worker_finish(unsigned machine,unsigned frames,unsigned epoch,
 void *player,drum808_t *kit,gg_state *groups,float *dry,float *rev,float *dly,
 uint32_t *hit_count){
 if(!player||!kit||!groups||!dry||!rev||!dly||!hit_count||!bb_c1_rich_active||
    !bb_c1_live.busy||bb_c1_live.failed||machine!=bb_c1_live.selected||
    frames!=bb_c1_live.frames||epoch!=bb_c1_rich_lease.epoch)
  return bb_c1_live_fail(BB_C1_LIVE_TARGET_BAD);
 uint32_t result;
 int completed=bb_retained_wait(&result);
#if BB_AUDIO_PROFILE
 /* CPU0-owned timing field, including timeout. Existing profile switch only. */
 bb_profile_worker_wait(bb_c1_live.wait_ticks);
#endif
 if(!completed)return 0; /* timeout holds CPU1; NEVER replay */
#if BB_AUDIO_PROFILE
 /* Read CPU1-owned output line only after verified completion. No raw
  * diagnostic value controls acceptance or playback. */
 BB_C1_SYNC();
 uint32_t timed_job=bb_c1_mail.timed_job,timed_ticks=bb_c1_mail.timed_ticks;
 uint32_t timed_check=bb_c1_mail.timed_check;
 bb_profile_worker_run(timed_ticks,timed_job==bb_c1_live.ticket &&
     timed_check==bb_c1_result_check(timed_job,timed_ticks) &&
     timed_ticks<=bb_c1_live.service_ticks);
#endif
 bb_rjt_job *j=&bb_c1_private_pool.rich.job;
 const bb_rsp_state *live=(const bb_rsp_state *)player;
 const uint32_t old_generation=live->generation,old_primary=live->primary,old_delayed=live->delayed;
 uint32_t checked;
 if(!bb_rjt_valid(j,&bb_c1_rich_lease)||old_generation!=bb_c1_rich_lease.generation||
    !bb_rjt_fixed_coefficients_match(kit,&j->render.kit)||
    !bb_rjt_checked_output_hash(j,&checked))return bb_c1_live_fail(BB_C1_LIVE_PAYLOAD_BAD);
#if BB_FIXA
 /* A plain finish never adopts a ticket that carries kit A's metal: bb_fixa_finish owns those. */
 if(bb_c1_metal_request)return bb_c1_live_fail(BB_C1_LIVE_TARGET_BAD);
#endif
 if(result!=checked)return bb_c1_live_fail(BB_C1_LIVE_PAYLOAD_BAD);
 /* Complete validation before all live writes. No shared scratch aliases. */
 *hit_count+=(j->next.primary-old_primary)+(j->next.delayed-old_delayed);
 bb_c1_drum_copy(player,&j->next,sizeof j->next);
 bb_c1_drum_copy(kit,&j->render.kit,sizeof *kit);bb_c1_drum_copy(groups,&j->render.groups,sizeof *groups);
 for(unsigned i=0;i<frames;i++){dry[i]=j->render.bus[0][i];rev[i]=j->render.bus[1][i];dly[i]=j->render.bus[2][i];}
 bb_c1_rich_lease.committed=1;bb_c1_rich_active=0;bb_c1_live.busy=0;bb_c1_live.jobs++;return 1;
}
#if BB_FIXA
/* Fix A join, last span of a call whose ticket carries kit A's metal. Returns 1 when B and kit A's metal are
 * adopted; 2 when CPU1's work verified but CPU0's own kit A prediction failed (a_ok = 0: a kit A metal strike
 * differed from the plan, a planned hit was left over, or kit A's metal settings changed; or the faders differ):
 * the job is dropped with NOTHING written and the caller rejects this block exactly like a P1b guard trip;
 * 0 for a CPU1 fault, latched exactly as in finish. All validation precedes every live write. */
int bb_fixa_finish(unsigned machine,unsigned frames,unsigned epoch,void *player,drum808_t *kit,gg_state *groups,
 float *dry,float *rev,float *dly,uint32_t *hit_count,
 int a_ok,drum808_t *kit_a,gg_state *groups_a,float *a_dry,float *a_rev,float *a_dly){
 if(!player||!kit||!groups||!dry||!rev||!dly||!hit_count||!kit_a||!groups_a||!a_dry||!a_rev||!a_dly||
    !bb_c1_rich_active||!bb_c1_live.busy||bb_c1_live.failed||machine!=bb_c1_live.selected||
    frames!=bb_c1_live.frames||epoch!=bb_c1_rich_lease.epoch||!bb_c1_metal_request||
    bb_c1_metal_request!=bb_c1_live.ticket)
  return bb_c1_live_fail(BB_C1_LIVE_TARGET_BAD);
 uint32_t result;
 int completed=bb_retained_wait(&result);
#if BB_AUDIO_PROFILE
 bb_profile_worker_wait(bb_c1_live.wait_ticks);
#endif
 if(!completed)return 0; /* timeout holds CPU1; NEVER replay */
#if BB_AUDIO_PROFILE
 BB_C1_SYNC();
 uint32_t timed_job=bb_c1_mail.timed_job,timed_ticks=bb_c1_mail.timed_ticks;
 uint32_t timed_check=bb_c1_mail.timed_check;
 bb_profile_worker_run(timed_ticks,timed_job==bb_c1_live.ticket &&
     timed_check==bb_c1_result_check(timed_job,timed_ticks) &&
     timed_ticks<=bb_c1_live.service_ticks);
#endif
 bb_rjt_job *j=&bb_c1_private_pool.rich.job;
 const bb_rsp_state *live=(const bb_rsp_state *)player;
 const uint32_t old_generation=live->generation,old_primary=live->primary,old_delayed=live->delayed;
 uint32_t checked,expected;
 if(!bb_rjt_valid(j,&bb_c1_rich_lease)||old_generation!=bb_c1_rich_lease.generation||
    !bb_rjt_fixed_coefficients_match(kit,&j->render.kit)||
    !bb_rjt_checked_output_hash(j,&checked)||!bb_c1_fixa_expected(checked,&expected)||result!=expected)
  return bb_c1_live_fail(BB_C1_LIVE_PAYLOAD_BAD);
 bb_c1_metal_job *m=&bb_c1_metal_private;
 const uint32_t ticket=bb_c1_metal_request;
 /* CPU1's work is verified. What follows checks CPU0's prediction, so a miss is not a CPU1 fault. */
 if(!a_ok || (m->result==1 && m->terms.frames!=frames) ||
    __builtin_memcmp(&m->groups,groups_a,sizeof *groups_a)){
  bb_c1_metal_request=0;bb_c1_rich_lease.committed=1;bb_c1_rich_active=0;bb_c1_live.busy=0;return 2;
 }
 /* Kit A first: its commit re-scans the metal values and writes nothing on a bad one (a CPU1 fault). */
 if(!bb_c1_metal_commit(m,ticket,bb_c1_metal_output_check(m),kit_a,groups_a,a_dry,a_rev,a_dly))
  return bb_c1_live_fail(BB_C1_LIVE_PAYLOAD_BAD);
 *hit_count+=(j->next.primary-old_primary)+(j->next.delayed-old_delayed);
 bb_c1_drum_copy(player,&j->next,sizeof j->next);
 bb_c1_drum_copy(kit,&j->render.kit,sizeof *kit);bb_c1_drum_copy(groups,&j->render.groups,sizeof *groups);
 for(unsigned i=0;i<frames;i++){dry[i]=j->render.bus[0][i];rev[i]=j->render.bus[1][i];dly[i]=j->render.bus[2][i];}
 bb_c1_metal_request=0;
 bb_c1_rich_lease.committed=1;bb_c1_rich_active=0;bb_c1_live.busy=0;bb_c1_live.jobs++;return 1;
}
#endif
/* Timing P1b guard trip only (bb_audio.c bb_p1b_guard_trip). A trip is not a
 * CPU1 fault: join the in-flight job, verify its completion and seal as
 * finish does, then DROP it. No live player, kit, group or bus is written, so
 * CPU0's last accepted state stands. Any join or verification failure IS a
 * CPU1 fault and latches through bb_c1_live_fail exactly as in finish; the
 * existing retirement path is unchanged. Returns 1 when dropped cleanly. */
int bb_rich_worker_discard(void){
 if(!bb_c1_rich_active)return 1;
 if(bb_c1_live.failed)return 0;
 if(!bb_c1_live.busy)return bb_c1_live_fail(BB_C1_LIVE_TARGET_BAD);
 uint32_t result;
 int completed=bb_retained_wait(&result);
#if BB_AUDIO_PROFILE
 bb_profile_worker_wait(bb_c1_live.wait_ticks);
#endif
 if(!completed)return 0; /* timeout/bad completion latched: CPU1 fault */
 bb_rjt_job *j=&bb_c1_private_pool.rich.job;
 uint32_t checked;
 if(!bb_rjt_valid(j,&bb_c1_rich_lease)||!bb_rjt_checked_output_hash(j,&checked))
  return bb_c1_live_fail(BB_C1_LIVE_PAYLOAD_BAD);
#if BB_FIXA
 if(!bb_c1_fixa_expected(checked,&checked))return bb_c1_live_fail(BB_C1_LIVE_PAYLOAD_BAD);
 bb_c1_metal_request=0;
#endif
 if(result!=checked)return bb_c1_live_fail(BB_C1_LIVE_PAYLOAD_BAD);
 bb_c1_rich_lease.committed=1;bb_c1_rich_active=0;bb_c1_live.busy=0;return 1;
}
