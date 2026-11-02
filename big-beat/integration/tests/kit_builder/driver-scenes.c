#define _POSIX_C_SOURCE 200809L
#include <stdlib.h>
#include <assert.h>
#include <stdio.h>
#include "bb_audio.h"
#include "bb_factory.h"
#include "bb_factory_drum_launch.h"
#include "bb_browser.h"
#include "bb_factory_pattern.h"
#include "bb_audio_session.h"
#include "bb_session.h"
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


/* ---- Dillo Duo: 1.1 scenes load identically. ----
 * make  : in the 1.1-equivalent build, build one BBSC scene per catalogue section (both kits' steps,
 *         the section's Dillo line and recipe, FX sends, tempo/swing, lead muted as 1.1 requires), write them.
 * play  : restore each scene through bb_audio_session_restore, play 600 calls + 120 tail calls, digest
 *         the audio (left/right/sends) and re-encode the live core: must equal the stored bytes.
 * duo   : (Duo build) also builds a scene with an audible lead and writes it, for the 1.1 refusal check. */
static uintptr_t enter_fn(void *x){(void)x;return 0;}
static void leave_fn(void *x,uintptr_t t){(void)x;(void)t;}
static bb_audio audio;static gg_state groups[BB_MACHINES];
static bb_audio_session_workspace ws;
static uint64_t digest;
static void put(const void *p,size_t n){const unsigned char *b=p;for(size_t i=0;i<n;i++)digest=(digest^b[i])*1099511628211ull;}
static void fresh(void){memset(&bb_c1_live,0,sizeof bb_c1_live);bb_c1_live.ready=1;bb_c1_retired=BB_C1_CON=0;
 memset(&bb_c1_mail,0,sizeof bb_c1_mail);bb_c1_rich_active=0;fail_wait=fail_publish=run_failed=inject_next=0;
 memset(&bb_coefficient_trace,0,sizeof bb_coefficient_trace);memset(groups,0,sizeof groups);
 assert(bb_audio_init(&audio,enter_fn,leave_fn,0));assert(bb_audio_groups_bind(&audio,groups,BB_MACHINES));}
int main(int argc,char **argv){
 assert(argc>=3);const char *mode=argv[1];
 assert(bb_factory_init());
 static bb_factory_choice choices[1024];unsigned N=0;
 for(unsigned family=0;family<2;family++){bb_browser browser;assert(bb_browser_init(&browser,&bb_factory_catalogue));assert(bb_browser_group(&browser,family));
  for(unsigned bank=0;bank<bb_browser_bank_count(&browser);bank++){browser.bank_index=bank;browser.part_index=0;
   for(unsigned part=0;part<bb_browser_part_count(&browser);part++){browser.part_index=part;assert(bb_browser_current(&browser,&choices[N++]));}}}
 static uint8_t wire[BB_SESSION_BYTES];
 if(!strcmp(mode,"make")||!strcmp(mode,"duo")){
  FILE *f=fopen(argv[2],"wb");assert(f);unsigned made=0;
  for(unsigned s=0;s<N;s++){
   bb_core c;assert(bb_init(&c,44100));
   static const unsigned bpm[4]={120,96,174,140},swing[4]={50,63,75,58};
   assert(bb_set_tempo(&c,bpm[s%4]));assert(bb_set_swing(&c,swing[s%4]));
   for(unsigned m=0;m<2;m++){c.drums[m].enabled=1;c.drums[m].level=(uint8_t)(100+m*20);c.drums[m].length=16;
    for(unsigned k=0;k<16;k++){unsigned v=(s*5u+k*3u+m)%11u;if((k+m+s)%3u==0)c.drums[m].steps[k].hits=(uint16_t)(1u<<v);if(k%4u==0)c.drums[m].steps[k].accents=c.drums[m].steps[k].hits;}}
   if(bb_factory_prepare_synth_pattern(&bb_factory_catalogue,&choices[s],0,&c)!=BB_FACTORY_PATTERN_OK)continue;
   c.effects.value[BB_FX_DILLO_ECHO]=64;c.effects.value[BB_FX_DILLO_SPRING]=40;
   if(!strcmp(mode,"duo")){
#if BB_WITH_TELEGRAM
    if(bb_factory_prepare_synth_pattern(&bb_factory_catalogue,&choices[s],1,&c)!=BB_FACTORY_PATTERN_OK)continue;
    c.synths[1].muted=0;c.effects.value[BB_FX_TELEGRAM_ECHO]=64;
#else
    abort();
#endif
   }
   assert(bb_session_encode(&c,wire,sizeof wire));assert(fwrite(wire,1,sizeof wire,f)==sizeof wire);made++;
  }
  fclose(f);printf("%s scenes=%u bytes_each=%u\n",mode,made,BB_SESSION_BYTES);return 0;
 }
 /* play / refuse */
 FILE *f=fopen(argv[2],"rb");assert(f);unsigned n=0,ok=0,invalid=0,reencoded_same=0;uint64_t all=1469598103934665603ull;
 while(fread(wire,1,sizeof wire,f)==sizeof wire){
  fresh();digest=1469598103934665603ull;
  bb_audio_session_result r=bb_audio_session_restore(&audio,wire,sizeof wire,&ws);n++;
  if(r!=BB_AUDIO_SESSION_OK){invalid++;continue;}ok++;
  static uint8_t again[BB_SESSION_BYTES];assert(bb_session_encode(&audio.core,again,sizeof again));
  if(!memcmp(again,wire,sizeof wire))reencoded_same++;
  bb_command t={0};t.type=BB_CMD_TRANSPORT;t.value=1;assert(bb_command_push(&audio.commands,&t));
  for(unsigned i=0;i<720;i++){if(i==600){t.value=0;assert(bb_command_push(&audio.commands,&t));}
   int rc=bb_audio_render(&audio,128);put(&rc,sizeof rc);put(audio.left,512);put(audio.right,512);put(audio.echo_send,512);put(audio.spring_send,512);}
  fprintf(stderr,"scene %u digest %016llx\n",n-1,(unsigned long long)digest);all=(all^digest)*1099511628211ull;
 }
 fclose(f);printf("restore scenes=%u ok=%u invalid=%u reencoded_identical=%u combined=%016llx\n",n,ok,invalid,reencoded_same,(unsigned long long)all);
 return 0;
}
