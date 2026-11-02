/* SPDX-License-Identifier: GPL-3.0-only */
#define main oracle_main
#include ORACLE_C
#undef main
static unsigned checks;
#define CK(x) do{checks++;if(!(x)){fprintf(stderr,"FAIL line%u %s\n",__LINE__,#x);exit(1);}}while(0)
static bb_section_bundle bundle;
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
 CK(sections==539);
}
static void make(unsigned sec){
 memset(&bundle,0,sizeof bundle);bundle.section=sec+1;bundle.revision=2;bundle.project_revision=3;
 bundle.drum[0]=catalogue[sec];bundle.drum[1]=catalogue[(sec+17)%sections];
 if(bb_factory_prepare_synth_data(&bb_factory_catalogue,&choices[sec],0,&bundle.dillo)!=BB_FACTORY_PATTERN_OK)
  bundle.dillo.length=16;
}
static unsigned current_section;
static void drain(void){int rc=render();if(!rc)fprintf(stderr,"render failure section=%u ticks=%u frame=%llu richerrors=%u worker=%u groupPhase=%u result=%u error=%u\n",current_section,audio.core.ticks,(unsigned long long)audio.core.frame,audio.rich.render_errors,bb_c1_live.failed,audio.section.phase,audio.section.status.state.result,audio.section.status.error);CK(rc);}
static void prepared(unsigned sec){setup(sec);drain();make(sec);}
static void applied(uint32_t t){
 CK(audio.section.status.state.applied==t);CK(audio.section.status.state.result==BB_PATTERN_APPLIED);
 CK(audio.section.phase==BB_SECTION_FREE);CK(audio.rich.valid[0]&&audio.rich.valid[1]);
 for(unsigned m=0;m<2;m++){
  CK(!memcmp(&audio.rich.active[m],&bundle.drum[m],sizeof bundle.drum[m]));
  CK(audio.rich.status[m].applied==t);CK(audio.rich.status[m].applied_frame==audio.section.status.state.applied_frame);
 }
 CK(!memcmp(audio.core.synths[0].notes,bundle.dillo.notes,sizeof bundle.dillo.notes));
 CK(!memcmp(audio.patterns.expression[0],bundle.dillo.expression,sizeof bundle.dillo.expression));
 CK(audio.core.synths[0].length==bundle.dillo.length);CK(audio.patterns.status[0].applied==t);
 CK(audio.patterns.status[0].applied_frame==audio.section.status.state.applied_frame);
 for(unsigned i=0;i<BB_PATTERN_SLOTS;i++){CK(audio.rich.inbox[i].state==BB_PATTERN_SLOT_FREE);CK(audio.patterns.inbox[i].state==BB_PATTERN_SLOT_FREE);}
 CK(!audio.rich.render_errors);
}

static bb_phrase_index old_drums[2];static bb_pattern_data old_synth;
static void remember(void){
 for(unsigned m=0;m<2;m++)old_drums[m]=audio.rich.active[m];
 memcpy(old_synth.notes,audio.core.synths[0].notes,sizeof old_synth.notes);
 memcpy(old_synth.expression,audio.patterns.expression[0],sizeof old_synth.expression);
 old_synth.length=audio.core.synths[0].length;
}
static void unchanged(void){
 for(unsigned m=0;m<2;m++)CK(!memcmp(&old_drums[m],&audio.rich.active[m],sizeof old_drums[m]));
 CK(!memcmp(old_synth.notes,audio.core.synths[0].notes,sizeof old_synth.notes));
 CK(!memcmp(old_synth.expression,audio.patterns.expression[0],sizeof old_synth.expression));
 CK(old_synth.length==audio.core.synths[0].length);
}
static void base(void){
 prepared(3);uint32_t t;CK(bb_audio_section_submit(&audio,&bundle,&t));drain();applied(t);
 make(4);remember();
}
static void assert_free(void){
 CK(audio.section.phase==BB_SECTION_FREE);
 for(unsigned i=0;i<BB_PATTERN_SLOTS;i++){CK(audio.rich.inbox[i].state==BB_PATTERN_SLOT_FREE);CK(audio.patterns.inbox[i].state==BB_PATTERN_SLOT_FREE);}
}
static void negative(void){
 uint32_t t=0;bb_section_bundle good;
 base();good=bundle;bundle.drum[1].phrase.length=0;CK(!bb_audio_section_submit(&audio,&bundle,&t));unchanged();assert_free();
 bundle=good;bundle.dillo.length=0;CK(!bb_audio_section_submit(&audio,&bundle,&t));unchanged();assert_free();
 bundle=good;CK(!bb_audio_section_submit(&audio,&bundle,&bundle.section));unchanged();
 CK(!bb_audio_section_submit(&audio,(const bb_section_bundle *)&audio,NULL));unchanged();
 bundle.wait_ticks=4096;CK(!bb_audio_section_submit(&audio,&bundle,&t));unchanged();
 bundle=good;audio.patterns.next_ticket=UINT32_MAX;CK(!bb_audio_section_submit(&audio,&bundle,&t));unchanged();
 base();audio.commands.stop_pending=1;CK(!bb_audio_section_submit(&audio,&bundle,&t));unchanged();
 base();while(audio.commands.count<BB_COMMAND_CAPACITY)push((bb_command){.type=BB_CMD_TEMPO,.value=120});
 CK(!bb_audio_section_submit(&audio,&bundle,&t));unchanged();assert_free();drain();unchanged();
 base();audio.rich.inbox[0].state=BB_PATTERN_SLOT_WRITING;CK(!bb_audio_section_submit(&audio,&bundle,&t));unchanged();
 base();CK(bb_audio_section_submit(&audio,&bundle,&t));CK(!bb_audio_section_submit(&audio,&bundle,NULL));
 audio.rich.inbox[audio.section.rich_slot[1]].data.phrase.length=0;drain();unchanged();assert_free();CK(audio.section.status.error==BB_SECTION_ERROR_DATA);
 base();CK(bb_audio_section_submit(&audio,&bundle,&t));audio.rich.player[1].generation++;drain();unchanged();assert_free();
 CK(audio.section.status.error==BB_SECTION_ERROR_STALE);
 base();CK(bb_audio_section_submit(&audio,&bundle,&t));audio.drum_patterns.family_epoch[1]++;drain();unchanged();assert_free();
 base();CK(bb_audio_section_submit(&audio,&bundle,&t));audio.rich.player[1].generation=UINT32_MAX;
 audio.section.generation[1]=UINT32_MAX;drain();unchanged();assert_free();
 base();CK(bb_audio_section_submit(&audio,&bundle,&t));audio.rich.player[1].queue.count=BB_DELAYED_CAPACITY+1;
 bb_command bad_queue={0};bad_queue.type=BB_CMD_SECTION_PATTERN;bad_queue.value=(uint16_t)t;bad_queue.voice=(uint8_t)(t>>16);bad_queue.note.note=(uint8_t)(t>>24);
 bb_audio_section_command(&audio,&bad_queue,NULL,NULL);unchanged();assert_free();
 base();CK(bb_audio_section_submit(&audio,&bundle,&t));push((bb_command){.type=BB_CMD_TRANSPORT});drain();unchanged();assert_free();
 CK(!audio.core.running&&audio.section.status.state.result==BB_PATTERN_CANCELLED);
 base();CK(bb_audio_section_submit(&audio,&bundle,&t));
 CK(bb_audio_section_cancel_submit(&audio,t));
 /* Cancellation after an accepted stopped load cannot undo an applied load. */
 drain();applied(t);
 base();CK(bb_audio_section_submit(&audio,&bundle,&t));
 push((bb_command){.type=BB_CMD_SYNTH_PATTERN,.value=(uint16_t)t,.voice=(uint8_t)(t>>16),.note={.note=(uint8_t)(t>>24)}});
 drain();applied(t); /* raw token cannot consume SECTION_SLOT storage */
 base();CK(bb_audio_section_submit(&audio,&bundle,&t));
 CK(bb_audio_fault_drain(&audio)>0);unchanged();assert_free();CK(audio.section.status.error==BB_SECTION_ERROR_FAULT);
}
static void running(void){
 for(unsigned sec=0;sec<sections;sec++){
  current_section=sec;prepared(sec);uint32_t t;CK(bb_audio_section_submit(&audio,&bundle,&t));drain();
  push((bb_command){.type=BB_CMD_TRANSPORT_RESTART});drain();
  make((sec+1)%sections);bundle.wait_ticks=2;remember();uint32_t before=audio.core.ticks;
  CK(bb_audio_section_submit(&audio,&bundle,&t));uint32_t target=audio.section.due_tick;CK(target==before+2);
  unsigned n=0;
  while(audio.section.status.state.applied!=t&&n++<1500){
   drain();if(audio.section.status.state.applied!=t){unchanged();CK(audio.section.phase!=BB_SECTION_FREE);}
  }
  CK(audio.section.status.state.applied==t);applied(t);CK(audio.core.running);
  CK(audio.core.ticks==target+1); /* callback applied immediately before target tick */
  CK(!audio.section.status.error);push((bb_command){.type=BB_CMD_TRANSPORT});drain();
 }
}
static unsigned interrupt_mode;
static void interrupted_leave(void *ctx,uintptr_t token_value){
 leave_fn(ctx,token_value);
 if(interrupt_mode&&audio.section.phase==BB_SECTION_WRITING){interrupt_mode=0;bb_audio_section_cancel_owned(&audio,0);}
}
static void interleavings(void){
 base();audio.commands.leave=interrupted_leave;interrupt_mode=1;
 CK(!bb_audio_section_submit(&audio,&bundle,NULL));unchanged();assert_free();CK(!interrupt_mode);
 base();push((bb_command){.type=BB_CMD_TRANSPORT_RESTART});drain();make(7);bundle.wait_ticks=8;remember();
 uint32_t t;CK(bb_audio_section_submit(&audio,&bundle,&t));drain();unchanged();
 CK(bb_audio_section_cancel_submit(&audio,t+1));drain();unchanged();CK(audio.section.phase==BB_SECTION_PENDING);
 CK(bb_audio_section_cancel_submit(&audio,t));drain();unchanged();assert_free();
 base();push((bb_command){.type=BB_CMD_TRANSPORT_RESTART});drain();make(8);bundle.wait_ticks=2;remember();
 CK(bb_audio_section_submit(&audio,&bundle,&t));audio.core.ticks=audio.section.due_tick+1;
 audio.core.pending_tick=1;drain();unchanged();assert_free();CK(audio.section.status.error==BB_SECTION_ERROR_LATE);
 base();audio.core.ticks=UINT32_MAX-1;push((bb_command){.type=BB_CMD_TRANSPORT_RESUME});drain();
 make(9);bundle.wait_ticks=2;CK(bb_audio_section_submit(&audio,&bundle,&t));
 unsigned n=0;while(audio.section.status.state.applied!=t&&n++<1500)drain();applied(t);CK(!audio.section.status.error);
}

static float tail_reference[64][2][128];
static void tail_case(void){
 double nonzero=0;
 for(unsigned branch=0;branch<2;branch++){
  prepared(3);uint32_t t;CK(bb_audio_section_submit(&audio,&bundle,&t));drain();
  push((bb_command){.type=BB_CMD_TRANSPORT_RESTART});
  for(unsigned i=0;i<128;i++)drain();
  push((bb_command){.type=BB_CMD_TRANSPORT});drain();
  if(branch){
   make(61);CK(bb_audio_section_submit(&audio,&bundle,&t));
   /* Intervening live sound/mix settings must remain owner state. */
  }
  for(unsigned i=0;i<64;i++){
   drain();if(!branch){
    memcpy(tail_reference[i][0],audio.left,sizeof audio.left);
    memcpy(tail_reference[i][1],audio.right,sizeof audio.right);
    for(unsigned j=0;j<128;j++)nonzero+=(double)audio.left[j]*audio.left[j]+(double)audio.right[j]*audio.right[j];
   }else{
    CK(!memcmp(tail_reference[i][0],audio.left,sizeof audio.left));
    CK(!memcmp(tail_reference[i][1],audio.right,sizeof audio.right));
   }
  }
  if(branch)applied(t);
 }
 CK(nonzero>1e-10);
}
static void live_settings(void){
 base();uint32_t t;CK(bb_audio_section_submit(&audio,&bundle,&t));
 push((bb_command){.type=BB_CMD_GROUP_LEVEL,.target=1,.voice=0,.value=39});
 push((bb_command){.type=BB_CMD_FX_PARAMETER,.voice=BB_FX_ECHO_WET,.value=68});
 drain();applied(t);CK(audio.core.effects.value[BB_FX_ECHO_WET]==68);
 /* The exact group level is curve state: compare to the accepted target. */
 CK(audio.groups[1].fader[0].level==39);
 push((bb_command){.type=BB_CMD_NOTE,.target=0,.step=0,.note={.note=62,.gate=1}});drain();
 CK(!audio.section.status.playing_section&&audio.core.synths[0].notes[0].note==62);
}
int main(void){collect();out=fopen("/dev/null","wb");CK(out);
 for(unsigned sec=0;sec<sections;sec++){
  prepared(sec);uint32_t t=0;CK(bb_audio_section_submit(&audio,&bundle,&t));CK(t);
  drain();applied(t);CK(!audio.core.running);
 }
 fprintf(stderr,"negative cases\n");negative();fprintf(stderr,"running cases\n");running();fprintf(stderr,"interleavings\n");interleavings();fprintf(stderr,"tails/live settings\n");tail_case();live_settings();
 bb_p1b_report report;bb_audio_p1b_report(&report);CK(!report.guard_trips);
 printf("PASS %u checks across539 stopped +539 running rich A/B/Dillo section transitions and rejection/interleaving cases\n",checks);fclose(out);return 0;
}
