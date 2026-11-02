/* SPDX-License-Identifier: GPL-3.0-only */
#define main oracle_main
#include ORACLE_C
#undef main
#include "bb_arr_controller.h"
static unsigned checks;
#define CK(x) do{checks++;if(!(x)){fprintf(stderr,"FAIL line%u %s\n",__LINE__,#x);exit(1);}}while(0)
static bb_arrangement model,model_copy;
static bb_arr_sources sources,sources_copy;
static bb_arr_controller controller;
static bb_section_bundle preparation;
static bb_phrase_index rich;
static bb_pattern_data synth;
static void collect(void){
 CK(bb_factory_init());sections=0;
 for(unsigned group=0;group<2;group++){
  bb_browser b;CK(bb_browser_init(&b,&bb_factory_catalogue));CK(bb_browser_group(&b,group));
  for(unsigned bank=0;bank<bb_browser_bank_count(&b);bank++){
   b.bank_index=bank;b.part_index=0;
   for(unsigned part=0;part<bb_browser_part_count(&b);part++){
    b.part_index=part;CK(sections<MAXSEC);CK(bb_browser_current(&b,&choices[sections]));
    CK(bb_factory_prepare_rich_drum(&bb_factory_catalogue,&choices[sections],&catalogue[sections])==BB_FDRUM_OK);sections++;
   }
  }
 }CK(sections==539);
}
static void drain(void){CK(render());}
static void ref_set(unsigned sec,bb_arr_section *part){
 CK(!bb_arr_source_bind(&sources,&choices[sec],BB_SOURCE_DRUM,&part->part[0]));
 CK(!bb_arr_source_bind(&sources,&choices[(sec+17)%sections],BB_SOURCE_DRUM,&part->part[1]));
 int ok=bb_factory_prepare_synth_data(&bb_factory_catalogue,&choices[sec],0,&synth);
 CK(ok==BB_FACTORY_PATTERN_OK); /* no silent missing/unsupported conversion */
 CK(!bb_arr_source_bind(&sources,&choices[sec],BB_SOURCE_DILLO,&part->part[2]));
}
static void source_cases(void){
 for(unsigned sec=0;sec<sections;sec++){
  CK(!bb_arr_sources_init(&sources,&bb_factory_catalogue));bb_arr_init(&model);
  bb_arr_section part={0};strcpy(part.name,"SOURCE SECTION");part.duration=24;ref_set(sec,&part);
  uint32_t id;CK(!bb_arr_section_put(&model,&part,&id));CK(sources.count==3);
  CK(!bb_arr_prepare_section(&model,&sources,id,2,&preparation));CK(preparation.duration_ticks==48);
  CK(!memcmp(&preparation.drum[0],&catalogue[sec],sizeof rich));
  CK(!memcmp(&preparation.drum[1],&catalogue[(sec+17)%sections],sizeof rich));
  CK(!memcmp(&preparation.dillo,&synth,sizeof synth));
  bb_arr_part fork;CK(!bb_arr_source_fork(&sources,part.part[0],BB_SOURCE_DRUM,&fork));
  CK(fork.source==BB_ARR_WORKING);CK(!bb_arr_source_drum(&sources,fork,&rich));
  CK(!memcmp(&rich,&catalogue[sec],sizeof rich));
  sources.work[0].protected_steps=UINT64_C(0x8000000000000001);
  bb_arr_part fork2;CK(!bb_arr_source_fork(&sources,fork,BB_SOURCE_DRUM,&fork2));
  CK(sources.work[1].protected_steps==sources.work[0].protected_steps);
  CK(sources.work[1].origin.id==fork.id&&sources.work[1].origin.source==BB_ARR_WORKING);
  bb_factory_choice resolved;CK(!bb_arr_source_choice(&sources,part.part[0],BB_SOURCE_DRUM,&resolved));
  CK(resolved.bank==choices[sec].bank&&resolved.part==choices[sec].part);
  part.part[0].revision^=1;CK(bb_arr_source_drum(&sources,part.part[0],&rich)==BB_SOURCE_REVISION);
  CK(bb_arr_source_drum(&sources,part.part[2],&rich)==BB_SOURCE_KIND);
 }
}
static void build_song(void){
 CK(!bb_arr_sources_init(&sources,&bb_factory_catalogue));bb_arr_init(&model);
 const char *names[]={"INTRO","VERSE","CHORUS","BREAK","ENDING"};uint32_t id[5];
 for(unsigned i=0;i<5;i++){
  bb_arr_section part={0};strcpy(part.name,names[i]);part.duration=i>=3?8:16;
  ref_set(i,&part);CK(!bb_arr_section_put(&model,&part,&id[i]));
 }
 unsigned order[]={0,1,2,3,2,4},repeat[]={1,2,1,1,1,1};
 for(unsigned i=0;i<6;i++)CK(!bb_arr_row_insert(&model,i,(bb_arr_row){id[order[i]],repeat[i]}));
 setup(0);drain();CK(!bb_arr_controller_init(&controller,&model,&sources));
}
static void play_song(int cue,int pause){
 build_song();model_copy=model;CK(!bb_arr_controller_start(&controller,&audio));
 unsigned observed[16],count=0,blocks=0;uint32_t seen=0;int did_cue=0,did_pause=0;
 while(blocks++<20000){
  drain();CK(!bb_arr_controller_poll(&controller,&audio));
  if(controller.seen_applied&&controller.seen_applied!=seen){
   seen=controller.seen_applied;CK(count<16);observed[count++]=controller.playing_row;
  }
  if(cue&&!did_cue&&audio.core.ticks>=4&&controller.playing_row==0){
   CK(!bb_arr_controller_cue(&controller,&audio,3));did_cue=1;
  }
  if(pause&&!did_pause&&audio.core.ticks>=4){
   CK(!bb_arr_controller_stop(&controller,&audio));drain();CK(!bb_arr_controller_poll(&controller,&audio));
   uint32_t tick=audio.core.ticks;uint64_t frame=audio.core.frame;
   for(unsigned i=0;i<20;i++){drain();CK(!bb_arr_controller_poll(&controller,&audio));}
   CK(!audio.core.running&&audio.core.ticks==tick&&audio.core.frame==frame);
   push((bb_command){.type=BB_CMD_TRANSPORT_RESUME});did_pause=1;
  }
  if(controller.view.ended&&!audio.core.running)break;
 }
 CK(blocks<20000);CK(!audio.core.running&&controller.view.ended);CK(!audio.rich.render_errors);
 const unsigned normal[]={0,1,2,3,4,5},cued[]={0,3,1,2,3,4,5};
 const unsigned *wanted=cue?cued:normal;unsigned n=cue?7:6;CK(count==n);
 for(unsigned i=0;i<n;i++)CK(observed[i]==wanted[i]);
 CK(audio.core.ticks==(cue?104u:96u));CK(!memcmp(&model,&model_copy,sizeof model));
 uint64_t frame=audio.core.frame;for(unsigned i=0;i<20;i++)drain();CK(audio.core.frame==frame);
 if(!cue&&!pause)CK(frame==529200u);
}
static void limits(void){
 CK(!bb_arr_sources_init(&sources,&bb_factory_catalogue));bb_arr_part ref,last;
 for(unsigned i=0;i<BB_ARR_SOURCE_REFS;i++)CK(!bb_arr_source_bind(&sources,&choices[i],BB_SOURCE_DRUM,&ref));
 sources_copy=sources;last=ref;
 CK(bb_arr_source_bind(&sources,&choices[BB_ARR_SOURCE_REFS],BB_SOURCE_DRUM,&ref)==BB_SOURCE_FULL);
 CK(!memcmp(&sources,&sources_copy,sizeof sources));CK(!memcmp(&ref,&last,sizeof ref));
 bb_factory_choice wrong;memset(&wrong,0,sizeof wrong);sources.entry[0].part[0]^=1;
 CK(bb_arr_source_choice(&sources,(bb_arr_part){1,sources.catalogue_crc,BB_ARR_CURATED},BB_SOURCE_DRUM,&wrong)==BB_SOURCE_MISSING);
 CK(!bb_arr_sources_init(&sources,&bb_factory_catalogue));CK(!bb_arr_source_bind(&sources,&choices[0],BB_SOURCE_DRUM,&ref));
 for(unsigned i=0;i<BB_ARR_WORK_PARTS;i++)CK(!bb_arr_source_fork(&sources,ref,BB_SOURCE_DRUM,&last));
 sources_copy=sources;
 CK(bb_arr_source_fork(&sources,ref,BB_SOURCE_DRUM,&last)==BB_SOURCE_WORK_FULL);
 CK(!memcmp(&sources,&sources_copy,sizeof sources));
}
static void restart_and_loop(void){
 build_song();CK(!bb_arr_controller_load(&controller,&audio,3));drain();CK(!bb_arr_controller_poll(&controller,&audio));
 CK(!audio.core.running&&controller.playing_row==3);CK(!controller.armed);
 CK(!bb_arr_controller_start(&controller,&audio));
 for(unsigned i=0;i<100;i++){drain();CK(!bb_arr_controller_poll(&controller,&audio));}
 CK(controller.playing_row==0&&controller.ticket);
 CK(!bb_arr_controller_start(&controller,&audio));CK(controller.restart_wait);
 for(unsigned i=0;i<10;i++){drain();CK(!bb_arr_controller_poll(&controller,&audio));}
 CK(controller.playing_row==0&&!controller.restart_wait&&audio.core.ticks<4);
 CK(controller.view.transport_epoch>=2);
 build_song();CK(!bb_arr_controller_set_loop(&controller,&audio,1));CK(!bb_arr_controller_start(&controller,&audio));
 unsigned n=0;while(controller.playing_row!=5&&n++<20000){drain();CK(!bb_arr_controller_poll(&controller,&audio));}
 CK(n<20000&&controller.ticket&&controller.requested_row==0);
 CK(!bb_arr_controller_set_loop(&controller,&audio,0));
 while(audio.core.running&&n++<20000){drain();CK(!bb_arr_controller_poll(&controller,&audio));}
 CK(!audio.core.running&&audio.core.ticks==96&&controller.view.ended);
}
int main(void){collect();out=fopen("/dev/null","wb");CK(out);source_cases();play_song(0,0);play_song(1,0);play_song(0,1);limits();restart_and_loop();
 printf("PASS %u checks:539 exact source mappings/forks; finite named arrangement, one-shot cue/return and pause/Continue use the real C owner\n",checks);fclose(out);return 0;}
