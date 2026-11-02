/* SPDX-License-Identifier: GPL-3.0-only */
static void ratchet_model_cases(void){
 for(unsigned m=0;m<2;m++){
  build_song();uint32_t id=model.rows[1].section;
  const bb_arr_section before=*bb_arr_find(&model,id);
  CK(!bb_arr_source_drum(&sources,before.part[m],&rich));bb_phrase original=rich.phrase;
  unsigned voice=0,step=0,found=0;
  for(unsigned i=0;i<original.count;i++){unsigned w=original.events[i];if((w&31u)!=BB_FDV_ACCENT&&(w&31u)!=BB_FDV_ACC){voice=w&31u;step=(w>>5)&63u;found=1;break;}}
  CK(found);model_copy=model;sources_copy=sources;
  CK(bb_arr_source_ratchet(&model,&sources,id,model.revision-1u,m,voice,step,2,UINT32_MAX,&rich)==BB_SOURCE_REVISION);
  CK(!memcmp(&model,&model_copy,sizeof model)&&!memcmp(&sources,&sources_copy,sizeof sources));
  CK(bb_arr_source_ratchet(&model,&sources,id,model.revision,m,voice,step,5,UINT32_MAX,&rich)==BB_SOURCE_INVALID);
  CK(!memcmp(&model,&model_copy,sizeof model)&&!memcmp(&sources,&sources_copy,sizeof sources));
  CK(!bb_arr_source_ratchet(&model,&sources,id,model.revision,m,voice,step,2,UINT32_MAX,&rich));
  bb_arr_section edited=*bb_arr_find(&model,id);CK(edited.part[m].source==BB_ARR_WORKING);
  CK(!memcmp(&edited.part[1u-m],&before.part[1u-m],sizeof before.part[0]));
  CK(!memcmp(&edited.part[2],&before.part[2],sizeof before.part[2]));
  CK(!strcmp(edited.name,before.name)&&edited.duration==before.duration);
  CK(!memcmp(model.rows,model_copy.rows,sizeof model.rows));
  CK(!bb_arr_source_drum(&sources,edited.part[m],&rich));
  CK(!memcmp(rich.phrase.events,original.events,sizeof original.events));
  CK(!bb_arr_source_drum(&sources,before.part[m],&preparation.drum[m]));
  CK(!memcmp(&preparation.drum[m].phrase,&original,sizeof original));
  model_copy=model;sources_copy=sources;
  CK(bb_arr_source_ratchet(&model,&sources,id,model.revision,m,voice,step,2,UINT32_MAX,&rich)==BB_SOURCE_UNCHANGED);
  CK(!memcmp(&model,&model_copy,sizeof model)&&!memcmp(&sources,&sources_copy,sizeof sources));
  CK(!bb_arr_undo(&model));CK(!memcmp(&bb_arr_find(&model,id)->part[m],&before.part[m],sizeof before.part[m]));
  CK(!bb_arr_redo(&model));CK(!memcmp(&bb_arr_find(&model,id)->part[m],&edited.part[m],sizeof edited.part[m]));
  uint64_t protection=UINT64_C(0x8000000000000001);sources.work[edited.part[m].id-1u].protected_steps=protection;
  CK(!bb_arr_source_ratchet(&model,&sources,id,model.revision,m,voice,step,3,UINT32_MAX,&rich));
  const bb_arr_section *again=bb_arr_find(&model,id);
  CK(sources.work[again->part[m].id-1u].protected_steps==protection);
  CK(!memcmp(&sources.work[again->part[m].id-1u].origin,&edited.part[m],sizeof edited.part[m]));
 }
 /* 100 branches from curated source: discard Redo and reclaim its dead fork. */
 build_song();uint32_t id=model.rows[0].section;unsigned voice=0,step=0;
 CK(!bb_arr_source_drum(&sources,bb_arr_find(&model,id)->part[0],&rich));
 for(unsigned i=0;i<rich.phrase.count;i++){unsigned w=rich.phrase.events[i];if((w&31u)!=BB_FDV_ACCENT&&(w&31u)!=BB_FDV_ACC){voice=w&31u;step=(w>>5)&63u;break;}}
 for(unsigned cycle=0;cycle<100;cycle++){
  CK(!bb_arr_source_ratchet(&model,&sources,id,model.revision,0,voice,step,2+cycle%3,UINT32_MAX,&rich));
  CK(bb_arr_source_owners_valid(&sources));CK(sources.work_count==1);
  CK(!bb_arr_undo(&model));CK(bb_arr_find(&model,id)->part[0].source==BB_ARR_CURATED);
 }
 /* Successive immutable lineage fills the honest 16-part capacity. */
 build_song();id=model.rows[0].section;
 CK(!bb_arr_source_drum(&sources,bb_arr_find(&model,id)->part[0],&rich));
 for(unsigned i=0;i<rich.phrase.count;i++){unsigned w=rich.phrase.events[i];if((w&31u)!=BB_FDV_ACCENT&&(w&31u)!=BB_FDV_ACC){voice=w&31u;step=(w>>5)&63u;break;}}
 for(unsigned n=0;n<BB_ARR_WORK_PARTS;n++)CK(!bb_arr_source_ratchet(&model,&sources,id,model.revision,0,voice,step,2+n%3,UINT32_MAX,&rich));
 model_copy=model;sources_copy=sources;
 CK(bb_arr_source_ratchet(&model,&sources,id,model.revision,0,voice,step,2+BB_ARR_WORK_PARTS%3,UINT32_MAX,&rich)==BB_SOURCE_WORK_FULL);
 CK(!memcmp(&model,&model_copy,sizeof model)&&!memcmp(&sources,&sources_copy,sizeof sources));
 CK(bb_arr_source_ratchet(&model,&sources,id,model.revision,0,voice,step,4,0,&rich)==BB_SOURCE_CONTENT);
 CK(!memcmp(&model,&model_copy,sizeof model)&&!memcmp(&sources,&sources_copy,sizeof sources));
 printf("SONG ratchet model cases PASS %u checks; live owner/nav not connected\n",checks);
}
static bb_section_status edit_section;
static bb_phrase_view edit_view;
static unsigned edit_voice,edit_step;
static void start_edit_song(int same){
 build_song();
 if(same)CK(!bb_arr_row_replace(&model,1,(bb_arr_row){model.rows[0].section,1}));
 CK(!bb_arr_controller_start(&controller,&audio));drain();CK(!bb_arr_controller_poll(&controller,&audio));
 CK(bb_audio_section_status_read(&audio,&edit_section));CK(edit_section.running);
 memset(&edit_view,0,sizeof edit_view);CK(bb_audio_phrase_view_read(&audio,0,&edit_view));CK(edit_view.valid);
 unsigned found=0;
 for(unsigned i=0;i<edit_view.phrase.count;i++){unsigned w=edit_view.phrase.events[i];if((w&31u)!=BB_FDV_ACCENT&&(w&31u)!=BB_FDV_ACC){edit_voice=w&31u;edit_step=(w>>5)&63u;found=1;break;}}
 CK(found);CK(controller.ticket);CK(edit_section.phase!=BB_SECTION_FREE);
}
static void ratchet_owner_cases(void){
 for(unsigned same=0;same<2;same++){
  start_edit_song(same);uint32_t ticket=0,end=edit_section.end_tick;
  uint32_t gen0=audio.rich.player[0].generation,gen1=audio.rich.player[1].generation;
  uint32_t synth_ticket=audio.patterns.status[0].applied;
  bb_phrase other=audio.rich.active[1].phrase;
  CK(bb_audio_section_ratchet_submit(&audio,&edit_section,&edit_view,0,edit_voice,edit_step,4,&ticket));
  bb_section_ratchet status;CK(bb_audio_section_ratchet_read(&audio,&status)&&status.phase==BB_SECTION_EDIT_QUEUED);
  CK(!bb_audio_section_ratchet_ack(&audio,ticket));drain();
  CK(bb_audio_section_ratchet_read(&audio,&status)&&status.phase==BB_SECTION_EDIT_APPLIED);
  bb_section_status after;CK(bb_audio_section_status_read(&audio,&after));
  CK(after.running&&after.playing_section==edit_section.playing_section&&after.end_tick==end);
  CK(after.playing_project_revision==model.revision+1u);
  CK(audio.rich.player[0].generation==gen0&&audio.rich.player[1].generation==gen1);
  CK(audio.patterns.status[0].applied==synth_ticket&&!memcmp(&other,&audio.rich.active[1].phrase,sizeof other));
  for(unsigned i=0;i<edit_view.phrase.count;i++){
   unsigned w=edit_view.phrase.events[i];if((w&31u)==edit_voice&&((w>>5)&63u)==edit_step)CK(bb_phrase_ratchet_count(&audio.rich.active[0].phrase,i)==4);
  }
  /* Simulate coordinator's locked main-owned commit after matching APPLIED. */
  CK(!bb_arr_source_ratchet(&model,&sources,edit_section.playing_section,model.revision,0,edit_voice,edit_step,4,UINT32_MAX,&rich));
  controller.accepted_revision=model.revision;controller.prepared=0;
  CK(bb_audio_section_ratchet_ack(&audio,ticket));CK(!bb_arr_controller_poll(&controller,&audio));
  unsigned tries=0;while(controller.playing_row!=1&&tries++<2000){drain();CK(!bb_arr_controller_poll(&controller,&audio));}
  CK(tries<2000&&controller.armed&&audio.core.running);
  if(same)for(unsigned i=0;i<edit_view.phrase.count;i++){
   unsigned w=edit_view.phrase.events[i];if((w&31u)==edit_voice&&((w>>5)&63u)==edit_step)CK(bb_phrase_ratchet_count(&audio.rich.active[0].phrase,i)==4);
  }
 }
 /* STOP wins after queue acceptance; no musical/model edit occurs. */
 start_edit_song(0);uint32_t ticket;bb_phrase before=audio.rich.active[0].phrase;
 model_copy=model;sources_copy=sources;
 CK(bb_audio_section_ratchet_submit(&audio,&edit_section,&edit_view,0,edit_voice,edit_step,4,&ticket));
 push((bb_command){.type=BB_CMD_TRANSPORT,.value=0});drain();bb_section_ratchet status;
 CK(bb_audio_section_ratchet_read(&audio,&status)&&status.phase==BB_SECTION_EDIT_REJECTED);
 CK(!memcmp(&before,&audio.rich.active[0].phrase,sizeof before));
 CK(!memcmp(&model,&model_copy,sizeof model)&&!memcmp(&sources,&sources_copy,sizeof sources));
 CK(bb_audio_section_ratchet_ack(&audio,ticket));
 /* Fault terminates the pending transaction instead of stranding it. */
 start_edit_song(0);CK(bb_audio_section_ratchet_submit(&audio,&edit_section,&edit_view,0,edit_voice,edit_step,4,&ticket));
 (void)bb_audio_fault_drain(&audio);
 CK(bb_audio_section_ratchet_read(&audio,&status)&&status.phase==BB_SECTION_EDIT_REJECTED&&status.error==BB_SECTION_ERROR_FAULT);
 CK(bb_audio_section_ratchet_ack(&audio,ticket));
 printf("SONG ratchet owner cases PASS %u checks; coordinator/nav not connected\n",checks);
}
static void ratchet_coordinator_cases(void){
 for(unsigned same=0;same<2;same++)for(unsigned m=0;m<2;m++){
  start_edit_song(same);
  memset(&edit_view,0,sizeof edit_view);CK(bb_audio_phrase_view_read(&audio,m,&edit_view));
  unsigned found=0;for(unsigned i=0;i<edit_view.phrase.count;i++){unsigned w=edit_view.phrase.events[i];if((w&31u)!=BB_FDV_ACCENT&&(w&31u)!=BB_FDV_ACC){edit_voice=w&31u;edit_step=(w>>5)&63u;found=1;break;}}CK(found);
  uint32_t end=edit_section.end_tick,next=controller.ticket,revision=model.revision;
  model_copy=model;sources_copy=sources;
  CK(!bb_arr_controller_ratchet(&controller,&audio,&edit_view,m,edit_voice,edit_step,4));
  CK(controller.edit_ticket&&!bb_arr_controller_can_edit(&controller,&audio));
  CK(!bb_arr_controller_poll(&controller,&audio));CK(model.revision==revision);
  CK(!memcmp(&model,&model_copy,sizeof model)&&!memcmp(&sources,&sources_copy,sizeof sources));
  drain();CK(!bb_arr_controller_poll(&controller,&audio));
  CK(!controller.edit_ticket&&model.revision==revision+1u&&controller.accepted_revision==model.revision);
  CK(controller.ticket==next&&controller.armed&&audio.core.running);
  CK(bb_audio_section_status_read(&audio,&edit_section)&&edit_section.end_tick==end);
  const bb_arr_section *part=bb_arr_find(&model,model.rows[0].section);
  CK(part->part[m].source==BB_ARR_WORKING);CK(!bb_arr_source_drum(&sources,part->part[m],&rich));
  CK(!memcmp(&rich.phrase,&audio.rich.active[m].phrase,sizeof rich.phrase));
  unsigned tries=0;while(controller.playing_row!=1&&tries++<2000){drain();CK(!bb_arr_controller_poll(&controller,&audio));}
  CK(tries<2000&&controller.armed&&audio.core.running);
  if(same)CK(!memcmp(&rich.phrase,&audio.rich.active[m].phrase,sizeof rich.phrase));
 }
 start_edit_song(0);uint32_t revision=model.revision;
 CK(!bb_arr_controller_ratchet(&controller,&audio,&edit_view,0,edit_voice,edit_step,4));
 CK(!bb_arr_controller_stop(&controller,&audio));drain();
 CK(bb_arr_controller_poll(&controller,&audio)==BB_ARR_CTL_REJECTED);
 CK(!controller.edit_ticket&&model.revision==revision&&!audio.core.running);
 /* A selected live cue survives an edit of the currently playing section. */
 start_edit_song(0);CK(!bb_arr_controller_cue(&controller,&audio,3));drain();CK(!bb_arr_controller_poll(&controller,&audio));
 CK(controller.request_cue&&controller.requested_row==3);
 memset(&edit_view,0,sizeof edit_view);CK(bb_audio_phrase_view_read(&audio,0,&edit_view));
 CK(!bb_arr_controller_ratchet(&controller,&audio,&edit_view,0,edit_voice,edit_step,4));drain();CK(!bb_arr_controller_poll(&controller,&audio));
 unsigned tries=0;while(controller.playing_row!=3&&tries++<2000){drain();CK(!bb_arr_controller_poll(&controller,&audio));}
 CK(tries<2000&&controller.playing_cue&&controller.return_row==1);
 /* Queue congestion refuses before model mutation and leaves no pending edit. */
 start_edit_song(0);model_copy=model;sources_copy=sources;
 while(audio.commands.count<BB_COMMAND_CAPACITY)push((bb_command){.type=BB_CMD_TEMPO,.value=120});
 CK(bb_arr_controller_ratchet(&controller,&audio,&edit_view,0,edit_voice,edit_step,4)==BB_ARR_CTL_BUSY);
 CK(!controller.edit_ticket&&!memcmp(&model,&model_copy,sizeof model)&&!memcmp(&sources,&sources_copy,sizeof sources));
 bb_section_ratchet st;CK(bb_audio_section_ratchet_read(&audio,&st)&&st.phase==BB_SECTION_EDIT_IDLE);
 /* An unchanged count never consumes a model revision or owner token. */
 start_edit_song(0);model_copy=model;sources_copy=sources;
 unsigned old=bb_phrase_ratchet_count(&edit_view.phrase,0);
 CK(bb_arr_controller_ratchet(&controller,&audio,&edit_view,0,edit_voice,edit_step,old)==BB_ARR_CTL_UNCHANGED);
 CK(!controller.edit_ticket&&!memcmp(&model,&model_copy,sizeof model)&&!memcmp(&sources,&sources_copy,sizeof sources));
 /* An earlier external edit supersedes this SONG request: no model commit. */
 start_edit_song(0);model_copy=model;sources_copy=sources;
 CK(bb_audio_ratchet_request(&audio,0,edit_voice,edit_step,2));
 CK(!bb_arr_controller_ratchet(&controller,&audio,&edit_view,0,edit_voice,edit_step,4));drain();
 CK(bb_arr_controller_poll(&controller,&audio)==BB_ARR_CTL_REJECTED);
 CK(!controller.edit_ticket&&!memcmp(&model,&model_copy,sizeof model)&&!memcmp(&sources,&sources_copy,sizeof sources));
 printf("SONG ratchet coordinator cases PASS %u checks; nav/Undo gesture not connected\n",checks);
}
#include "bb_pattern_edit.h"
static void native_count_cases(void){
 unsigned changes=0,undos=0;
 for(unsigned m=0;m<2;m++)for(unsigned voice=0;voice<BB_VOICES;voice++){
  setup(0);drain();unsigned step=voice%16;
  audio.core.drums[m].steps[step].hits=(uint16_t)((1u<<voice)|(1u<<((voice+1)%BB_VOICES)));
  audio.core.drums[m].steps[step].accents=(uint16_t)(1u<<voice);
  bb_drum_step original=audio.core.drums[m].steps[step],other[BB_STEPS];
  memcpy(other,audio.core.drums[1u-m].steps,sizeof other);
  push((bb_command){.type=BB_CMD_TRANSPORT,.value=1});drain();
  bb_pattern_edit journal={0};bb_native_ratchet_view view;bb_section_ratchet status;
  for(unsigned count=2;count<=4;count++){
   CK(bb_audio_native_ratchet_view(&audio,m,voice,step,&view));uint32_t ticket;
   unsigned before=bb_ratchet_bits_get(view.packed,voice);
   CK(bb_audio_native_ratchet_submit(&audio,&view,count,&ticket));CK(!journal.count||journal.count==count-2u);
   drain();CK(bb_audio_section_ratchet_read(&audio,&status)&&status.phase==BB_SECTION_EDIT_APPLIED&&status.mode==2&&status.old_count==before);
   CK(bb_pe_push_count(&journal,m,view.source_ticket,view.epoch,voice,step,before,count)==BB_PE_OK);
   CK(bb_audio_section_ratchet_ack(&audio,ticket));
   CK(bb_ratchet_bits_get(audio.core.drums[m].steps[step].ratchets,voice)==count);
   CK(audio.core.drums[m].steps[step].hits==original.hits&&audio.core.drums[m].steps[step].accents==original.accents);
   CK(!memcmp(other,audio.core.drums[1u-m].steps,sizeof other));changes++;
  }
  /* Old phrase-only consumer must not accidentally pop a native entry. */
  bb_phrase unused;unsigned machine=9;unsigned saved_count=journal.count;
  CK(bb_pe_undo(&journal,&machine,&unused)==BB_PE_CHANGED&&journal.count==saved_count);
  while(journal.count){bb_pe_entry entry=journal.history[journal.count-1u];
   CK(entry.kind==BB_PE_ENTRY_NATIVE_COUNT&&entry.machine==m);
   CK(bb_audio_native_ratchet_view(&audio,m,voice,step,&view));
   CK(view.source_ticket==entry.source_ticket&&view.epoch==entry.source_epoch&&bb_ratchet_bits_get(view.packed,voice)==entry.after_count);
   uint32_t ticket;CK(bb_audio_native_ratchet_submit(&audio,&view,entry.before_count,&ticket));drain();
   CK(bb_audio_section_ratchet_read(&audio,&status)&&status.phase==BB_SECTION_EDIT_APPLIED);
   journal.count--;CK(bb_audio_section_ratchet_ack(&audio,ticket));undos++;
  }
  CK(!memcmp(&audio.core.drums[m].steps[step],&original,sizeof original));
 }
 /* A pattern installed before the count command makes its snapshot stale. */
 setup(0);drain();audio.core.drums[0].steps[0].hits=1;
 bb_native_ratchet_view view;CK(bb_audio_native_ratchet_view(&audio,0,0,0,&view));
 bb_drum_pattern_data replacement={0};replacement.length=16;replacement.steps[0].hits=1;
 uint32_t pattern,ticket;CK(bb_audio_drum_pattern_submit(&audio,0,&replacement,16,&pattern));
 CK(bb_audio_native_ratchet_submit(&audio,&view,4,&ticket));drain();bb_section_ratchet status;
 CK(bb_audio_section_ratchet_read(&audio,&status)&&status.phase==BB_SECTION_EDIT_REJECTED);
 CK(audio.core.drums[0].steps[0].ratchets==0);CK(bb_audio_section_ratchet_ack(&audio,ticket));
 printf("NATIVE count owner changes=%u Undo=%u checks=%u; production journal not connected\n",changes,undos,checks);
}
static void plain_rich_count_cases(void){
 for(unsigned m=0;m<2;m++){
  setup(0);drain();CK(submit(m,0));drain();push((bb_command){.type=BB_CMD_TRANSPORT,.value=1});drain();
  bb_phrase_view view={0};bb_section_status section;CK(bb_audio_phrase_view_read(&audio,m,&view));CK(bb_audio_section_status_read(&audio,&section));
  CK(!section.playing_section);unsigned voice=0,step=0;
  for(unsigned i=0;i<view.phrase.count;i++){unsigned w=view.phrase.events[i];if((w&31u)!=BB_FDV_ACCENT&&(w&31u)!=BB_FDV_ACC){voice=w&31u;step=(w>>5)&63u;break;}}
  uint32_t ticket,generation=audio.rich.player[m].generation;
  CK(bb_audio_section_ratchet_submit(&audio,&section,&view,m,voice,step,4,&ticket));drain();
  bb_section_ratchet status;CK(bb_audio_section_ratchet_read(&audio,&status)&&status.phase==BB_SECTION_EDIT_APPLIED&&status.mode==1);
  CK(!audio.section.status.playing_section&&!audio.section.status.playing_project_revision);
  CK(audio.rich.player[m].generation==generation);CK(bb_audio_section_ratchet_ack(&audio,ticket));
  bb_pattern_edit journal={0};CK(bb_pe_push(&journal,m,&view.phrase)==BB_PE_OK);
  bb_phrase original;unsigned target;CK(bb_pe_undo(&journal,&target,&original)==BB_PE_OK&&target==m);
  CK(bb_phrase_index_prepare(&original,UINT32_MAX,&rich));CK(bb_audio_phrase_submit_seamless(&audio,m,&rich,&ticket));
  unsigned tries=0;while(audio.rich.status[m].applied!=ticket&&tries++<6000)drain();CK(tries<6000);
  CK(!memcmp(&audio.rich.active[m].phrase,&view.phrase,sizeof view.phrase));
 }
 printf("PLAIN rich count/Undo checks=%u; production journal not connected\n",checks);
}
static bb_phrase mixed_original;
static uint32_t mixed_section,mixed_before_revision;
static void mixed_song_start(unsigned m,unsigned future){
 build_song();mixed_section=model.rows[0].section;
 bb_arr_section part=*bb_arr_find(&model,mixed_section);bb_arr_part fork;
 CK(!bb_arr_source_fork(&sources,part.part[m],BB_SOURCE_DRUM,&fork));
 bb_arr_work_part *w=&sources.work[fork.id-1u];bb_phrase phrase=w->data.drum.phrase;
 unsigned first=0;while(first<phrase.count&&((phrase.events[first]&31u)==BB_FDV_ACCENT||(phrase.events[first]&31u)==BB_FDV_ACC))first++;
 CK(first<phrase.count&&phrase.count<BB_FACTORY_DRUM_EVENTS);
 edit_voice=phrase.events[first]&31u;edit_step=(phrase.events[first]>>5)&63u;
 unsigned duplicate=phrase.count++;phrase.events[duplicate]=phrase.events[first];
 unsigned shift=2u*(first&3u);phrase.ratchets[first>>2]=(uint8_t)((phrase.ratchets[first>>2]&~(3u<<shift))|(1u<<shift));
 shift=2u*(duplicate&3u);phrase.ratchets[duplicate>>2]=(uint8_t)((phrase.ratchets[duplicate>>2]&~(3u<<shift))|(2u<<shift));
 CK(bb_phrase_index_prepare(&phrase,UINT32_MAX,&w->data.drum));mixed_original=phrase;
 part.part[m]=fork;CK(!bb_arr_section_put(&model,&part,NULL));CK(!bb_arr_source_release(&sources,fork));
 if(future==1)CK(!bb_arr_row_replace(&model,1,(bb_arr_row){mixed_section,1}));
 if(future==2)CK(!bb_arr_row_replace(&model,2,(bb_arr_row){mixed_section,1}));
 mixed_before_revision=bb_arr_find(&model,mixed_section)->revision;
 CK(!bb_arr_controller_start(&controller,&audio));drain();CK(!bb_arr_controller_poll(&controller,&audio));
 memset(&edit_view,0,sizeof edit_view);CK(bb_audio_phrase_view_read(&audio,m,&edit_view));
 CK(!memcmp(&edit_view.phrase,&mixed_original,sizeof mixed_original));
 CK(!bb_arr_controller_ratchet(&controller,&audio,&edit_view,m,edit_voice,edit_step,4));drain();CK(!bb_arr_controller_poll(&controller,&audio));
 CK(controller.edit_completed&&controller.edit_result==BB_ARR_CTL_OK);
}
static void song_exact_undo_cases(void){
 for(unsigned m=0;m<2;m++)for(unsigned context=0;context<3;context++){
  mixed_song_start(m,context==0?1:context==1?2:0);
  uint32_t token=bb_arr_find(&model,mixed_section)->revision;
  if(context){unsigned tries=0;while(controller.playing_row!=1&&tries++<2000){drain();CK(!bb_arr_controller_poll(&controller,&audio));}CK(tries<2000);}
  CK(bb_audio_section_status_read(&audio,&edit_section));uint32_t end=edit_section.end_tick,gen[2]={audio.rich.player[0].generation,audio.rich.player[1].generation};
  bb_phrase current=audio.rich.active[m].phrase,other=audio.rich.active[1u-m].phrase;
  unsigned cursor=model.cursor;uint32_t source_next=sources.next_revision;
  memset(&edit_view,0,sizeof edit_view);CK(bb_audio_phrase_view_read(&audio,m,&edit_view));
  model_copy=model;sources_copy=sources;
  CK(bb_arr_controller_ratchet_undo(&controller,&audio,&edit_view,m,mixed_section,token+1u)==BB_ARR_CTL_CHANGED);
  CK(!memcmp(&model,&model_copy,sizeof model)&&!memcmp(&sources,&sources_copy,sizeof sources));
  CK(!bb_arr_controller_ratchet_undo(&controller,&audio,&edit_view,m,mixed_section,token));
  CK(model.cursor==cursor&&!memcmp(&model,&model_copy,sizeof model));
  drain();CK(!bb_arr_controller_poll(&controller,&audio));CK(!controller.edit_ticket&&controller.edit_result==BB_ARR_CTL_OK);
  CK(model.cursor==cursor-1u&&sources.next_revision==source_next);CK(bb_arr_find(&model,mixed_section)->revision==mixed_before_revision);
  CK(bb_arr_source_owners_valid(&sources));CK(bb_audio_section_status_read(&audio,&edit_section)&&edit_section.end_tick==end);
  CK(audio.rich.player[0].generation==gen[0]&&audio.rich.player[1].generation==gen[1]);
  CK(!memcmp(&other,&audio.rich.active[1u-m].phrase,sizeof other));
  CK(!bb_arr_source_drum(&sources,bb_arr_find(&model,mixed_section)->part[m],&rich));CK(!memcmp(&rich.phrase,&mixed_original,sizeof mixed_original));
  if(!context)CK(!memcmp(&audio.rich.active[m].phrase,&mixed_original,sizeof mixed_original));
  else CK(!memcmp(&audio.rich.active[m].phrase,&current,sizeof current));
  if(context==1){unsigned tries=0;while(controller.playing_row!=2&&tries++<2000){drain();CK(!bb_arr_controller_poll(&controller,&audio));}CK(tries<2000);CK(!memcmp(&audio.rich.active[m].phrase,&mixed_original,sizeof mixed_original));}
 }
 /* STOP after acceptance: no model Undo, no flattening of the current counts. */
 mixed_song_start(0,1);uint32_t token=bb_arr_find(&model,mixed_section)->revision;
 memset(&edit_view,0,sizeof edit_view);CK(bb_audio_phrase_view_read(&audio,0,&edit_view));model_copy=model;sources_copy=sources;
 CK(!bb_arr_controller_ratchet_undo(&controller,&audio,&edit_view,0,mixed_section,token));CK(!bb_arr_controller_stop(&controller,&audio));drain();
 CK(bb_arr_controller_poll(&controller,&audio)==BB_ARR_CTL_REJECTED);CK(!memcmp(&model,&model_copy,sizeof model)&&!memcmp(&sources,&sources_copy,sizeof sources));
 printf("SONG exact-count Undo backend PASS cumulative %u checks; eight-deep STEPS integration remains required\n",checks);
}
int main(void){collect();out=fopen("/dev/null","wb");CK(out);ratchet_model_cases();ratchet_owner_cases();ratchet_coordinator_cases();native_count_cases();plain_rich_count_cases();song_exact_undo_cases();fclose(out);return 0;}
