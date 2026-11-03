/* SPDX-License-Identifier: GPL-3.0-only
 * 2.1 Live Play P3a: the audio owner's per-part HOLD mask (bb_section_bundle.keep) on the real SONG section path.
 * Built like section_owner_test.c (the oracle's own compile line, ASan/UBSan, the real bb_audio owner), plus the
 * arranger source units for the producer check. Owner-level proofs of SPEC 8.1.1-8.1.8; the conductor controller
 * (UI, labels, notices) is P3b. usage: test-section-hold [p1..p9|diff]   (no argument = p1..p9)
 *   p9: restart contract (keep!=0 with restart!=0 refused; Duke #64 6587881964).
 *   diff: keep is never named; a seeded SONG-only script prints one digest of every drain's audio and owner state.
 *         Built on shipped 8425ff5 and on P3a, the two digests must be equal (keep=0 is SONG, byte-identical). */
#define main oracle_main
#include ORACLE_C
#undef main
#ifndef P3A_DIFF_ONLY
#include "bb_arr_sources.h"
#endif
static unsigned checks;
#define CK(x) do{checks++;if(!(x)){fprintf(stderr,"FAIL line %u %s\n",__LINE__,#x);exit(1);}}while(0)
static bb_section_bundle bundle;
static uint32_t rng=0x9e3779b9u;
static uint32_t rnd(void){rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return rng;}
static uint64_t fnv(uint64_t h,const void *p,size_t n){const uint8_t *b=p;while(n--)h=(h^*b++)*1099511628211ull;return h;}
static void collect(void){
 CK(bb_factory_init());sections=0;
 for(unsigned family=0;family<2;family++){
  bb_browser browser;CK(bb_browser_init(&browser,&bb_factory_catalogue));CK(bb_browser_group(&browser,family));
  for(unsigned bank=0;bank<bb_browser_bank_count(&browser);bank++){
   browser.bank_index=bank;browser.part_index=0;
   for(unsigned part=0;part<bb_browser_part_count(&browser);part++){
    browser.part_index=part;CK(sections<MAXSEC);CK(bb_browser_current(&browser,&choices[sections]));
    CK(bb_factory_prepare_rich_drum(&bb_factory_catalogue,&choices[sections],&catalogue[sections])==BB_FDRUM_OK);sections++;
   }
  }
 }
}
/* Sections whose drums are 16-step sixteenths, so a 16-tick bar is a phrase boundary for every part. */
static unsigned pool[MAXSEC],npool;
static void make(unsigned sec){
 memset(&bundle,0,sizeof bundle);bundle.section=sec+1;bundle.revision=2;bundle.project_revision=3;
 bundle.drum[0]=catalogue[sec];bundle.drum[1]=catalogue[(sec+17)%sections];
 if(bb_factory_prepare_synth_data(&bb_factory_catalogue,&choices[sec],0,&bundle.dillo)!=BB_FACTORY_PATTERN_OK)
  bundle.dillo.length=16;
}
static void drain(void){CK(render());}
static void stopped_load(unsigned sec){
 setup(sec);drain();make(sec);uint32_t t;CK(bb_audio_section_submit(&audio,&bundle,&t));drain();
 CK(audio.section.status.state.applied==t&&audio.rich.valid[0]&&audio.rich.valid[1]);
}
static void play(void){push((bb_command){.type=BB_CMD_TRANSPORT_RESTART});drain();CK(audio.core.running);}
static void stop(void){push((bb_command){.type=BB_CMD_TRANSPORT});}
static void run_to(uint32_t tick){unsigned n=0;while(audio.core.ticks<tick){drain();CK(++n<100000);}}
/* Musical state of one part: what it plays, where it is, its label. A,B = rich layer; 2 = Dillo (synth 0). */
static uint64_t part_hash(unsigned p){
 uint64_t h=1469598103934665603ull;
 if(p<2){h=fnv(h,&audio.rich.active[p],sizeof audio.rich.active[p]);h=fnv(h,&audio.rich.valid[p],1);
  h=fnv(h,&audio.rich.status[p],sizeof audio.rich.status[p]);h=fnv(h,&audio.rich.error[p],1);
  h=fnv(h,&audio.rich.player[p],sizeof audio.rich.player[p]);}
 else{h=fnv(h,&audio.core.synths[0],sizeof audio.core.synths[0]);
  h=fnv(h,audio.patterns.expression[0],sizeof audio.patterns.expression[0]);
  h=fnv(h,&audio.patterns.status[0],sizeof audio.patterns.status[0]);}
 return h;
}
static uint32_t part_applied(unsigned p){return p<2?audio.rich.status[p].applied:audio.patterns.status[0].applied;}
static uint64_t part_frame(unsigned p){return p<2?audio.rich.status[p].applied_frame:audio.patterns.status[0].applied_frame;}
static void slots_free(void){
 CK(audio.section.phase==BB_SECTION_FREE);
 for(unsigned i=0;i<BB_PATTERN_SLOTS;i++){CK(audio.rich.inbox[i].state==BB_PATTERN_SLOT_FREE);CK(audio.patterns.inbox[i].state==BB_PATTERN_SLOT_FREE);}
}
/* Moved parts carry the bundle's payload. */
static void moved_payload(unsigned mask){
 for(unsigned m=0;m<2;m++)if(!(mask&(1u<<m)))CK(!memcmp(&audio.rich.active[m],&bundle.drum[m],sizeof bundle.drum[m]));
 if(!(mask&4u)){CK(!memcmp(audio.core.synths[0].notes,bundle.dillo.notes,sizeof bundle.dillo.notes));
  CK(audio.core.synths[0].length==bundle.dillo.length);}
}
/* What every part plays (payload), for all-or-nothing refusals. */
static bb_phrase_index old_drums[2];static bb_sequence old_seq;
static void remember(void){old_drums[0]=audio.rich.active[0];old_drums[1]=audio.rich.active[1];old_seq=audio.core.synths[0];}
static void unchanged(void){
 for(unsigned m=0;m<2;m++)CK(!memcmp(&old_drums[m],&audio.rich.active[m],sizeof old_drums[m]));
 CK(!memcmp(old_seq.notes,audio.core.synths[0].notes,sizeof old_seq.notes));CK(old_seq.length==audio.core.synths[0].length);
}
#ifndef P3A_DIFF_ONLY
static int cue(unsigned sec,unsigned mask,uint32_t target,uint32_t *t){
 make(sec);bundle.keep=(uint8_t)mask;bundle.at_tick=1;bundle.target_tick=target;
 return bb_audio_section_submit(&audio,&bundle,t);
}
/* ---------------------------------------------------------------- 8.1.1 coordinated change on one tick */
static void p1(void){
 for(unsigned mask=0;mask<7;mask++){
  stopped_load(pool[0]);play();run_to(5);
  unsigned before=audio.commands.count;uint32_t t;CK(cue(pool[1+mask],mask,16,&t));
  CK(audio.commands.count==before+1);   /* exactly one BB_CMD_SECTION_PATTERN */
  CK(audio.commands.items[(audio.commands.write+BB_COMMAND_CAPACITY-1u)%BB_COMMAND_CAPACITY].type==BB_CMD_SECTION_PATTERN);
  unsigned seen=0;
  while(audio.core.ticks<24){
   drain();unsigned n=0;for(unsigned p=0;p<3;p++)n+=part_applied(p)==t;
   if(!n){CK(audio.core.ticks<=16);continue;}
   unsigned moved=0;for(unsigned p=0;p<3;p++)if(!(mask&(1u<<p))){moved++;CK(part_applied(p)==t);
    CK(part_frame(p)==audio.section.status.state.applied_frame);}
   CK(n==moved);CK(audio.section.status.applied_tick==16);CK(audio.section.status.playing_section==bundle.section);
   seen=1;
  }
  CK(seen);moved_payload(mask);slots_free();
 }
 fprintf(stderr,"p1 coordinated: 7 masks, every moved part applied on tick 16 only, one command each\n");
}
/* ---------------------------------------------------------------- 8.1.2 HOLD: kept parts untouched, never re-linked */
#define DR 900
static uint64_t ref_hash[DR][3];
static void world(unsigned mask,int with_cue,uint64_t (*h)[3],int cancel_at,int stop_at){
 stopped_load(pool[2]);
 /* mixed songs: B and Dillo from another catalogue entry than A */
 make(pool[3]);bundle.keep=1;uint32_t t;CK(bb_audio_section_submit(&audio,&bundle,&t));drain();
 play();run_to(5);
 if(with_cue)CK(cue(pool[4],mask,16,&t));
 for(int i=0;i<DR;i++){
  if(with_cue&&i==cancel_at)CK(bb_audio_section_cancel_submit(&audio,t));
  if(i==stop_at)stop();
  drain();for(unsigned p=0;p<3;p++)h[i][p]=part_hash(p);
 }
}
static uint64_t got_hash[DR][3];
static void p2(void){
 for(unsigned mask=1;mask<7;mask++){
  world(mask,0,ref_hash,-1,-1);world(mask,1,got_hash,-1,-1);
  CK(audio.section.status.applied_tick==16);moved_payload(mask);
  for(int i=0;i<DR;i++)for(unsigned p=0;p<3;p++)if(mask&(1u<<p))CK(got_hash[i][p]==ref_hash[i][p]);
  unsigned differs=0;for(unsigned p=0;p<3;p++)if(!(mask&(1u<<p)))differs+=got_hash[DR-1][p]!=ref_hash[DR-1][p];
  CK(differs==3u-__builtin_popcount(mask));
 }
 /* individual launches change one part only: 64 random, after a HOLD cue */
 world(1,1,got_hash,-1,-1);
 for(unsigned k=0;k<64;k++){
  unsigned p=rnd()%3,sec=pool[rnd()%npool];uint64_t other[3];for(unsigned q=0;q<3;q++)other[q]=part_hash(q);
  bb_phrase_index keep_active[2]={audio.rich.active[0],audio.rich.active[1]};bb_sequence keep_seq=audio.core.synths[0];
  uint32_t t;
  if(p<2)CK(bb_audio_phrase_submit(&audio,p,&catalogue[sec],16,&t));
  else{bb_pattern_data d;make(sec);d=bundle.dillo;CK(bb_audio_pattern_submit(&audio,0,&d,16,&t));}
  unsigned n=0;while(part_applied(p)!=t){drain();CK(++n<3000);}
  for(unsigned q=0;q<2;q++)if(q!=p)CK(!memcmp(&audio.rich.active[q],&keep_active[q],sizeof keep_active[q]));
  if(p!=2)CK(!memcmp(audio.core.synths[0].notes,keep_seq.notes,sizeof keep_seq.notes));
  (void)other;
 }
 fprintf(stderr,"p2 HOLD: 6 masks x %u drains, kept parts byte-identical to the no-cue world; 64 single launches move one part\n",DR);
}
/* ---------------------------------------------------------------- 8.1.3 rapid replace, never a mixed ticket */
static uint32_t tk[256];static uint8_t tk_mask[256];static unsigned ntk;
static unsigned mask_of(uint32_t t){for(unsigned i=0;i<ntk;i++)if(tk[i]==t)return tk_mask[i];return 255;}
static void no_mixed(void){
 uint32_t T=audio.section.status.state.applied;unsigned mask=mask_of(T);if(mask==255)return;
 for(unsigned p=0;p<3;p++){
  if(mask&(1u<<p))CK(part_applied(p)!=T);
  else{CK(part_applied(p)==T);CK(part_frame(p)==audio.section.status.state.applied_frame);}
 }
}
static void p3(void){
 /* 2 then 5 inside one bar: only 5 */
 for(unsigned mask=0;mask<7;mask++){
  stopped_load(pool[0]);play();run_to(3);uint32_t t2,t5;CK(cue(pool[5],mask,16,&t2));drain();
  {uint32_t x;CK(!cue(pool[6],(mask+3)%7,16,&x));}   /* one cue waits at a time: resubmit only when free */
  CK(bb_audio_section_cancel_submit(&audio,t2));unsigned n=0;while(audio.section.phase!=BB_SECTION_FREE){drain();CK(++n<10);}
  CK(audio.core.ticks<16);CK(cue(pool[6],(mask+3)%7,16,&t5));
  while(audio.core.ticks<20){drain();for(unsigned p=0;p<3;p++)CK(part_applied(p)!=t2);}
  CK(audio.section.status.state.applied==t5&&audio.section.status.applied_tick==16);moved_payload((mask+3)%7);
 }
 /* 2 then 5 with the bar between: 2 at 16, 5 at 32 */
 stopped_load(pool[0]);play();run_to(5);uint32_t t2,t5;CK(cue(pool[5],2,16,&t2));
 while(audio.section.status.state.applied!=t2)drain();CK(audio.section.status.applied_tick==16);
 CK(cue(pool[6],1,32,&t5));while(audio.section.status.state.applied!=t5)drain();CK(audio.section.status.applied_tick==32);
 /* 1,000 random press sequences */
 unsigned applied=0;
 for(unsigned s=0;s<1000;s++){
  stopped_load(pool[rnd()%npool]);play();ntk=0;
  for(unsigned op=0;op<12;op++){
   unsigned r=rnd()%10,mask=rnd()%7;uint32_t t;
   if(r<5){uint32_t bar=(audio.core.ticks/16u+1u)*16u;unsigned busy=audio.section.phase!=BB_SECTION_FREE;
    int ok=ntk<256&&cue(pool[rnd()%npool],mask,bar,&t);CK(!(ok&&busy));
    if(ok){tk[ntk]=t;tk_mask[ntk++]=(uint8_t)mask;}}
   else if(r<7&&ntk)(void)bb_audio_section_cancel_submit(&audio,tk[ntk-1]);
   else if(r==7&&audio.core.running)stop();
   else if(r==8&&!audio.core.running)play();
   for(unsigned k=rnd()%60;k--;){drain();no_mixed();}
   if(mask_of(audio.section.status.state.applied)!=255)applied++;
  }
 }
 CK(applied>1000);
 fprintf(stderr,"p3 replace: 7 in-bar replaces, bar-between 16/32, 1,000 random sequences (%u applied observations), no mixed ticket\n",applied);
}
/* ---------------------------------------------------------------- 8.1.4 cancel */
static void p4(void){
 for(unsigned mask=0;mask<7;mask++){
  world(mask,0,ref_hash,-1,-1);world(mask,1,got_hash,20,-1);
  CK(audio.section.status.state.result==BB_PATTERN_CANCELLED);slots_free();
  for(int i=0;i<DR;i++)for(unsigned p=0;p<3;p++)CK(got_hash[i][p]==ref_hash[i][p]);
 }
 fprintf(stderr,"p4 cancel: 7 masks, every part byte-identical to the no-cue world for %u drains\n",DR);
}
/* ---------------------------------------------------------------- 8.1.5 STOP; stopped applies at once */
static void p5(void){
 for(unsigned mask=0;mask<7;mask++){
  world(mask,0,ref_hash,-1,30);world(mask,1,got_hash,-1,30);
  CK(!audio.core.running&&audio.section.status.state.result==BB_PATTERN_CANCELLED);slots_free();
  for(int i=0;i<DR;i++)for(unsigned p=0;p<3;p++)CK(got_hash[i][p]==ref_hash[i][p]);
  /* stopped: applies within the draining block; kept parts unchanged; PLAY starts on it */
  uint64_t before[3];for(unsigned p=0;p<3;p++)before[p]=part_hash(p);uint32_t t;
  CK(cue(pool[7],mask,0,&t)==0);   /* an absolute target in the past is refused */
  make(pool[7]);bundle.keep=(uint8_t)mask;CK(bb_audio_section_submit(&audio,&bundle,&t));drain();
  CK(audio.section.status.state.applied==t&&!audio.core.running);moved_payload(mask);
  for(unsigned p=0;p<3;p++)if(mask&(1u<<p))CK(part_hash(p)==before[p]);
  play();moved_payload(mask);
 }
 fprintf(stderr,"p5 STOP: 7 masks cancelled and byte-identical to STOP alone; stopped cue applies at once, PLAY starts on it\n");
}
/* ---------------------------------------------------------------- 8.1.6 rejected, absent, late */
static void untouched_by(int (*f)(unsigned),unsigned arg){
 uint32_t lab[3];for(unsigned p=0;p<3;p++)lab[p]=part_applied(p);unsigned c=audio.commands.count;uint32_t nt=audio.patterns.next_ticket;
 remember();CK(!f(arg));CK(audio.commands.count==c);CK(audio.patterns.next_ticket==nt);slots_free();
 drain();unchanged();for(unsigned p=0;p<3;p++)CK(part_applied(p)==lab[p]);
}
static int submit_bundle(unsigned x){(void)x;uint32_t t;return bb_audio_section_submit(&audio,&bundle,&t);}
static void p6(void){
 uint32_t t;unsigned refused=0,held=0;
 for(unsigned mask=0;mask<7;mask++)for(unsigned bad=0;bad<3;bad++){
  stopped_load(pool[0]);play();run_to(5);
  make(pool[8]);bundle.keep=(uint8_t)mask;bundle.at_tick=1;bundle.target_tick=16;
  if(bad<2)bundle.drum[bad].phrase.length=0;else bundle.dillo.length=0;
  if(!(mask&(1u<<bad))){untouched_by(submit_bundle,0);refused++;}
  else{CK(bb_audio_section_submit(&audio,&bundle,&t));while(audio.section.status.state.applied!=t)drain();held++;
   for(unsigned m=0;m<2;m++)if(!(mask&(1u<<m)))CK(!memcmp(&audio.rich.active[m],&bundle.drum[m],sizeof bundle.drum[m]));}
 }
 /* voices the moving kit does not own */
 for(unsigned mask=0;mask<7;mask++)for(unsigned m=0;m<2;m++){
  stopped_load(pool[0]);uint32_t owned=bb_audio_phrase_source_voices_owned(&audio,m);
  if(!(~owned&BB_RICH_REPRESENTABLE_VOICES))continue;
  make(pool[8]);bundle.keep=(uint8_t)mask;bundle.drum[m].voices|=~owned&BB_RICH_REPRESENTABLE_VOICES;
  if(!(mask&(1u<<m))){untouched_by(submit_bundle,0);refused++;}
  else{CK(bb_audio_section_submit(&audio,&bundle,&t));drain();CK(audio.section.status.state.applied==t);held++;}
 }
 /* masks 7..255 */
 stopped_load(pool[0]);for(unsigned mask=7;mask<256;mask++){make(pool[8]);bundle.keep=(uint8_t)mask;untouched_by(submit_bundle,0);refused++;}
 /* payload corrupted after admission: whole ticket refused at the owner, DATA, nothing moves */
 for(unsigned mask=0;mask<7;mask++)for(unsigned p=0;p<3;p++){
  if(mask&(1u<<p))continue;
  stopped_load(pool[0]);play();run_to(5);uint64_t h[3];
  CK(cue(pool[9],mask,16,&t));
  if(p<2)audio.rich.inbox[audio.section.rich_slot[p]].data.phrase.length=0;
  else audio.patterns.inbox[audio.section.synth_slot].data.length=0;
  (void)h;remember();
  drain();CK(audio.section.status.error==BB_SECTION_ERROR_DATA);slots_free();
  unchanged();for(unsigned q=0;q<3;q++)CK(part_applied(q)!=t);refused++;
 }
 /* stale identity: a moving part refuses the ticket; a kept part is outside it */
 for(unsigned mask=0;mask<7;mask++){
  stopped_load(pool[0]);play();run_to(5);CK(cue(pool[9],mask,16,&t));
  unsigned m=mask&1u?0:1;audio.rich.player[mask&1u?0:1].generation++;   /* m kept if its bit is set */
  int kept=(mask>>m)&1u;while(audio.core.ticks<20)drain();
  if(kept){CK(audio.section.status.state.applied==t);held++;}
  else{CK(audio.section.status.error==BB_SECTION_ERROR_STALE);for(unsigned q=0;q<3;q++)CK(part_applied(q)!=t);refused++;}
 }
 /* LATE: main loop prepared after the bar */
 for(unsigned mask=0;mask<7;mask++){
  stopped_load(pool[0]);play();run_to(5);CK(cue(pool[9],mask,16,&t));drain();
  remember();audio.core.ticks=audio.section.due_tick+1;audio.core.pending_tick=1;drain();
  CK(audio.section.status.error==BB_SECTION_ERROR_LATE);slots_free();for(unsigned p=0;p<3;p++)CK(part_applied(p)!=t);
  unchanged();refused++;
 }
 fprintf(stderr,"p6 refused/absent/late: %u refusals leave the owner untouched; %u held parts skip preflight and identity\n",refused,held);
}
/* ---------------------------------------------------------------- 8.1.7 mutes, levels, kits, sounds, effects */
typedef struct {
 uint8_t kit[BB_MACHINES],enabled[BB_MACHINES],muted[BB_MACHINES],level[BB_MACHINES];bb_drum_sound sound[BB_MACHINES];
 uint32_t sound_revision[BB_MACHINES],drum_revision[BB_MACHINES],mix_gain[BB_MACHINES],band_gain[BB_MIX_CHANNELS];
 uint8_t synth_muted[BB_SYNTHS],band_muted,band_level[BB_MIX_CHANNELS];int8_t transpose[BB_SYNTHS];
 bb_dillo_patch dillo;bb_telegram_patch telegram;bb_fx_settings effects;uint16_t bpm;uint8_t swing;
 uint8_t glevel[BB_MACHINES][GG_COUNT],gmuted[BB_MACHINES][GG_COUNT];float gtarget[BB_MACHINES][GG_COUNT];
 uint8_t sample_selected,sample_b_selected;
} mixsnap;
static void snap(mixsnap *s){
 memset(s,0,sizeof *s);
 for(unsigned m=0;m<BB_MACHINES;m++){bb_machine *d=&audio.core.drums[m];s->kit[m]=d->kit;s->enabled[m]=d->enabled;
  s->muted[m]=d->muted;s->level[m]=d->level;s->sound[m]=d->sound;s->sound_revision[m]=d->sound_revision;
  s->drum_revision[m]=audio.drum_revision[m];s->mix_gain[m]=audio.mix_gain[m];
  for(unsigned g=0;g<GG_COUNT;g++){s->glevel[m][g]=groups[m].fader[g].level;s->gmuted[m][g]=groups[m].fader[g].muted;s->gtarget[m][g]=groups[m].fader[g].target;}}
 for(unsigned c=0;c<BB_MIX_CHANNELS;c++){s->band_gain[c]=audio.band_gain[c];s->band_level[c]=audio.core.band_level[c];}
 for(unsigned v=0;v<BB_SYNTHS;v++){s->synth_muted[v]=audio.core.synths[v].muted;s->transpose[v]=audio.transpose[v];}
 s->band_muted=audio.core.band_muted;s->dillo=audio.core.dillo;s->telegram=audio.core.telegram;s->effects=audio.core.effects;
 s->bpm=audio.core.bpm;s->swing=audio.core.swing;s->sample_selected=audio.sample_selected;s->sample_b_selected=audio.sample_b_selected;
}
static void p7(void){
 mixsnap a,b;unsigned cues=0;
 for(unsigned round=0;round<10;round++){
  stopped_load(pool[rnd()%npool]);
  for(unsigned m=0;m<2;m++){audio.core.drums[m].muted=(uint8_t)(rnd()&1);audio.core.drums[m].level=(uint8_t)(rnd()%128);
   for(unsigned g=0;g<4;g++)push((bb_command){.type=BB_CMD_GROUP_LEVEL,.target=(uint8_t)m,.voice=(uint8_t)g,.value=(uint16_t)(rnd()%128)});}
  audio.core.synths[0].muted=(uint8_t)(rnd()&1);audio.core.band_muted=(uint8_t)(rnd()&7);
  for(unsigned c=0;c<BB_MIX_CHANNELS;c++)audio.core.band_level[c]=(uint8_t)(rnd()%128);
  push((bb_command){.type=BB_CMD_FX_PARAMETER,.voice=BB_FX_ECHO_WET,.value=(uint16_t)(rnd()%101)});
  for(unsigned k=0;k<400;k++)drain();snap(&a);
  for(unsigned c=0;c<100;c++){
   unsigned mask=rnd()%7;uint32_t t;
   if(c%10==9){play();uint32_t bar=(audio.core.ticks/16u+1u)*16u;CK(cue(pool[rnd()%npool],mask,bar,&t));
    while(audio.section.status.state.applied!=t)drain();stop();drain();}
   else{make(pool[rnd()%npool]);bundle.keep=(uint8_t)mask;CK(bb_audio_section_submit(&audio,&bundle,&t));drain();
    CK(audio.section.status.state.applied==t);}
   moved_payload(mask);for(unsigned k=0;k<2;k++)drain();snap(&b);CK(!memcmp(&a,&b,sizeof a));cues++;
  }
 }
 CK(cues==1000);
 fprintf(stderr,"p7 mix: %u cues (900 stopped, 100 on the bar), mutes/levels/faders/kits/sounds/effects/tempo byte-identical\n",cues);
}
/* ---------------------------------------------------------------- 8.1.8 SONG exclusivity and producer */
static void p8(void){
 uint32_t t,u;
 /* one handoff: a SONG ticket in flight refuses a HOLD cue, and the reverse */
 for(unsigned mask=1;mask<7;mask++){
  stopped_load(pool[0]);play();run_to(3);make(pool[1]);bundle.at_tick=1;bundle.target_tick=16;CK(bb_audio_section_submit(&audio,&bundle,&t));
  CK(!cue(pool[2],mask,16,&u));drain();CK(!cue(pool[2],mask,16,&u));
  while(audio.section.status.state.applied!=t)drain();for(unsigned p=0;p<3;p++)CK(part_applied(p)==t);
  CK(cue(pool[2],mask,32,&u));make(pool[3]);bundle.at_tick=1;bundle.target_tick=32;CK(!bb_audio_section_submit(&audio,&bundle,&t));
  drain();CK(!bb_audio_section_submit(&audio,&bundle,&t));while(audio.section.status.state.applied!=u)drain();
 }
 /* SONG's producer writes keep=0 whatever the staging buffer held */
 static bb_arrangement model;static bb_arr_sources src;CK(!bb_arr_sources_init(&src,&bb_factory_catalogue));bb_arr_init(&model);
 bb_arr_section part={0};snprintf(part.name,sizeof part.name,"S");part.duration=16;uint32_t id;
 CK(!bb_arr_source_bind(&src,&choices[pool[0]],BB_SOURCE_DRUM,&part.part[0]));
 CK(!bb_arr_source_bind(&src,&choices[pool[1]],BB_SOURCE_DRUM,&part.part[1]));
 CK(!bb_arr_source_bind(&src,&choices[pool[0]],BB_SOURCE_DILLO,&part.part[2]));
 CK(!bb_arr_section_put(&model,&part,&id));
 memset(&bundle,0xff,sizeof bundle);CK(!bb_arr_prepare_section(&model,&src,id,1,&bundle));CK(bundle.keep==0);
 _Static_assert(sizeof(bb_section_bundle)==P3A_BUNDLE_SIZE,"bundle layout unchanged");
 _Static_assert(sizeof(bb_section_handoff)==P3A_HANDOFF_SIZE,"handoff layout unchanged");
 fprintf(stderr,"p8 SONG: one handoff both ways for 6 masks; producer writes keep=0 over 0xFF; layout unchanged (%zu/%zu B)\n",
  sizeof(bb_section_bundle),sizeof(bb_section_handoff));
}
#endif
#ifndef P3A_DIFF_ONLY
/* ---------------------------------------------------------------- restart contract (Duke #64 6587881964)
 * keep 1..6 x restart 0/1, running at tick 5 (Duke's PR68 d7b4d3ea set-up). A restart is a whole-transport Start
 * (bb_start + TRANSPORT_RESTART to every player), so keep!=0 with restart!=0 must be refused at submit with the
 * owner untouched; restart 0 is the conductor's form. Every held part's hash must equal the no-cue world on every
 * drain, in both cases. keep=0 with restart=1 (SONG's own form) still restarts the transport. */
#define RD 900
static uint64_t rref[RD][3],rgot[RD][3];
static void rworld(unsigned mask,int restart,int with_cue,uint64_t (*h)[3]){
 stopped_load(pool[0]);play();run_to(5);
 if(with_cue){
  make(pool[1]);bundle.keep=(uint8_t)mask;bundle.restart=(uint8_t)restart;
  if(!restart){bundle.at_tick=1;bundle.target_tick=16;}
  if(restart)untouched_by(submit_bundle,0);           /* refused: no command, ticket, slot or label */
  else{uint32_t t;CK(bb_audio_section_submit(&audio,&bundle,&t));}
 }else if(restart)drain();                            /* match untouched_by's one drain */
 for(unsigned i=0;i<RD;i++){drain();for(unsigned p=0;p<3;p++)h[i][p]=part_hash(p);}
}
static void p9(void){
 unsigned held=0;
 for(unsigned mask=1;mask<7;mask++)for(int restart=0;restart<2;restart++){
  rworld(mask,restart,0,rref);uint32_t ticks=audio.core.ticks;
  rworld(mask,restart,1,rgot);CK(audio.core.ticks==ticks);
  for(unsigned i=0;i<RD;i++)for(unsigned p=0;p<3;p++)if(mask&(1u<<p)){CK(rgot[i][p]==rref[i][p]);held++;}
  if(restart)for(unsigned p=0;p<3;p++)CK(rgot[RD-1][p]==rref[RD-1][p]);   /* refused: nothing moved */
  else{CK(audio.section.status.applied_tick==16);moved_payload(mask);}
  slots_free();
 }
 /* SONG's form is unchanged: keep=0 restart=1 is admitted and restarts the transport at once. */
 stopped_load(pool[0]);play();run_to(5);make(pool[1]);bundle.restart=1;uint32_t t;
 CK(bb_audio_section_submit(&audio,&bundle,&t));drain();
 CK(audio.section.status.state.applied==t&&audio.core.ticks<5);moved_payload(0);
 fprintf(stderr,"p9 restart contract: keep 1..6 x restart 0/1, restart refused untouched, %u held-part drains equal the no-cue world; keep=0 restart=1 restarts\n",held);
}
#endif
/* ---------------------------------------------------------------- keep=0 differential (SONG only; keep never named) */
static void diff(void){
 uint64_t d=1469598103934665603ull;unsigned drains=0;
 for(unsigned s=0;s<120;s++){
  stopped_load(pool[rnd()%npool]);if(rnd()&1)play();
  for(unsigned op=0;op<10;op++){
   uint32_t t;unsigned r=rnd()%12;make(pool[rnd()%npool]);
   if(r<4){bundle.at_tick=audio.core.running;bundle.target_tick=(audio.core.ticks/16u+1u)*16u;(void)bb_audio_section_submit(&audio,&bundle,&t);}
   else if(r==4){bundle.wait_ticks=(uint16_t)(rnd()%8);bundle.duration_ticks=rnd()%64;(void)bb_audio_section_submit(&audio,&bundle,&t);}
   else if(r==5){bundle.restart=1;(void)bb_audio_section_submit(&audio,&bundle,&t);}
   else if(r==6)(void)bb_audio_section_cancel_submit(&audio,audio.section.ticket);
   else if(r==7){if(audio.core.running)stop();else play();}
   else if(r==8&&audio.section.phase==BB_SECTION_QUEUED)audio.rich.inbox[audio.section.rich_slot[rnd()&1]].data.phrase.length=0;
   else if(r==9&&audio.section.phase==BB_SECTION_PENDING){audio.core.ticks=audio.section.due_tick+1;audio.core.pending_tick=1;}
   else if(r==10){uint32_t x;(void)bb_audio_phrase_submit(&audio,rnd()&1,&catalogue[pool[rnd()%npool]],16,&x);}
   for(unsigned k=rnd()%80;k--;){
    (void)render();drains++;
    d=fnv(d,audio.left,sizeof audio.left);d=fnv(d,audio.right,sizeof audio.right);d=fnv(d,audio.bass,sizeof audio.bass);
    d=fnv(d,&audio.section.status,sizeof audio.section.status);d=fnv(d,&audio.section.phase,1);
    for(unsigned p=0;p<3;p++){uint64_t h=part_hash(p);d=fnv(d,&h,sizeof h);}
    for(unsigned i=0;i<BB_PATTERN_SLOTS;i++){d=fnv(d,&audio.rich.inbox[i].state,sizeof audio.rich.inbox[i].state);
     d=fnv(d,&audio.patterns.inbox[i].state,sizeof audio.patterns.inbox[i].state);}
    d=fnv(d,&audio.core.ticks,4);d=fnv(d,&audio.core.frame,8);
   }
  }
 }
 printf("DIFF %016llx over %u drains\n",(unsigned long long)d,drains);
}
int main(int argc,char **argv){
 collect();out=fopen("/dev/null","wb");CK(out);
 for(unsigned i=0;i<sections;i++){const bb_phrase *a=&catalogue[i].phrase,*b=&catalogue[(i+17)%sections].phrase;
  if(a->lattice==BB_FDL_SIXTEENTHS&&b->lattice==BB_FDL_SIXTEENTHS&&a->length==16&&b->length==16)pool[npool++]=i;}
 CK(npool>=16);
 const char *only=argc>1?argv[1]:"";
 if(!strcmp(only,"diff")){diff();return 0;}
#ifndef P3A_DIFF_ONLY
 static void (*const proof[9])(void)={p1,p2,p3,p4,p5,p6,p7,p8,p9};
 for(unsigned i=0;i<9;i++){char n[3]={'p',(char)('1'+i),0};if(!*only||!strcmp(only,n))proof[i]();}
 bb_p1b_report report;bb_audio_p1b_report(&report);CK(!report.guard_trips);
 printf("PASS %u checks: P3a HOLD proofs %s on the real section owner\n",checks,*only?only:"8.1.1-8.1.8 + restart contract");
#endif
 fclose(out);return 0;
}
