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
 run_failed=!bb_rjt_run(j,bb_c1_rich_request,&completion);jobs_run++;
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

/* ---- integrated bb_audio comparison: 480 sections x two native kits + Dillo + echo/spring ---- */
static uintptr_t enter_fn(void *x){(void)x;return 0;}
static void leave_fn(void *x,uintptr_t t){(void)x;(void)t;}
static bb_audio audio;static gg_state groups[BB_MACHINES];
#define MAXSEC 1024
static bb_phrase_index catalogue[MAXSEC];static unsigned sections;
static bb_factory_choice choices[MAXSEC]; /* r9.9: each section's own catalogue entry, for its Dillo line */   /* every section the catalogue offers (480 in r9) */
static FILE *out;static uint64_t digest;static unsigned long calls,failures,retirements,three_span_blocks;static double energy;static unsigned long total_hits[2];
static void put(const void *p,size_t n){
 assert(fwrite(p,1,n,out)==n);
 const unsigned char *b=p;for(size_t i=0;i<n;i++)digest=(digest^b[i])*1099511628211ull;
}
static void dump_call(int rc){
 put(&rc,sizeof rc);put(audio.left,128*sizeof(float));put(audio.right,128*sizeof(float));
 put(audio.echo_send,128*sizeof(float));put(audio.spring_send,128*sizeof(float));
#if !AUDIO_ONLY
 put(&audio.core,sizeof audio.core);put(audio.kit,sizeof audio.kit);put(groups,sizeof groups);
 put(&audio.rich.player,sizeof audio.rich.player);put(&audio.rich.valid,sizeof audio.rich.valid);
 put(&audio.rich.render_errors,sizeof audio.rich.render_errors);
 put(audio.hits,sizeof audio.hits);put(audio.mix_gain,sizeof audio.mix_gain);put(&audio.dillo,sizeof audio.dillo);
 put(&audio.echo,sizeof audio.echo);put(&audio.spring,sizeof audio.spring);put(&audio.output,sizeof audio.output);
 unsigned c1[5]={bb_c1_live.failed,bb_c1_retired,bb_c1_live.jobs,bb_c1_rich_active,bb_c1_live.busy};put(c1,sizeof c1);
#endif
}
static double dillo_energy,send_energy;
static int render(void){
 unsigned spans_before=audio.spans;
 int rc=bb_audio_render(&audio,128);calls++;
 if(audio.spans-spans_before>=2)three_span_blocks++;
 if(!rc){failures++;
  if(bb_rich_worker_recovery_pending()&&bb_c1_retire_rich_failed()==2)retirements++;}
 for(unsigned i=0;i<128;i++){energy+=(double)audio.left[i]*audio.left[i]+(double)audio.right[i]*audio.right[i];
  dillo_energy+=(double)audio.bass[i]*audio.bass[i];send_energy+=(double)audio.echo_send[i]*audio.echo_send[i]+(double)audio.spring_send[i]*audio.spring_send[i];}
 dump_call(rc);return rc;
}
static void push(bb_command c){assert(bb_command_push(&audio.commands,&c));}
static void setup(unsigned s){
 memset(&bb_c1_live,0,sizeof bb_c1_live);bb_c1_live.ready=FORCE_SERIAL?0u:1u;bb_c1_retired=BB_C1_CON=0;
 memset(&bb_c1_mail,0,sizeof bb_c1_mail);bb_c1_rich_active=0;fail_wait=fail_publish=run_failed=inject_next=0;
 memset(&bb_coefficient_trace,0,sizeof bb_coefficient_trace);
 memset(groups,0,sizeof groups);
 assert(bb_audio_init(&audio,enter_fn,leave_fn,0));assert(bb_audio_groups_bind(&audio,groups,BB_MACHINES));
 static const unsigned bpm[4]={120,96,174,140},swing[4]={50,63,75,58};
 assert(bb_set_tempo(&audio.core,bpm[s%4]));assert(bb_set_swing(&audio.core,swing[s%4]));
 for(unsigned m=0;m<2;m++){audio.core.drums[m].enabled=1;audio.core.drums[m].muted=0;audio.core.drums[m].level=(uint8_t)(100+m*20);}
 /* r9.9: Dillo plays the section's own catalogue line through the pattern path, as the device loads it.
  * Until r9.8 the notes were written straight into the sequence; with BB_WITH_PATTERN each Dillo step plays
  * through the pattern's expression data, which that left empty, so Dillo and both effect sends were silent
  * and the oracle covered drums only. Drum-only song sections keep silent Dillo lines. */
 audio.core.synths[0].muted=0;
 {bb_pattern_data d;uint32_t t;
  if(bb_factory_prepare_synth_data(&bb_factory_catalogue,&choices[s],0,&d)==BB_FACTORY_PATTERN_OK)
   assert(bb_audio_pattern_submit(&audio,0,&d,16,&t));}
 /* Hardware workload: Dillo sends 64/127 to echo and spring, wet 50/35. */
 audio.core.effects.value[BB_FX_DILLO_ECHO]=64;audio.core.effects.value[BB_FX_DILLO_SPRING]=64;
 audio.core.effects.value[BB_FX_ECHO_WET]=50;audio.core.effects.value[BB_FX_SPRING_WET]=35;
}
static int submit(unsigned m,unsigned s){uint32_t ticket;return bb_audio_phrase_submit(&audio,m,&catalogue[s],16,&ticket);}
int main(void){
 assert(bb_factory_init());
 for(unsigned family=0;family<2;family++){
  bb_browser browser;assert(bb_browser_init(&browser,&bb_factory_catalogue));assert(bb_browser_group(&browser,family));
  for(unsigned bank=0;bank<bb_browser_bank_count(&browser);bank++){
   browser.bank_index=bank;browser.part_index=0;
   for(unsigned part=0;part<bb_browser_part_count(&browser);part++){
    browser.part_index=part;bb_factory_choice choice;assert(bb_browser_current(&browser,&choice));
    assert(sections<MAXSEC);
    choices[sections]=choice;
    assert(bb_factory_prepare_rich_drum(&bb_factory_catalogue,&choice,&catalogue[sections])==BB_FDRUM_OK);sections++;
   }
  }
 }
 assert(sections>0); const unsigned N=sections;
 out=popen("sha256sum","w");assert(out);
 unsigned long submitted=0,refused=0,published_running=0;
 for(unsigned s=0;s<N;s++){
  digest=1469598103934665603ull;setup(s);
  unsigned a0=s,b0=(s*7u+3u)%N;
  int ok=submit(0,a0)&&submit(1,b0);if(ok)submitted+=2;else refused++;
  render(); /* stopped: phrases staged immediately */
  push((bb_command){.type=BB_CMD_TRANSPORT,.value=1});
  unsigned inject=AUDIO_ONLY?0u:((s%9u==4u)?1u+(s/9u)%6u:0u);
  for(unsigned i=0;i<300;i++){
   if(inject&&i==150)inject_next=inject;
   if(i==90)push((bb_command){.type=BB_CMD_DRUM_LEVEL,.target=1,.value=60});
   /* r9.9: the FX parameter id travels in .voice (bb_control.c). Until r9.8 this line used .target, so it
    * set echo TIME to 90 ms instead of feedback to 90 %; the check below makes the intended change visible. */
   if(i==120)push((bb_command){.type=BB_CMD_FX_PARAMETER,.voice=BB_FX_ECHO_FEEDBACK,.value=90});
   if(i==125)assert(audio.core.effects.value[BB_FX_ECHO_FEEDBACK]==90&&audio.core.effects.value[BB_FX_ECHO_MS]!=90);
   render();
  }
  /* Boundary publication while running: staged by audio_before_tick inside a block. */
  bb_audio_phrase_status st0,st1;assert(bb_audio_phrase_status_read(&audio,0,&st0)&&bb_audio_phrase_status_read(&audio,1,&st1));
  uint32_t launches=st0.state.launches+st1.state.launches;
  if(submit(0,(s+1u)%N)&&submit(1,(s+N/2u)%N))submitted+=2;else refused++;
  unsigned i;
  for(i=0;i<1100;i++){
   if(i==40)push((bb_command){.type=BB_CMD_TEMPO,.value=(uint16_t)(audio.core.bpm+7u)});
   if(i==60)push((bb_command){.type=BB_CMD_SWING,.value=67});
   render();
   assert(bb_audio_phrase_status_read(&audio,0,&st0)&&bb_audio_phrase_status_read(&audio,1,&st1));
   if(st0.state.launches+st1.state.launches>=launches+2u&&i>=200)break;
  }
  if(st0.state.launches+st1.state.launches>=launches+2u)published_running++;
  /* STOP: native tails continue, sequence time does not advance. */
  push((bb_command){.type=BB_CMD_TRANSPORT,.value=0});
  for(unsigned k=0;k<250;k++)render();
  total_hits[0]+=audio.hits[0];total_hits[1]+=audio.hits[1];
  /* Full live object at section end, pointer members cleared. */
  static bb_audio copy;memcpy(&copy,&audio,sizeof copy);
  copy.groups=0;copy.commands.enter=0;copy.commands.leave=0;copy.commands.admit=0;copy.commands.admit_context=0;
  copy.sample=0;copy.aux=0;
#if BB_SAMPLE_DUAL_TRIAL
  copy.sample_b=0;
#endif
#if !AUDIO_ONLY
  put(&copy,sizeof copy);
#endif
  fprintf(stderr,"section %u digest %016llx\n",s,(unsigned long long)digest);
 }
 assert(!pclose(out));
 assert(dillo_energy>0&&send_energy>0); /* r9.9: Dillo and the effect sends are exercised */
 printf("sections=%u calls=%lu worker_jobs=%u submitted=%lu refused=%lu running_publications=%lu failed_calls=%lu retirements=%lu multi_span_calls=%lu sizeof_audio=%zu energy=%.17g dillo_energy=%.6g send_energy=%.6g hits_kitA=%lu hits_kitB=%lu\n",
  sections,calls,jobs_run,submitted,refused,published_running,failures,retirements,three_span_blocks,sizeof audio,energy,dillo_energy,send_energy,total_hits[0],total_hits[1]);
 return 0;
}
