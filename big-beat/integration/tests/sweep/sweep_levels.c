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
#define BB_GROUP_JOB_HOST_TEST 1
#include "bb_c1_drum_job.h"
#include "rich-job-transaction-prototype.h"
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
#include "bb_c1_rich_hooks.inc"
#undef bb_rjt_seal


/* ===== Sampled-kit level sweep (integration step 3, issue #34) ==================================
 * The real bb_audio engine (synchronous CPU1 mock above), the INSTALLED sample pack bound exactly as
 * bb_sample_boot.inc does (resident prepare + binding + BB AUX), all 342 offered sections.
 * Cases: A = sampled kit k (or the native 808 for reference) with B off, A sampled + B native 808,
 * A and B two different sampled kits; independent phrases on A and B; Dillo playing with sends to
 * echo and spring; master unchanged. Per case: post-guard peak, RMS (after 100 warm-up calls),
 * frames the output guard limited, invalid-input faults. Host only: not FM-1 output or timing.
 * usage: sweep_levels PACK.bbsk GAIN [CALLS]   (GAIN = resident sample gain, installed 0.25)      */
#include <math.h>
#include "bb_sample_resident_r24.h"
#include "bb_aux_voices_r1.h"
static uintptr_t enter_fn(void *x){(void)x;return 0;}
static void leave_fn(void *x,uintptr_t t){(void)x;(void)t;}
static bb_audio audio;static gg_state groups[BB_MACHINES];
#define MAXSEC 1024
static bb_phrase_index catalogue[MAXSEC];static unsigned nsec;
static bb_factory_choice choices[MAXSEC]; /* r9.9: each section's catalogue entry, for its Dillo line */
static bb_sample_resident store;static bb_aux_voices aux;
static bb_sample_audio_binding current,entries[3],bbind;static bb_sample_slot bslots[16];
static void push(bb_command c){assert(bb_command_push(&audio.commands,&c));}
static float GAIN;
static void setup(unsigned s){
 memset(&bb_c1_live,0,sizeof bb_c1_live);bb_c1_live.ready=1;bb_c1_retired=BB_C1_CON=0;
 memset(&bb_c1_mail,0,sizeof bb_c1_mail);bb_c1_rich_active=0;fail_wait=fail_publish=run_failed=inject_next=0;
 memset(groups,0,sizeof groups);
 assert(bb_audio_init(&audio,enter_fn,leave_fn,0));assert(bb_audio_groups_bind(&audio,groups,BB_MACHINES));
 /* sound library: as bb_sample_rebind_cold() */
 memset(store.slots,0,sizeof store.slots);bb_aux_voices_stop(&aux);
 for(unsigned i=0;i<3;i++){assert(bb_sample_resident_binding(&store,i,&entries[i]));if(GAIN>=0.0f)entries[i].gain=GAIN;}
 current=entries[0];
 assert(bb_audio_sample_startup(&audio,&current,entries,3,BB_SAMPLE_808_ID));
 memset(bslots,0,sizeof bslots);bbind=entries[0];bbind.slots=bslots;
 assert(bb_audio_sample_bind_b(&audio,&bbind,BB_SAMPLE_808_ID));
 assert(bb_audio_aux_bind(&audio,&aux));
 static const unsigned bpm[4]={120,96,174,140},swing[4]={50,63,75,58};
 assert(bb_set_tempo(&audio.core,bpm[s%4]));assert(bb_set_swing(&audio.core,swing[s%4]));
 for(unsigned m=0;m<2;m++){audio.core.drums[m].enabled=1;audio.core.drums[m].muted=0;audio.core.drums[m].level=127;}
 /* r9.9: Dillo plays the section's own catalogue line through the pattern path (see oracle/integration-480.c:
  * notes written straight into the sequence play nothing with BB_WITH_PATTERN, so until r9.8 this sweep
  * measured levels with Dillo and its effect sends silent). */
 audio.core.synths[0].muted=0;
 {bb_pattern_data d;uint32_t t;
  if(bb_factory_prepare_synth_data(&bb_factory_catalogue,&choices[s],0,&d)==BB_FACTORY_PATTERN_OK)
   assert(bb_audio_pattern_submit(&audio,0,&d,16,&t));}
 audio.core.effects.value[BB_FX_DILLO_ECHO]=64;audio.core.effects.value[BB_FX_DILLO_SPRING]=64;
 audio.core.effects.value[BB_FX_ECHO_WET]=50;audio.core.effects.value[BB_FX_SPRING_WET]=35;
}
static int submit(unsigned m,unsigned kit,unsigned s){
 uint32_t ticket;
 return kit==BB_SAMPLE_808_ID?bb_audio_kit_phrase_submit(&audio,m,BB_SAMPLE_808_ID,&catalogue[s],16,&ticket)
                             :bb_audio_kit_phrase_submit(&audio,m,kit,&catalogue[s],16,&ticket);
}
typedef struct {double peak,rms;unsigned long limited,faults,frames;unsigned refused,sections_limited,wrong_kit;} stat;
static void run_case(unsigned kit_a,unsigned kit_b,unsigned calls,stat *st){
 memset(st,0,sizeof *st);double sum=0;unsigned long n=0;
 for(unsigned s=0;s<nsec;s++){
  setup(s);
  if(kit_b==255u)audio.core.drums[1].enabled=0;
  unsigned b0=(s*7u+3u)%nsec;
  int ok=submit(0,kit_a,s)&&(kit_b==255u||submit(1,kit_b,b0));
  if(!ok){st->refused++;continue;}
  assert(bb_audio_render(&audio,128));
  push((bb_command){.type=BB_CMD_TRANSPORT,.value=1});
  uint32_t lim0=audio.output.limited,f0=audio.output.faults;
  for(unsigned i=0;i<calls;i++){
   if(!bb_audio_render(&audio,128))st->faults++;
   if(i<100)continue;
   for(unsigned k=0;k<128;k++){
    double l=fabs(audio.left[k]),r=fabs(audio.right[k]);
    if(l>st->peak)st->peak=l;if(r>st->peak)st->peak=r;
    sum+=(double)audio.left[k]*audio.left[k]+(double)audio.right[k]*audio.right[k];n+=2;
   }
  }
  /* the requested kit is the one sounding on each layer (no silent refusal) */
  if(bb_audio_sample_layer_id(&audio,0)!=kit_a||(kit_b!=255u&&bb_audio_sample_layer_id(&audio,1)!=kit_b))st->wrong_kit++;
  unsigned long lim=audio.output.limited-lim0;st->limited+=lim;st->faults+=audio.output.faults-f0;
  if(lim)st->sections_limited++;
 }
 st->rms=n?sqrt(sum/n):0;st->frames=n/2;
}
int main(int argc,char **argv){
 static uint8_t pack[1<<20];FILE *f=fopen(argv[1],"rb");assert(f);size_t bytes=fread(pack,1,sizeof pack,f);fclose(f);
 bytes=bb_fsmp_u32(pack+8);unsigned calls=argc>3?(unsigned)atoi(argv[3]):800u;
 /* "default": keep the binding's own gain (the shipped value) and report it */
 GAIN=strcmp(argv[2],"default")?(float)atof(argv[2]):-1.0f;
 assert(bb_sample_resident_prepare(&store,pack,bytes)&&store.count==4);
 assert(bb_aux_voices_bind(&aux,store.zones[3],BB_AUX_ZONES));
 assert(bb_factory_init());unsigned sections=0;
 for(unsigned family=0;family<2;family++){
  bb_browser browser;assert(bb_browser_init(&browser,&bb_factory_catalogue));assert(bb_browser_group(&browser,family));
  for(unsigned bank=0;bank<bb_browser_bank_count(&browser);bank++){
   browser.bank_index=bank;browser.part_index=0;
   for(unsigned part=0;part<bb_browser_part_count(&browser);part++){
    browser.part_index=part;bb_factory_choice choice;assert(bb_browser_current(&browser,&choice));
    assert(sections<MAXSEC);
    choices[sections]=choice;
    assert(bb_factory_prepare_rich_drum(&bb_factory_catalogue,&choice,&catalogue[sections])==BB_FDRUM_OK);sections++;
   }}}
 nsec=sections;fprintf(stderr,"sections %u\n",nsec);
 if(GAIN<0.0f){bb_sample_audio_binding probe;assert(bb_sample_resident_binding(&store,0,&probe));
  fprintf(stderr,"binding default gain %.3f\n",probe.gain);}
 static const char *const NAME[3]={"LinnDrum","DrumTraks","DMX"};
 struct {const char *label;unsigned a,b;} cases[]={
  {"808 A only (reference)",BB_SAMPLE_808_ID,255u},{"808 A + 808 B (reference)",BB_SAMPLE_808_ID,BB_SAMPLE_808_ID},
  {"LinnDrum A only",0,255u},{"DrumTraks A only",1,255u},{"DMX A only",2,255u},
  {"LinnDrum A + 808 B",0,BB_SAMPLE_808_ID},{"DrumTraks A + 808 B",1,BB_SAMPLE_808_ID},{"DMX A + 808 B",2,BB_SAMPLE_808_ID},
  {"LinnDrum A + DrumTraks B",0,1},{"DrumTraks A + DMX B",1,2},{"DMX A + LinnDrum B",2,0}};
 (void)NAME;
 printf("gain,case,peak,peak_dbfs,rms_dbfs,sections_limited,limited_frames,invalid_faults,refused,wrong_kit,measured_frames\n");
 for(unsigned c=0;c<sizeof cases/sizeof cases[0];c++){
  stat st;run_case(cases[c].a,cases[c].b,calls,&st);
  printf("%s,%s,%.4f,%.1f,%.1f,%u,%lu,%lu,%u,%u,%lu\n",argv[2],cases[c].label,st.peak,20*log10(st.peak>0?st.peak:1e-9),
         20*log10(st.rms>0?st.rms:1e-9),st.sections_limited,st.limited,st.faults,st.refused,st.wrong_kit,st.frames);
  fflush(stdout);
 }
 return 0;
}
