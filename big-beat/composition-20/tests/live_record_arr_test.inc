/* SPDX-License-Identifier: GPL-3.0-only */
#include "bb_live_record_arr.h"
static bb_live_record recorder;
static void record_cases(void){
 build_song();const bb_arr_section *section=bb_arr_find(&model,model.rows[0].section);CK(section);
 uint32_t id=section->id;bb_arr_part original=section->part[0];
 CK(!bb_arr_source_drum(&sources,original,&rich));
 CK(!bb_live_record_begin(&recorder,&rich.phrase,model.revision,0,100,100));
 unsigned added=0;
 for(unsigned step=0;step<rich.phrase.length&&!added;step++)for(unsigned v=0;v<19&&!added;v++)
  if(bb_live_record_hit(&recorder,100+step*100,v,1)==BB_REC_OK)added=1;
 CK(added);CK(!bb_live_record_finish(&recorder));model_copy=model;sources_copy=sources;
 CK(bb_arr_record_commit(&model,&sources,id,0,&recorder,0,&rich)==BB_REC_INVALID);
 CK(!memcmp(&model,&model_copy,sizeof model));CK(!memcmp(&sources,&sources_copy,sizeof sources));
 CK(!bb_arr_record_commit(&model,&sources,id,0,&recorder,UINT32_MAX,&rich));
 section=bb_arr_find(&model,id);bb_arr_part recorded=section->part[0];CK(recorded.source==BB_ARR_WORKING);
 CK(sources.work_count==sources_copy.work_count+1);CK(!memcmp(&sources.work[recorded.id-1].origin,&original,sizeof original));
 CK(!bb_arr_source_drum(&sources,recorded,&rich));CK(!memcmp(&rich.phrase,&recorder.take,sizeof recorder.take));
 CK(!bb_arr_undo(&model));section=bb_arr_find(&model,id);CK(!memcmp(&section->part[0],&original,sizeof original));
 CK(!bb_arr_redo(&model));section=bb_arr_find(&model,id);CK(!memcmp(&section->part[0],&recorded,sizeof recorded));
 CK(bb_arr_record_commit(&model,&sources,id,0,&recorder,UINT32_MAX,&rich)==BB_REC_CONFLICT);
 /* At the journal bound, a new commit ages exactly the oldest entry. */
 CK(!bb_live_record_undo(&recorder));CK(!bb_arr_source_drum(&sources,recorded,&rich));
 while(model.cursor<BB_ARR_HISTORY){bb_arr_row row=model.rows[0];row.repeats=row.repeats==1?2:1;CK(!bb_arr_row_replace(&model,0,row));}
 CK(!bb_live_record_begin(&recorder,&rich.phrase,model.revision,0,0,100));added=0;
 for(unsigned step=0;step<rich.phrase.length&&!added;step++)for(unsigned v=0;v<19&&!added;v++)
  if(bb_live_record_hit(&recorder,step*100,v,0)==BB_REC_OK)added=1;
 CK(added);CK(!bb_live_record_finish(&recorder));model_copy=model;sources_copy=sources;
 CK(!bb_arr_record_commit(&model,&sources,id,0,&recorder,UINT32_MAX,&rich));
 CK(bb_arr_validate(&model)&&bb_arr_source_owners_valid(&sources));CK(model.history_count==BB_ARR_HISTORY);
 bb_arr_part aged=bb_arr_find(&model,id)->part[0];CK(!bb_arr_undo(&model));CK(!memcmp(&bb_arr_find(&model,id)->part[0],&recorded,sizeof recorded));
 CK(!bb_arr_redo(&model));CK(!memcmp(&bb_arr_find(&model,id)->part[0],&aged,sizeof aged));
}
#if BB_WITH_DILLO_KIT_VIEW
static void stopped_pad_activity(void){
 setup(0);drain();CK(!audio.core.running);bb_dillo_strikes before,after;
 CK(bb_audio_dillo_strikes_read(&audio,0,&before));CK(!before.running);
 bb_drum_step steps[BB_STEPS];memcpy(steps,audio.core.drums[0].steps,sizeof steps);
 push((bb_command){.type=BB_CMD_DRUM_TRIGGER,.target=0,.voice=D8_LT,.value=100});drain();
 CK(bb_audio_dillo_strikes_read(&audio,0,&after));CK(!after.running);CK(!audio.core.running);
 CK(after.strikes[BB_FDV_LT]==before.strikes[BB_FDV_LT]+1);
 CK(!memcmp(steps,audio.core.drums[0].steps,sizeof steps));
 before=after;push((bb_command){.type=BB_CMD_DRUM_PARAMETER,.target=0,.voice=D8_LT,.step=BB_DRUM_MODE,.value=1});
 push((bb_command){.type=BB_CMD_DRUM_TRIGGER,.target=0,.voice=D8_LT,.value=100});drain();
 CK(bb_audio_dillo_strikes_read(&audio,0,&after));CK(after.strikes[BB_FDV_LC]==before.strikes[BB_FDV_LC]+1);
 CK(after.strikes[BB_FDV_LT]==before.strikes[BB_FDV_LT]);CK(!audio.core.running);
 before=after;push((bb_command){.type=BB_CMD_DRUM_PARAMETER,.target=0,.voice=D8_CP,.step=BB_DRUM_MODE,.value=1});
 push((bb_command){.type=BB_CMD_DRUM_TRIGGER,.target=0,.voice=D8_CP,.value=100});drain();
 CK(bb_audio_dillo_strikes_read(&audio,0,&after));CK(after.strikes[BB_FDV_MA]==before.strikes[BB_FDV_MA]+1);
 CK(!memcmp(steps,audio.core.drums[0].steps,sizeof steps));CK(!audio.core.running);
}
#endif
/* Direct phrase loads cannot overwrite the arranger's queued whole section.
 * Verify refusal is byte-preserving and STOP/poll releases the reservation. */
static bb_audio refused_audio;
static void direct_loader_ownership(void){
 build_song();CK(!bb_arr_controller_start(&controller,&audio));
 for(unsigned i=0;i<10;i++){drain();CK(!bb_arr_controller_poll(&controller,&audio));}
 CK(controller.ticket&&audio.core.running);CK(audio.section.phase!=BB_SECTION_FREE);
 for(unsigned i=0;i<BB_PATTERN_SLOTS;i++)CK(audio.rich.inbox[i].state==BB_SECTION_SLOT);
 rich=audio.rich.active[0];CK(bb_audio_phrase_preflight_playable(&rich,bb_audio_phrase_source_voices_owned(&audio,0)));
 uint32_t ticket=UINT32_C(0xa55a5aa5);refused_audio=audio;model_copy=model;sources_copy=sources;
 CK(!bb_audio_phrase_submit(&audio,0,&rich,16,&ticket));CK(ticket==UINT32_C(0xa55a5aa5));
 CK(!memcmp(&audio,&refused_audio,sizeof audio));CK(!memcmp(&model,&model_copy,sizeof model));CK(!memcmp(&sources,&sources_copy,sizeof sources));
 CK(!bb_arr_controller_stop(&controller,&audio));
 for(unsigned i=0;i<10;i++){drain();CK(!bb_arr_controller_poll(&controller,&audio));}
 CK(!audio.core.running&&!controller.ticket);CK(audio.section.phase==BB_SECTION_FREE);
 for(unsigned i=0;i<BB_PATTERN_SLOTS;i++)CK(audio.rich.inbox[i].state==BB_PATTERN_SLOT_FREE);
 rich=audio.rich.active[0];CK(bb_audio_phrase_submit(&audio,0,&rich,16,&ticket));
 CK(ticket!=UINT32_C(0xa55a5aa5));drain();CK(!audio.core.running);CK(audio.rich.status[0].applied==ticket);
 CK(audio.rich.status[0].result==BB_RICH_APPLIED);CK(!memcmp(&rich,&audio.rich.active[0],sizeof rich));
 CK(!memcmp(&model,&model_copy,sizeof model));CK(!memcmp(&sources,&sources_copy,sizeof sources));
 printf("PHRASE LOAD ownership refusal is atomic; STOP/poll releases slots and unchanged reload stages without PLAY\n");
}
#if BB_WITH_LIVE_CAPTURE
static bb_capture_clock capture_clock,clock_guard;
static bb_capture_event capture_event,event_guard;
static void capture_start(void){
 build_song();CK(!bb_arr_controller_start(&controller,&audio));
 for(unsigned i=0;i<10;i++){drain();CK(!bb_arr_controller_poll(&controller,&audio));}
 CK(audio.core.running&&audio.rich.valid[0]);CK(bb_commands_idle(&audio.commands));
 CK(!bb_audio_capture_begin(&audio,0,&capture_clock));
 CK(capture_clock.frame==audio.core.frame);CK(capture_clock.bpm==audio.core.bpm);
 CK(capture_clock.source_phase==audio.rich.player[0].phase);CK(capture_clock.source_step==audio.rich.player[0].step);
 CK(capture_clock.lattice==audio.rich.active[0].phrase.lattice);CK(capture_clock.length==audio.rich.active[0].phrase.length);
}
static bb_live_record pipeline_recorder;
static void capture_pipeline(void){
 capture_start();bb_phrase before=audio.rich.active[0].phrase;
 unsigned subdivision=capture_clock.lattice==BB_FDL_EIGHTHS?2:capture_clock.lattice==BB_FDL_TRIPLETS?6:4;
 CK(!bb_live_record_begin_phase(&pipeline_recorder,&before,model.revision,0,capture_clock.frame,
  capture_clock.sample_rate*6000u,capture_clock.bpm*subdivision*100u,capture_clock.source_phase,capture_clock.source_step));
 static const unsigned voices[11]={0,1,2,3,4,8,10,12,13,14,15};unsigned pad=11;
 for(unsigned lane=0;lane<11&&pad==11;lane++){
  unsigned present=0;for(unsigned i=0;i<before.count;i++)if((before.events[i]&31u)==voices[lane]&&!((before.events[i]>>5)&63u))present=1;
  if(!present)pad=lane;
 }CK(pad<11);
 push((bb_command){.type=BB_CMD_DRUM_TRIGGER,.target=0,.voice=(uint8_t)pad,.value=127});drain();
 CK(!bb_audio_capture_read(&audio,&capture_event));
 CK(!bb_live_record_hit(&pipeline_recorder,capture_event.frame,capture_event.voice,capture_event.velocity==127));
 CK(!bb_audio_capture_end(&audio));CK(bb_audio_capture_read(&audio,&capture_event)==BB_CAPTURE_CLOSED);
 CK(!bb_live_record_finish(&pipeline_recorder));
 CK(!bb_arr_controller_stop(&controller,&audio));
 for(unsigned i=0;i<10;i++){drain();CK(!bb_arr_controller_poll(&controller,&audio));}
 CK(bb_arr_controller_can_edit(&controller,&audio));
 uint32_t id=model.rows[0].section;bb_arr_part original=bb_arr_find(&model,id)->part[0];unsigned cursor=model.cursor;
 CK(!bb_arr_record_commit(&model,&sources,id,0,&pipeline_recorder,UINT32_MAX,&rich));
 CK(model.cursor==cursor+1);bb_arr_part added=bb_arr_find(&model,id)->part[0];CK(added.source==BB_ARR_WORKING);
 CK(!bb_arr_source_drum(&sources,added,&rich));CK(rich.phrase.count==before.count+1);
 CK(!memcmp(rich.phrase.events,before.events,before.count*sizeof before.events[0]));
 CK(!bb_arr_undo(&model));CK(!memcmp(&bb_arr_find(&model,id)->part[0],&original,sizeof original));
 CK(!bb_arr_redo(&model));CK(!memcmp(&bb_arr_find(&model,id)->part[0],&added,sizeof added));
 CK(!bb_audio_capture_discard(&audio));
 printf("CAPTURE PIPELINE accepted owner hit -> recorder -> stopped immutable working part -> Undo/Redo; engine-phase rational grid, single-hit transaction\n");
}
static bb_live_record phase_seed,phase_take;
static bb_phrase phase_phrase;
static void actual_phase_cases(void){
 unsigned hits=0;
 for(unsigned lattice=0;lattice<=BB_FDL_TRIPLETS;lattice++)for(unsigned t=0;t<3;t++){
  static const unsigned tempos[3]={30,120,174};setup(0);drain();
  memset(&phase_phrase,0,sizeof phase_phrase);phase_phrase.lattice=(uint8_t)lattice;phase_phrase.bars=1;
  unsigned subdivision=lattice==BB_FDL_EIGHTHS?2:lattice==BB_FDL_TRIPLETS?6:4;
  phase_phrase.length=(uint8_t)(4*subdivision);CK(bb_phrase_index_prepare(&phase_phrase,UINT32_MAX,&rich));
  uint32_t ticket=0;CK(bb_audio_phrase_submit(&audio,0,&rich,16,&ticket));drain();
  push((bb_command){.type=BB_CMD_TEMPO,.value=(uint16_t)tempos[t]});push((bb_command){.type=BB_CMD_TRANSPORT,.value=1});drain();
  CK(!bb_audio_capture_begin(&audio,0,&capture_clock));memset(&phase_seed,0,sizeof phase_seed);
  uint32_t numerator=capture_clock.sample_rate*6000u,divisor=capture_clock.bpm*subdivision*100u;
  CK(!bb_live_record_begin_phase(&phase_seed,&phase_phrase,1,0,capture_clock.frame,numerator,divisor,capture_clock.source_phase,capture_clock.source_step));
  for(unsigned i=0;i<512;i++){
   unsigned expected=(audio.rich.player[0].step+(audio.rich.player[0].phase>=numerator/2u+numerator%2u))%phase_phrase.length;
   push((bb_command){.type=BB_CMD_DRUM_TRIGGER,.target=0,.voice=D8_BD,.value=88});drain();
   CK(!bb_audio_capture_read(&audio,&capture_event));phase_take=phase_seed;
   CK(!bb_live_record_hit(&phase_take,capture_event.frame,capture_event.voice,0));
   CK(((phase_take.take.events[0]>>5)&63u)==expected);hits++;
  }
  CK(!bb_audio_capture_end(&audio));CK(bb_audio_capture_read(&audio,&capture_event)==BB_CAPTURE_CLOSED);CK(!bb_audio_capture_discard(&audio));
 }
 printf("PHASE %u accepted owner hits match actual source phase across 30/120/174 BPM and eighth/sixteenth/triplet grids\n",hits);
}
static void capture_cases(void){
 setup(0);drain();CK(bb_audio_capture_begin(&audio,0,&capture_clock)==BB_CAPTURE_INVALID);
 CK(bb_audio_capture_read(&audio,&capture_event)==BB_CAPTURE_CLOSED);
 capture_start();clock_guard=capture_clock;
 CK(bb_audio_capture_begin(&audio,1,&capture_clock)==BB_CAPTURE_BUSY);CK(!memcmp(&clock_guard,&capture_clock,sizeof capture_clock));
 CK(bb_audio_capture_read(&audio,(bb_capture_event *)&audio)==BB_CAPTURE_INVALID);
 event_guard=capture_event;CK(bb_audio_capture_read(&audio,&capture_event)==BB_CAPTURE_EMPTY);CK(!memcmp(&event_guard,&capture_event,sizeof capture_event));
 uint64_t accepted_frame=audio.core.frame;
 push((bb_command){.type=BB_CMD_DRUM_TRIGGER,.target=0,.voice=D8_BD,.value=93});drain();
 int result=bb_audio_capture_read(&audio,&capture_event);
 if(result)fprintf(stderr,"capture result=%d armed=%u count=%u error=%u hits=%u epoch=%u/%u applied=%u/%u transport=%u/%u bpm=%u/%u kit=%u/%u\n",result,audio.capture.armed,audio.capture.count,audio.capture.error,audio.hits[0],audio.drum_patterns.family_epoch[0],capture_clock.epoch,audio.rich.status[0].applied,capture_clock.applied,audio.section.status.transport_epoch,capture_clock.transport_epoch,audio.core.bpm,capture_clock.bpm,bb_audio_sample_layer_id(&audio,0),capture_clock.kit);
 CK(result==BB_CAPTURE_OK);CK(capture_event.frame==accepted_frame);
 CK(capture_event.voice==BB_FDV_BD&&capture_event.velocity==93&&capture_event.target==0);
 CK(capture_event.epoch==capture_clock.epoch);CK(bb_audio_capture_read(&audio,&capture_event)==BB_CAPTURE_EMPTY); /* no copied sequencer event */
 for(unsigned i=0;i<40;i++){
  push((bb_command){.type=BB_CMD_DRUM_TRIGGER,.target=0,.voice=D8_SD,.value=88});drain();
  CK(!bb_audio_capture_read(&audio,&capture_event));CK(capture_event.voice==BB_FDV_SD);
 }
 CK(audio.capture.head==9); /* one kick +40 snares wraps the ring */
 push((bb_command){.type=BB_CMD_DRUM_PARAMETER,.target=0,.voice=D8_LT,.step=BB_DRUM_MODE,.value=1});
 push((bb_command){.type=BB_CMD_DRUM_TRIGGER,.target=0,.voice=D8_LT,.value=127});
 CK(bb_audio_capture_end(&audio)==BB_CAPTURE_BUSY);CK(audio.capture.armed);drain();
 CK(!bb_audio_capture_read(&audio,&capture_event));CK(capture_event.voice==BB_FDV_LC&&capture_event.velocity==127);
 CK(!bb_audio_capture_end(&audio));CK(bb_audio_capture_read(&audio,&capture_event)==BB_CAPTURE_CLOSED);
 CK(bb_audio_capture_begin(&audio,0,&capture_clock)==BB_CAPTURE_BUSY);CK(!bb_audio_capture_discard(&audio));
 /* Other-layer and ordinary sequencer strikes never become manual input. */
 CK(!bb_audio_capture_begin(&audio,0,&capture_clock));push((bb_command){.type=BB_CMD_DRUM_TRIGGER,.target=1,.voice=D8_BD,.value=88});drain();
 CK(bb_audio_capture_read(&audio,&capture_event)==BB_CAPTURE_EMPTY);
 for(unsigned i=0;i<33;i++){push((bb_command){.type=BB_CMD_DRUM_TRIGGER,.target=0,.voice=D8_BD,.value=88});drain();}
 CK(audio.capture.count==32&&audio.capture.lost==1&&!audio.capture.armed);
 event_guard=capture_event;CK(bb_audio_capture_read(&audio,&capture_event)==BB_CAPTURE_OVERFLOW);CK(!memcmp(&event_guard,&capture_event,sizeof capture_event));
 CK(!bb_audio_capture_discard(&audio));CK(!bb_audio_capture_begin(&audio,0,&capture_clock));
 unsigned bpm=audio.core.bpm;
 push((bb_command){.type=BB_CMD_TEMPO,.value=(uint16_t)(bpm+1)});push((bb_command){.type=BB_CMD_TEMPO,.value=(uint16_t)bpm});drain();
 CK(audio.core.bpm==bpm);CK(bb_audio_capture_read(&audio,&capture_event)==BB_CAPTURE_CHANGED);
 CK(!bb_audio_capture_discard(&audio));CK(!bb_audio_capture_begin(&audio,0,&capture_clock));
 push((bb_command){.type=BB_CMD_TRANSPORT,.value=0});drain();
 CK(bb_audio_capture_read(&audio,&capture_event)==BB_CAPTURE_CLOSED);CK(!audio.core.running);
 CK(!bb_audio_capture_discard(&audio));
 capture_start();push((bb_command){.type=BB_CMD_DRUM_ENABLE,.target=0,.value=0});push((bb_command){.type=BB_CMD_DRUM_ENABLE,.target=0,.value=1});drain();
 CK(bb_audio_capture_read(&audio,&capture_event)==BB_CAPTURE_CHANGED);CK(!bb_audio_capture_discard(&audio));
 capture_start();push((bb_command){.type=BB_CMD_DRUM_MUTE,.target=0,.value=1});push((bb_command){.type=BB_CMD_DRUM_MUTE,.target=0,.value=0});drain();
 CK(bb_audio_capture_read(&audio,&capture_event)==BB_CAPTURE_CHANGED);CK(!bb_audio_capture_discard(&audio));
 capture_start();push((bb_command){.type=BB_CMD_TRANSPORT_RESTART});drain();
 CK(bb_audio_capture_read(&audio,&capture_event)==BB_CAPTURE_CHANGED);
 CK(!bb_audio_capture_discard(&audio));
 capture_start();CK(!bb_arr_controller_cue(&controller,&audio,3));
 unsigned attempts=0;while(controller.playing_row!=3&&attempts++<2000){drain();CK(!bb_arr_controller_poll(&controller,&audio));}
 CK(attempts<2000);CK(bb_audio_capture_read(&audio,&capture_event)==BB_CAPTURE_CHANGED);
 CK(!bb_audio_capture_discard(&audio));
 capture_start();(void)bb_audio_fault_drain(&audio);
 CK(bb_audio_capture_read(&audio,&capture_event)==BB_CAPTURE_FAULT);CK(!audio.capture.armed);
 CK(!bb_audio_capture_discard(&audio));
 /* Compile gate changes no live pattern/scene project. */
 CK(sizeof(bb_capture_event)==16);printf("CAPTURE event=%zuB state=%zuB; accepted manual hits only\n",sizeof(bb_capture_event),sizeof(bb_live_capture));
}
#endif
int main(void){collect();out=fopen(getenv("BB_LIVE_PCM")?getenv("BB_LIVE_PCM"):"/dev/null","wb");CK(out);record_cases();play_song(0,0);
#if BB_WITH_DILLO_KIT_VIEW
 bb_dillo_strikes snapshot;
 for(unsigned m=0;m<2;m++){
  CK(bb_audio_dillo_strikes_read(&audio,m,&snapshot));uint32_t total=0;
  for(unsigned v=0;v<BB_PHRASE_VOICES;v++)total+=snapshot.strikes[v];
  printf("ACTIVITY m%u rich_strikes=%u all_hits=%u\n",m,total,audio.hits[m]);CK(total==audio.hits[m]);CK(total>0);CK(!snapshot.active);
 }
 CK(!bb_audio_dillo_strikes_read(&audio,2,&snapshot));
 stopped_pad_activity();
#endif
#if BB_WITH_LIVE_CAPTURE
 capture_cases();capture_pipeline();actual_phase_cases();
#endif
direct_loader_ownership();
printf("PASS %u real catalogue/arranger recording checks\n",checks);fclose(out);return 0;}
