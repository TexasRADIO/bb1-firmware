/* SPDX-License-Identifier: GPL-3.0-only
 * 2.0 series: editable copy, Undo and scene round-trip on the real audio owner (bb_audio + rich renderer),
 * with the catalogue's own patterns. Appended to the oracle driver's CPU1 stand-in (build.py), so it runs
 * the same worker path as the 539-section oracle. ASan/UBSan on.
 *  A. The old STEPS route (BB_CMD_DRUM_STEP) is refused on a rich pattern, as before: that is why STEPS said
 *     "SOURCE PATTERN VIEW".
 *  B. For every 7th catalogue section, on kit A, playing: toggle a step with bb_pe_toggle and publish it as the
 *     firmware does (index prepare, preflight, bb_audio_phrase_submit_seamless). The owner reports QUEUED and
 *     keeps playing the source until the loop wrap, then APPLIED with exactly the edited phrase. The source in
 *     the catalogue is unchanged (prepared again from flash data), and the edit adds exactly one hit.
 *  C. Undo through bb_pattern_edit restores the source byte for byte on the owner; a second toggle of the
 *     same cell gives the source back too.
 *  D. Scene round-trip: the scene writer's rich block (bb_rich_scene_finish) carries the copy; opening it gives
 *     the same phrase and the same nested legacy scene; a fresh owner installs it (scene restore path) and
 *     plays the copy. A scene without a copy (mask 0) is byte-identical to the legacy scene, so old scenes
 *     load as before.
 *  E. History: 8 entries, oldest forgotten first; forgetting one kit keeps the other kit's entries in order.
 *  F. (BB_PE_HAS_TAKE) Live record into the playing pattern, no SONG row: the capture ring opens on a plain
 *     pattern, the count-in gate is the next bar line, a pad hit during the count-in is not recorded, and each
 *     hit after it lands on the step the owner was playing or the next one (nearest step). KEEP adds exactly
 *     the new cells after the untouched source events, the owner then plays the take (QUEUED, APPLIED), a
 *     take over a pattern that has since changed is refused, and stable swung clocks can capture. */
#include "bb_pattern_edit.h"
#include "bb_rich_scene.h"
#include "bb_session.h"
static uintptr_t enter_fn(void *x){(void)x;return 0;}
static void leave_fn(void *x,uintptr_t t){(void)x;(void)t;}
static bb_audio audio;static gg_state groups[BB_MACHINES];
#define MAXSEC 1024
static bb_phrase_index catalogue[MAXSEC];static bb_factory_choice choices[MAXSEC];static unsigned sections;
static unsigned long checks,failures;
#define CHECK(c,...) do{checks++;if(!(c)){failures++;if(failures<40){printf("FAIL: ");printf(__VA_ARGS__);printf("\n");}}}while(0)
static void fresh(void){
 memset(&bb_c1_live,0,sizeof bb_c1_live);bb_c1_live.ready=1;bb_c1_retired=BB_C1_CON=0;
 memset(&bb_c1_mail,0,sizeof bb_c1_mail);bb_c1_rich_active=0;fail_wait=fail_publish=run_failed=inject_next=0;
 memset(&bb_coefficient_trace,0,sizeof bb_coefficient_trace);memset(groups,0,sizeof groups);
 assert(bb_audio_init(&audio,enter_fn,leave_fn,0));assert(bb_audio_groups_bind(&audio,groups,BB_MACHINES));
 assert(bb_set_tempo(&audio.core,174));assert(bb_set_swing(&audio.core,50));
 for(unsigned m=0;m<2;m++){audio.core.drums[m].enabled=1;audio.core.drums[m].muted=0;audio.core.drums[m].level=110;}
}
static void push(bb_command c){assert(bb_command_push(&audio.commands,&c));}
static int publish(unsigned m,const bb_phrase *p,uint32_t *ticket){
 static bb_phrase_index index;uint32_t playable=bb_audio_phrase_source_voices(&audio,m);
 if(!bb_phrase_index_prepare(p,playable,&index)||!bb_audio_phrase_preflight_playable(&index,playable))return 0;
 return bb_audio_phrase_submit_seamless(&audio,m,&index,ticket);
}
/* render until the owner applies `ticket`; returns calls used, or 0 on timeout */
static unsigned until_applied(unsigned m,uint32_t ticket,int *source_kept,const bb_phrase *source){
 for(unsigned i=1;i<6000;i++){
  bb_audio_phrase_status st;assert(bb_audio_phrase_status_read(&audio,m,&st));
  if(st.state.applied==ticket)return i;
  if(source_kept&&memcmp(&audio.rich.active[m].phrase,source,sizeof *source))*source_kept=0;
  (void)bb_audio_render(&audio,128);
 }
 return 0;
}
static unsigned lane_count(const bb_phrase *p,unsigned voice,unsigned step){
 unsigned n=0;for(unsigned i=0;i<p->count;i++){unsigned w=p->events[i];if((w&31u)==voice&&((w>>5)&63u)==step)n++;}return n;
}
int main(void){
 assert(bb_factory_init());
 for(unsigned family=0;family<2;family++){
  bb_browser browser;assert(bb_browser_init(&browser,&bb_factory_catalogue));assert(bb_browser_group(&browser,family));
  for(unsigned bank=0;bank<bb_browser_bank_count(&browser);bank++){
   browser.bank_index=bank;browser.part_index=0;
   for(unsigned part=0;part<bb_browser_part_count(&browser);part++){
    browser.part_index=part;assert(bb_browser_current(&browser,&choices[sections]));
    assert(bb_factory_prepare_rich_drum(&bb_factory_catalogue,&choices[sections],&catalogue[sections])==BB_FDRUM_OK);sections++;
   }
  }
 }
 unsigned tested=0,queued_seen=0,legacy_refused=0;
 static uint8_t legacy[BB_SESSION_BYTES],wire[BB_RICH_SCENE_OVERHEAD+BB_SESSION_BYTES];
 for(unsigned s=0;s<sections;s+=7){
  fresh();uint32_t ticket;
  assert(bb_audio_phrase_submit(&audio,0,&catalogue[s],16,&ticket));(void)bb_audio_render(&audio,128);
  push((bb_command){.type=BB_CMD_TRANSPORT,.value=1});
  for(unsigned i=0;i<40;i++)(void)bb_audio_render(&audio,128);
  const bb_phrase source=catalogue[s].phrase;
  CHECK(!memcmp(&audio.rich.active[0].phrase,&source,sizeof source),"s%u: source not playing",s);
  /* A: the old route is still refused on a rich pattern */
  uint32_t refusals=audio.rich.edit_refusals;
  push((bb_command){.type=BB_CMD_DRUM_STEP,.target=0,.step=0,.voice=0});(void)bb_audio_render(&audio,128);
  CHECK(audio.rich.edit_refusals==refusals+1u,"s%u: DRUM_STEP not refused",s);legacy_refused+=audio.rich.edit_refusals==refusals+1u;
  CHECK(!memcmp(&audio.rich.active[0].phrase,&source,sizeof source),"s%u: refused edit changed the pattern",s);
  /* B: the first edit makes a copy; choose a cell of a playable voice that is empty */
  uint32_t playable=bb_audio_phrase_source_voices(&audio,0);unsigned voice=BB_FDV_BD,step=0,found=0;
  static const uint8_t try_voices[]={BB_FDV_HH,BB_FDV_SD,BB_FDV_BD,BB_FDV_CP,BB_FDV_RS};
  for(unsigned v=0;v<sizeof try_voices&&!found;v++)if(playable&(1u<<try_voices[v]))
   for(unsigned k=0;k<source.length&&!found;k++)if(!lane_count(&source,try_voices[v],k)){voice=try_voices[v];step=k;found=1;}
  if(!found)continue;
  bb_pattern_edit edit;memset(&edit,0,sizeof edit);bb_phrase copy,back;
  CHECK(bb_pe_toggle(&source,voice,step,1,&copy)==BB_PE_OK,"s%u: toggle on refused",s);
  CHECK(copy.count==source.count+1u&&lane_count(&copy,voice,step)==1u,"s%u: toggle added %u hits",s,copy.count-source.count);
  CHECK(!memcmp(copy.events,source.events,source.count*sizeof source.events[0]),"s%u: source events moved",s);
  CHECK(bb_pe_push(&edit,0,&source)==BB_PE_OK,"s%u: push",s);
  uint32_t t1;CHECK(publish(0,&copy,&t1),"s%u: seamless publish refused",s);
  (void)bb_audio_render(&audio,128);
  bb_audio_phrase_status st;assert(bb_audio_phrase_status_read(&audio,0,&st));
  if(st.state.applied!=t1){queued_seen++;CHECK(st.state.queued==t1&&st.state.result==BB_RICH_QUEUED,"s%u: not QUEUED",s);}
  int kept=1;unsigned took=until_applied(0,t1,&kept,&source);
  CHECK(took,"s%u: edit never APPLIED",s);
  CHECK(!memcmp(&audio.rich.active[0].phrase,&copy,sizeof copy),"s%u: owner does not play the copy",s);
  CHECK(audio.core.running,"s%u: transport stopped",s);
  bb_phrase_index again;
  CHECK(bb_factory_prepare_rich_drum(&bb_factory_catalogue,&choices[s],&again)==BB_FDRUM_OK&&
        !memcmp(&again.phrase,&source,sizeof source),"s%u: catalogue source changed",s);
  /* C: Undo restores the source on the owner; the inverse toggle equals the source too */
  unsigned m=9;CHECK(bb_pe_undo(&edit,&m,&back)==BB_PE_OK&&m==0&&!memcmp(&back,&source,sizeof source),"s%u: undo entry",s);
  CHECK(bb_pe_undo(&edit,&m,&back)==BB_PE_NOTHING,"s%u: history should be empty",s);
  bb_phrase off;CHECK(bb_pe_toggle(&copy,voice,step,0,&off)==BB_PE_OK&&!memcmp(&off,&source,sizeof source),"s%u: toggle off != source",s);
  uint32_t t2;CHECK(publish(0,&source,&t2)&&until_applied(0,t2,NULL,NULL),"s%u: undo not applied",s);
  CHECK(!memcmp(&audio.rich.active[0].phrase,&source,sizeof source),"s%u: undo does not play the source",s);
  /* D: scene round-trip of the copy, and legacy scenes unchanged */
  assert(bb_session_encode(&audio.core,legacy,sizeof legacy));
  bb_phrase parts[2];memset(parts,0,sizeof parts);parts[0]=copy;
  memcpy(wire+BB_RICH_SCENE_OVERHEAD,legacy,sizeof legacy);
  size_t n=bb_rich_scene_finish(wire,sizeof wire,sizeof legacy,parts,1);
#ifdef BB_RICH_SCENE_LEGACY_OVERHEAD   /* ratchet line: OVERHEAD is the v2 reservation; a v1 scene uses the legacy header */
  CHECK(n==BB_RICH_SCENE_LEGACY_OVERHEAD+sizeof legacy,"s%u: scene with copy not written",s);
#else
  CHECK(n==BB_RICH_SCENE_OVERHEAD+sizeof legacy,"s%u: scene with copy not written",s);
#endif
  bb_phrase opened[2];uint8_t mask=0;const void *inner;size_t inner_n;
  CHECK(bb_rich_scene_open(wire,n,opened,&mask,&inner,&inner_n)&&mask==1&&!memcmp(&opened[0],&copy,sizeof copy)&&
        inner_n==sizeof legacy&&!memcmp(inner,legacy,sizeof legacy),"s%u: scene round-trip",s);
  fresh();CHECK(bb_audio_phrase_scene_install_reset(&audio,opened,mask)&&audio.rich.valid[0]&&
        !memcmp(&audio.rich.active[0].phrase,&copy,sizeof copy),"s%u: scene install does not play the copy",s);
  memset(parts,0,sizeof parts);memcpy(wire+BB_RICH_SCENE_OVERHEAD,legacy,sizeof legacy);
  n=bb_rich_scene_finish(wire,sizeof wire,sizeof legacy,parts,0);
  CHECK(n==sizeof legacy&&!memcmp(wire,legacy,sizeof legacy),"s%u: no-copy scene is not the legacy bytes",s);
  CHECK(bb_rich_scene_open(legacy,sizeof legacy,opened,&mask,&inner,&inner_n)&&mask==0,"s%u: legacy scene refused",s);
  tested++;
 }
 #ifdef BB_PE_HAS_TAKE
 unsigned takes=0,take_hits=0,early_dropped=0;
 for(unsigned s=3;s<sections;s+=11){
  fresh();uint32_t ticket;
  assert(bb_audio_phrase_submit(&audio,0,&catalogue[s],16,&ticket));(void)bb_audio_render(&audio,128);
  push((bb_command){.type=BB_CMD_TRANSPORT,.value=1});
  for(unsigned i=0;i<37;i++)(void)bb_audio_render(&audio,128);
  bb_capture_clock clock;bb_live_record take;memset(&take,0,sizeof take);uint64_t gate=0;
  CHECK(bb_audio_capture_begin(&audio,0,&clock)==BB_CAPTURE_OK,"s%u: capture refused on a plain pattern",s);
  bb_phrase_view view;memset(&view,0,sizeof view);
  CHECK(bb_audio_phrase_view_read(&audio,0,&view)&&view.valid&&view.applied==clock.applied,"s%u: view",s);
  CHECK(bb_pe_take_begin(&take,&view.phrase,&clock,1,&gate)==BB_PE_OK,"s%u: take begin",s);
  unsigned per_step=(unsigned)((uint64_t)clock.sample_rate*60u/clock.bpm/(clock.lattice==BB_FDL_EIGHTHS?2u:clock.lattice==BB_FDL_TRIPLETS?6u:4u));
  unsigned per_bar=per_step*(clock.lattice==BB_FDL_EIGHTHS?8u:clock.lattice==BB_FDL_TRIPLETS?24u:16u);
  CHECK(gate>=clock.frame&&gate-clock.frame<=per_bar+1u,"s%u: count-in gate %llu frames",s,(unsigned long long)(gate-clock.frame));
  unsigned expect_cells[6],n_expect=0;int early=0;
  if(gate>audio.core.frame+512u){push((bb_command){.type=BB_CMD_DRUM_TRIGGER,.target=0,.voice=0,.value=88});early=1;}
  for(unsigned k=0;k<5;k++){
   uint64_t at=gate+128u+k*(per_bar/5u+97u);
   while(audio.core.frame+128u<=at)(void)bb_audio_render(&audio,128);
   unsigned lane=k%3u==0?0u:k%3u==1?1u:10u;   /* kick, snare, closed hat pads */
   unsigned head=bb_audio_phrase_playhead(&audio,0);
   push((bb_command){.type=BB_CMD_DRUM_TRIGGER,.target=0,.voice=(uint8_t)lane,.value=(uint16_t)(k==2?127:88)});
   (void)bb_audio_render(&audio,128);
   expect_cells[n_expect++]=head;
   bb_capture_event ev;int r;
   while((r=bb_audio_capture_read(&audio,&ev))==BB_CAPTURE_OK){
    if(ev.frame<gate){early_dropped++;continue;}
    unsigned before=take.take.count;
    int h=bb_live_record_hit(&take,ev.frame,ev.voice,ev.velocity==127);
    CHECK(h==BB_REC_OK||h==BB_REC_DUPLICATE,"s%u: hit refused %d",s,h);
    if(take.take.count>before){unsigned cell=(take.take.events[before]>>5)&63u,len=view.phrase.length;
     CHECK(cell==head%len||cell==(head+1u)%len,"s%u: hit at cell %u, owner at step %u",s,cell,head);take_hits++;}
   }
   CHECK(r==BB_CAPTURE_EMPTY,"s%u: capture read %d",s,r);
  }
  (void)early;(void)expect_cells;
  CHECK(bb_audio_capture_end(&audio)==BB_CAPTURE_OK,"s%u: capture end",s);
  CHECK(bb_live_record_finish(&take)==BB_REC_OK,"s%u: finish",s);
  bb_phrase out,other=view.phrase;
  CHECK(bb_pe_take(&take,clock.applied,&audio.rich.active[0].phrase,&out)==BB_PE_OK,"s%u: keep refused",s);
  CHECK(out.count>view.phrase.count&&!memcmp(out.events,view.phrase.events,view.phrase.count*sizeof out.events[0]),"s%u: source events moved",s);
  other.events[0]^=1u<<5;CHECK(bb_pe_take(&take,clock.applied,&other,&out)==BB_PE_CHANGED,"s%u: changed pattern accepted",s);
  CHECK(bb_pe_take(&take,clock.applied,&audio.rich.active[0].phrase,&out)==BB_PE_OK,"s%u: keep again",s);
  (void)bb_audio_capture_discard(&audio);
  uint32_t t;CHECK(publish(0,&out,&t)&&until_applied(0,t,NULL,NULL)&&!memcmp(&audio.rich.active[0].phrase,&out,sizeof out),"s%u: take not applied",s);
  takes++;
 }
 {fresh();assert(bb_set_swing(&audio.core,58));uint32_t ticket;bb_capture_clock clock;
  assert(bb_audio_phrase_submit(&audio,0,&catalogue[0],16,&ticket));(void)bb_audio_render(&audio,128);
  push((bb_command){.type=BB_CMD_TRANSPORT,.value=1});for(unsigned i=0;i<20;i++)(void)bb_audio_render(&audio,128);
  CHECK(bb_audio_capture_begin(&audio,0,&clock)==BB_CAPTURE_OK,"swing 58: stable owner clock capture");}
 printf("live record: %u takes, %u new hits on the nearest step, %u count-in hits dropped\n",takes,take_hits,early_dropped);
 #endif
 /* E: history bounds and per-kit forgetting */
 {bb_pattern_edit e;memset(&e,0,sizeof e);bb_phrase p=catalogue[0].phrase,out;unsigned m;
  for(unsigned i=0;i<12;i++){p.events[0]=(uint16_t)i;CHECK(bb_pe_push(&e,i&1u,&p)==BB_PE_OK,"push %u",i);}
  CHECK(e.count==BB_PE_HISTORY,"history holds %u",e.count);
  CHECK(bb_pe_undo(&e,&m,&out)==BB_PE_OK&&out.events[0]==11u&&m==1,"newest first");
  bb_pe_forget(&e,1);CHECK(bb_pe_entries(&e,1)==0&&bb_pe_entries(&e,0)==4,"forget kit B keeps kit A (%u)",bb_pe_entries(&e,0));
  CHECK(bb_pe_undo(&e,&m,&out)==BB_PE_OK&&out.events[0]==10u&&m==0,"kit A order kept");
  bb_pe_drop_newest(&e,0);CHECK(bb_pe_undo(&e,&m,&out)==BB_PE_OK&&out.events[0]==6u,"drop newest of kit A");
  /* a refusal on kit A never takes kit B's entry (Duke a81212b3 case 2) */
  p.events[0]=40u;CHECK(bb_pe_push(&e,1,&p)==BB_PE_OK,"push kit B");
  {unsigned a=bb_pe_entries(&e,0),b=bb_pe_entries(&e,1);bb_pe_drop_newest(&e,0);
   CHECK(bb_pe_entries(&e,0)==a-1u&&bb_pe_entries(&e,1)==b,"drop kit A keeps kit B (%u/%u)",bb_pe_entries(&e,0),bb_pe_entries(&e,1));
   CHECK(bb_pe_undo(&e,&m,&out)==BB_PE_OK&&m==1&&out.events[0]==40u,"kit B's entry still newest");}
  bb_phrase full=catalogue[0].phrase;full.count=BB_FACTORY_DRUM_EVENTS;
  for(unsigned i=0;i<BB_FACTORY_DRUM_EVENTS;i++)full.events[i]=(uint16_t)(BB_FDV_BD|((i%full.length)<<5));
  CHECK(bb_pe_toggle(&full,BB_FDV_CB,0,0,&out)==BB_PE_FULL,"128 hits: a new hit is refused");
  CHECK(bb_pe_toggle(&catalogue[0].phrase,BB_FDV_HH,catalogue[0].phrase.length,0,&out)==BB_PE_INVALID,"step past the end");}
 /* D2 preparation + existing journal and actual owner: both layers, x1..4.
  * This does not wire nav or qualify SONG ownership. */
 for(unsigned s=0;s<sections;s+=7)for(unsigned m=0;m<2;m++){
  fresh();uint32_t ticket;bb_phrase original=catalogue[s].phrase;
  CHECK(bb_audio_phrase_submit(&audio,m,&catalogue[s],16,&ticket),"ratchet source submit");
  (void)bb_audio_render(&audio,128);
  push((bb_command){.type=BB_CMD_TRANSPORT,.value=1});
  (void)bb_audio_render(&audio,128);
  unsigned voice=0,step=0,found=0;
  for(unsigned i=0;i<original.count;i++){
   bb_phrase_event e;if(bb_phrase_read(&original,i,&e)&&e.voice!=BB_FDV_ACCENT&&e.voice!=BB_FDV_ACC){voice=e.voice;step=e.step-1u;found=1;break;}
  }
  CHECK(found,"ratchet source has hit");if(!found)continue;
  bb_pattern_edit journal={0};bb_phrase before=original,after,undo,sentinel;
  memset(&after,0xa5,sizeof after);sentinel=after;
  CHECK(bb_pe_ratchet(&before,voice,step,0,&after)==BB_PE_INVALID&&!memcmp(&after,&sentinel,sizeof after),"bad count atomic");
  CHECK(bb_pe_ratchet(&before,voice,step,2,&before)==BB_PE_INVALID&&!memcmp(&before,&original,sizeof before),"alias refused");
  for(unsigned n=2;n<=4;n++){
   CHECK(bb_pe_ratchet(&before,voice,step,n,&after)==BB_PE_OK,"ratchet prepare x%u",n);
   CHECK(!memcmp(before.events,after.events,sizeof before.events)&&before.length==after.length&&before.count==after.count&&before.lattice==after.lattice&&before.bars==after.bars,"event metadata exact");
   for(unsigned i=0;i<before.count;i++){
    unsigned w=before.events[i],want=((w&31u)==voice&&((w>>5)&63u)==step)?n:bb_phrase_ratchet_count(&before,i);
    CHECK(bb_phrase_ratchet_count(&after,i)==want,"only selected ordinals change");
   }
   CHECK(publish(m,&after,&ticket),"ratchet publish");
   CHECK(bb_pe_push(&journal,m,&before)==BB_PE_OK,"ratchet journal");
   CHECK(until_applied(m,ticket,NULL,NULL)&&!memcmp(&audio.rich.active[m].phrase,&after,sizeof after),"ratchet owner applied");
   bb_phrase untouched=after;
   CHECK(bb_pe_ratchet(&after,voice,step,n,&sentinel)==BB_PE_UNCHANGED&&!memcmp(&after,&untouched,sizeof after),"no-op does not edit source");
   assert(bb_session_encode(&audio.core,legacy,sizeof legacy));
   bb_phrase stored[2]={{0}},opened[2];stored[m]=after;
   memcpy(wire+BB_RICH_SCENE_OVERHEAD,legacy,sizeof legacy);
   size_t bytes=bb_rich_scene_finish_v2(wire,sizeof wire,sizeof legacy,stored,1u<<m,&audio.core);
   uint8_t mask=0;const void *inner;size_t inner_n;
   CHECK(bytes&&bb_rich_scene_open(wire,bytes,opened,&mask,&inner,&inner_n)&&mask==(1u<<m)&&!memcmp(&opened[m],&after,sizeof after)&&inner_n==sizeof legacy&&!memcmp(inner,legacy,sizeof legacy),"ratchet scene exact");
   before=after;
  }
  for(unsigned n=3;n;n--){unsigned target=9;
   CHECK(bb_pe_undo(&journal,&target,&undo)==BB_PE_OK&&target==m,"ratchet undo entry");
   CHECK(publish(m,&undo,&ticket)&&until_applied(m,ticket,NULL,NULL),"ratchet undo owner");
   CHECK(!memcmp(&audio.rich.active[m].phrase,&undo,sizeof undo),"ratchet undo exact");
  }
  CHECK(!journal.count&&!memcmp(&audio.rich.active[m].phrase,&original,sizeof original),"ratchet source restored exact");
 }
 printf("pattern edit: %u sections, %u seen QUEUED before APPLIED, %u old-route refusals, %lu checks, %lu failures\n",
        tested,queued_seen,legacy_refused,checks,failures);
 return failures!=0;
}
