/* SPDX-License-Identifier: GPL-3.0-only */
#include "bb/bb_live_record_arr.h"
#if BB_WITH_LIVE_CAPTURE
enum {BB_RECORD_IDLE,BB_RECORD_SETUP,BB_RECORD_START,BB_RECORD_COUNT,
 BB_RECORD_LIVE,BB_RECORD_STOP,BB_RECORD_REVIEW,BB_RECORD_ERROR};
static struct {
 bb_live_record take;bb_capture_clock clock;bb_capture_event event;bb_section_status view;
 uint64_t gate;uint32_t section,revision,ticket,deadline,count_tick;
 const char *error;uint8_t phase,target,countin,accent,clear_undo,pattern;   /* pattern: REC+SEQ take over the playing pattern */
} bb_record __attribute__((section(".pool")));
static void bb_record_boot(void){memset(&bb_record,0,sizeof bb_record);}
static void bb_record_owner_reset(void){
 if(bb_record.phase){bb_record.phase=BB_RECORD_ERROR;bb_record.error=BB_TX(TX_AUDIO_REPLACED_DISCARD_TAKE);}
}
static void bb_record_stop(const char *error){
 if(error&&!bb_record.error)bb_record.error=error;
 if(bb_record.phase==BB_RECORD_STOP)return;
 if(!bb_record.pattern)(void)bb_arr_controller_stop(&bb_arr_workspace.controller,&bb_sound);   /* a pattern take keeps the music playing */
 bb_record.phase=BB_RECORD_STOP;bb_record.deadline=fm1_ms+2000u;bb_dirty=1;
}
static void bb_record_clear(void){
 (void)bb_audio_capture_discard(&bb_sound);(void)bb_live_record_undo(&bb_record.take);
 bb_record.phase=BB_RECORD_IDLE;bb_record.error=NULL;bb_record.clear_undo=0;bb_record.pattern=0;bb_dirty=1;
}
static unsigned bb_record_layer(void){
 uintptr_t guard=bb_target_enter(0);unsigned v=bb_sound.core.drums[bb_record.target].enabled|((unsigned)bb_sound.core.drums[bb_record.target].muted<<1);bb_target_leave(0,guard);return v;
}
static void bb_record_start(void){
 bb_arrangement *m=&bb_arr_workspace.model;bb_arr_controller *c=&bb_arr_workspace.controller;
 if(!m->row_count||c->selected>=m->row_count||!bb_arr_edit_allowed())return;
 bb_record.section=m->rows[c->selected].section;bb_record.revision=m->revision;
 int result=bb_arr_prepare_section(m,&bb_arr_workspace.sources,bb_record.section,1,&bb_arr_workspace.preview);
 if(result){bb_say(BB_TX(TX_SECTION_SOURCE_REFUSED));return;}
 unsigned row=c->selected; (void)bb_arr_controller_init(c,m,&bb_arr_workspace.sources);c->selected=(uint8_t)row;
 /* Temporary performance only: repeat selected sources without rewriting the
  * arrangement's duration/repeats. STOP releases this owner before KEEP. */
 bb_arr_workspace.preview.duration_ticks=0;bb_arr_workspace.preview.wait_ticks=0;
 bb_arr_workspace.preview.at_tick=0;bb_arr_workspace.preview.restart=1;
 if(!bb_audio_section_submit(&bb_sound,&bb_arr_workspace.preview,&bb_record.ticket)){
  bb_say(BB_TX(TX_SOURCE_BUSY_MUSIC_UNCHANGED));return;}
 memset(&bb_record.take,0,sizeof bb_record.take);bb_record.error=NULL;bb_record.clear_undo=0;
 bb_record.phase=BB_RECORD_START;bb_record.deadline=fm1_ms+2000u;bb_dirty=1;
}
/* 2.0: REC+SEQ on PLAY/STEPS records into the pattern that is playing on the focused kit: no SONG row,
 * no setup page. Count-in to the next bar line, the pattern's own grid (nearest step, as the SONG
 * recorder), pads 1-11, REC finishes, EDIT keeps (an editable copy, bb_pattern_edit_firmware.inc),
 * REC discards, and REC+EDIT undoes a kept take. Same recorder, capture ring and refusals as SONG. */
#if BB_WITH_PATTERN_COPY
static void bb_record_pattern_begin(void){
 unsigned m=bb_face.machine;
 if(bb_record.phase)return;
 if(!bb_nav_is_drum()||m>=BB_RICH_DRUMS){bb_say(BB_TX(TX_RECORD_CHOOSE_KIT_A_OR_B));return;}
 if(!bb_rich_active()){bb_say(BB_TX(TX_LOAD_A_PATTERN_TO_RECORD));return;}
 if(bb_pe.pending[m]){bb_say(BB_TX(TX_EDIT_QUEUED_RECORD_AFTER_IT));return;}
 if(!bb_face.requested.drums[m].enabled||bb_face.requested.drums[m].muted){bb_say(BB_TX(TX_LAYER_OFF_OR_MUTED_NO_TAKE));return;}
 memset(&bb_record.take,0,sizeof bb_record.take);
 bb_record.pattern=1;bb_record.target=(uint8_t)m;bb_record.countin=1;bb_record.accent=0;bb_record.error=NULL;bb_record.clear_undo=0;
 if(!bb_face.requested.running)bb_nav_transport(0);
 bb_record.phase=BB_RECORD_START;bb_record.deadline=fm1_ms+2000u;bb_hint_until=0;bb_dirty=1;
}
static void bb_record_pattern_keep(void){
 unsigned m=bb_record.target;bb_phrase_view *v=&bb_rich_views[m];
 if(bb_audio_phrase_view_read(&bb_sound,m,v)==0||!v->valid){bb_say(BB_TX(TX_BUSY_TAKE_KEPT_IN_RAM));return;}
 int r=bb_pe_take(&bb_record.take,bb_record.revision,&v->phrase,&bb_pe.work);
 if(r==BB_PE_UNCHANGED){bb_record_clear();bb_say(BB_TX(TX_NO_NEW_HITS_PATTERN_KEPT));return;}
 if(r!=BB_PE_OK){bb_record_clear();bb_say(BB_TX(TX_PATTERN_CHANGED_TAKE_NOT_KEPT));return;}
 bb_pe.before=v->phrase;
 if(!bb_pe_publish(m,&bb_pe.before,&bb_pe.work,1,BB_TX(TX_TAKE_QUEUED_NEXT_LOOP),BB_TX(TX_TAKE_APPLIED_REC_EDIT_UNDO)))return;
 bb_record_clear();
}
#else
static void bb_record_pattern_begin(void){bb_say("RECORD: NOT IN THIS BUILD");}
static void bb_record_pattern_keep(void){}
#endif
static int bb_record_read(void){
 for(unsigned i=0;i<BB_CAPTURE_CAPACITY;i++){
  int result=bb_audio_capture_read(&bb_sound,&bb_record.event);
  if(result==BB_CAPTURE_EMPTY||result==BB_CAPTURE_CLOSED)break;
  if(result){bb_record_stop(result==BB_CAPTURE_OVERFLOW?BB_TX(TX_INPUT_OVERFLOW_TAKE_REFUSED):result==BB_CAPTURE_FAULT?"AUDIO FAULT / TAKE REFUSED":BB_TX(TX_SOURCE_CHANGED_TAKE_REFUSED));return 0;}
  if(bb_record.event.frame<bb_record.gate)continue;
  if(bb_record.phase==BB_RECORD_COUNT){bb_record.phase=BB_RECORD_LIVE;bb_dirty=1;}
  result=bb_live_record_hit(&bb_record.take,bb_record.event.frame,bb_record.event.voice,bb_record.event.velocity==127);
  if(result==BB_REC_DUPLICATE)continue;
  if(result==BB_REC_PROTECTED){bb_say("PROTECTED HIT KEPT");continue;}
  if(result){bb_record_stop(result==BB_REC_FULL?BB_TX(TX_128_HITS_FULL_TAKE_REFUSED):BB_TX(TX_RECORD_EVENT_REFUSED));return 0;}
  bb_dirty=1;
 }
 return 1;
}
static void bb_record_poll(void){
 if(!bb_record.phase)return;
 if(!bb_audio_section_status_read(&bb_sound,&bb_record.view)){bb_record_stop(BB_TX(TX_AUDIO_STATUS_REFUSED));return;}
 bb_face.requested.running=bb_record.view.running;
 if(bb_record.phase==BB_RECORD_SETUP||bb_record.phase>=BB_RECORD_REVIEW)return;
 if(!bb_record.pattern&&bb_record.revision!=bb_arr_workspace.model.revision)bb_record_stop(BB_TX(TX_PROJECT_CHANGED_TAKE_REFUSED));
 #if BB_WITH_PATTERN_COPY
 if(bb_record.phase==BB_RECORD_START&&bb_record.pattern){
  if(!bb_record.view.running){if((int32_t)(fm1_ms-bb_record.deadline)>=0)bb_record_stop(BB_TX(TX_PLAY_DID_NOT_START_TAKE_REFUSED));return;}
  int result=bb_audio_capture_begin(&bb_sound,bb_record.target,&bb_record.clock);
  if(result==BB_CAPTURE_BUSY){if((int32_t)(fm1_ms-bb_record.deadline)>=0)bb_record_stop(BB_TX(TX_INPUT_BUSY_TAKE_REFUSED));return;}
  if(result){bb_record_stop(BB_TX(TX_LAYER_OFF_CLOCK_SOURCE_REFUSED));return;}
  bb_phrase_view *v=&bb_rich_views[bb_record.target];
  if(bb_audio_phrase_view_read(&bb_sound,bb_record.target,v)==0||!v->valid||v->applied!=bb_record.clock.applied||
     bb_pe_take_begin(&bb_record.take,&v->phrase,&bb_record.clock,bb_record.countin,&bb_record.gate)!=BB_PE_OK){
   bb_record_stop(BB_TX(TX_PATTERN_CHANGING_TRY_AGAIN));return;}
  bb_record.revision=bb_record.clock.applied;
  bb_record.count_tick=(bb_record.clock.ticks?bb_record.clock.ticks-1u:0u)/16u*16u+17u;
  bb_record.phase=bb_record.countin?BB_RECORD_COUNT:BB_RECORD_LIVE;bb_dirty=1;
 }
 #endif
 if(bb_record.phase==BB_RECORD_START){
  if(bb_record.view.state.applied!=bb_record.ticket){
   if((int32_t)(fm1_ms-bb_record.deadline)>=0)bb_record_stop(BB_TX(TX_SOURCE_ACK_TIMEOUT_TAKE_REFUSED));return;
  }
  int result=bb_audio_capture_begin(&bb_sound,bb_record.target,&bb_record.clock);
  if(result==BB_CAPTURE_BUSY){if((int32_t)(fm1_ms-bb_record.deadline)>=0)bb_record_stop(BB_TX(TX_INPUT_BUSY_TAKE_REFUSED));return;}
  if(result){bb_record_stop(BB_TX(TX_LAYER_OFF_CLOCK_SOURCE_REFUSED));return;}
  const bb_arr_section *section=bb_arr_find(&bb_arr_workspace.model,bb_record.section);
  bb_arr_part part=section->part[bb_record.target];uint64_t protected_steps=0;
  if(part.source==BB_ARR_WORKING)protected_steps=bb_arr_workspace.sources.work[part.id-1].protected_steps;
  unsigned subdivision=bb_record.clock.lattice==BB_FDL_EIGHTHS?2:bb_record.clock.lattice==BB_FDL_TRIPLETS?6:4;
  uint32_t even=bb_swing_denominator(bb_record.clock.sample_rate,bb_record.clock.swing,0);
  uint32_t odd=bb_swing_denominator(bb_record.clock.sample_rate,bb_record.clock.swing,1);
  uint32_t divisor=bb_record.clock.bpm*subdivision*100u;
  result=bb_live_record_begin_pair_phase(&bb_record.take,&bb_arr_workspace.preview.drum[bb_record.target].phrase,
    bb_record.revision,protected_steps,bb_record.clock.frame,even,odd,divisor,
    bb_record.clock.source_phase,bb_record.clock.source_step,bb_record.clock.source_odd);
  if(result){bb_record_stop(BB_TX(TX_RECORD_GRID_REFUSED));return;}
  bb_record.gate=bb_record.clock.frame;
  if(bb_record.countin){
   uint64_t wait;
   if(bb_live_record_wait_steps(&bb_record.take,4u*subdivision-bb_record.clock.source_step%(4u*subdivision),&wait)){
    bb_record_stop(BB_TX(TX_RECORD_GRID_REFUSED));return;
   }
   if(wait>UINT64_MAX-bb_record.gate){bb_record_stop(BB_TX(TX_RECORD_CLOCK_EXHAUSTED));return;}
   bb_record.gate+=wait;bb_record.count_tick=(bb_record.clock.ticks?bb_record.clock.ticks-1u:0u)/16u*16u+17u;
  }
  bb_record.phase=bb_record.countin?BB_RECORD_COUNT:BB_RECORD_LIVE;bb_dirty=1;
 }
 if(bb_record.phase==BB_RECORD_COUNT&&(int32_t)(bb_record.view.current_tick-bb_record.count_tick)>=0){bb_record.phase=BB_RECORD_LIVE;bb_dirty=1;}
 if(bb_record.take.state==BB_REC_ARMED||bb_record.take.state==BB_REC_RECORDING){
  (void)bb_record_read();
 }
 if(bb_record.phase==BB_RECORD_STOP){
  if(!bb_record.pattern&&!bb_arr_controller_can_edit(&bb_arr_workspace.controller,&bb_sound)){
   if((int32_t)(fm1_ms-bb_record.deadline)>=0){bb_record.phase=BB_RECORD_ERROR;bb_record.error=BB_TX(TX_STOP_NOT_CONFIRMED_TAKE_KEPT);bb_dirty=1;}return;
  }
  if(bb_record.take.state!=BB_REC_REVIEW){
   int result=bb_audio_capture_end(&bb_sound);
   /* Music still runs on a pattern take: wait for queued pads, then drain what arrived after the last read. */
   if(bb_record.pattern&&result==BB_CAPTURE_BUSY&&(int32_t)(fm1_ms-bb_record.deadline)<0)return;
   if(bb_record.pattern&&(result==BB_CAPTURE_OK||result==BB_CAPTURE_CLOSED)&&!bb_record_read()&&!bb_record.error)bb_record.error=BB_TX(TX_INCOMPLETE_TAKE_DISCARD);
   if(result!=BB_CAPTURE_OK&&result!=BB_CAPTURE_CLOSED&&!bb_record.error)bb_record.error=BB_TX(TX_INCOMPLETE_TAKE_DISCARD);
   if(!bb_record.error&&bb_live_record_finish(&bb_record.take))bb_record.error=BB_TX(TX_NO_RECORD_PASS_DISCARD);
  }
  bb_record.phase=bb_record.error?BB_RECORD_ERROR:BB_RECORD_REVIEW;bb_dirty=1;
 }
}
static int bb_record_input(uint32_t pressed,uint32_t keys){
 if(!bb_record.phase)return 0;
 if(pressed&(1u<<panel.btn[B_SAVE])){bb_say(BB_TX(TX_KEEP_FIRST_FILE_SAVE_LATER));return 1;}
 int knobs[4];for(unsigned i=0;i<4;i++)knobs[i]=bb_read_encoder(EN_K1+i);
 for(unsigned i=0;i<3;i++)(void)bb_read_encoder(i);
 if(bb_record.phase==BB_RECORD_SETUP){
  if(knobs[0])bb_record.target=(uint8_t)bb_limit((int)bb_record.target+bb_limit(knobs[0],-1,1),0,1);
  if(knobs[1])bb_record.countin=(uint8_t)(knobs[1]>0);
  if(knobs[2])bb_record.accent=(uint8_t)(knobs[2]>0);
  if(knobs[3]){bb_command command={0};command.type=BB_CMD_DRUM_ENABLE;command.target=bb_record.target;command.value=(uint16_t)(knobs[3]>0);if(!bb_command_push(&bb_sound.commands,&command))bb_say(BB_TX(TX_LAYER_INPUT_BUSY));}
  if(pressed&((1u<<panel.btn[B_HOME])|(1u<<panel.btn[B_GLO]))){bb_record_clear();return 1;}
  if(pressed&(1u<<panel.btn[B_REC]))bb_record_start();
  bb_dirty=1;return 1;
 }
 if(bb_record.phase==BB_RECORD_REVIEW||bb_record.phase==BB_RECORD_ERROR){
  if(pressed&(1u<<panel.btn[B_PLAY])){if(bb_record.pattern)bb_nav_transport(0);else bb_record_stop(NULL);return 1;}
  if(pressed&(1u<<panel.btn[B_REC])){
   if(bb_record.clear_undo){bb_record.clear_undo=0;bb_hint_until=0;bb_dirty=1;return 1;}
   if(!bb_record.pattern&&!bb_arr_controller_can_edit(&bb_arr_workspace.controller,&bb_sound)){bb_record_stop(NULL);bb_say(BB_TX(TX_WAIT_FOR_STOP_TAKE_KEPT));return 1;}
   bb_record_clear();bb_say(BB_TX(TX_TAKE_DISCARDED_PROJECT_KEPT));return 1;
  }
  if(pressed&(1u<<panel.btn[B_EDIT])){
   if(bb_record.error){bb_say(bb_record.error);return 1;}
   if(bb_record.pattern){bb_record_pattern_keep();return 1;}
   if(!bb_arr_edit_allowed())return 1;
   uint32_t playable=bb_audio_phrase_source_voices(&bb_sound,bb_record.target);
   int result=bb_arr_record_commit(&bb_arr_workspace.model,&bb_arr_workspace.sources,bb_record.section,
    bb_record.target,&bb_record.take,playable,&bb_arr_workspace.preview.drum[bb_record.target]);
   if(result==BB_REC_HISTORY_FULL){
    if(!bb_record.clear_undo){bb_record.clear_undo=1;bb_say(BB_TX(TX_CLEAR_UNDO_EDIT_YES_REC_NO));bb_dirty=1;return 1;}
    /* HISTORY_FULL is returned only after source/take/capacity preflight.
     * Both stores are main-owned; explicit discard keeps revision unchanged.
     * Reuse the same capability snapshot for the now-admissible commit. */
    if(!bb_arr_history_discard(&bb_arr_workspace.model,bb_record.revision))
     result=bb_arr_record_commit(&bb_arr_workspace.model,&bb_arr_workspace.sources,bb_record.section,
      bb_record.target,&bb_record.take,playable,&bb_arr_workspace.preview.drum[bb_record.target]);
   }
   bb_record.clear_undo=0;
   if(result&&result!=BB_REC_UNCHANGED){bb_say(result==BB_REC_WORK_FULL?BB_TX(TX_16_WORK_PARTS_FULL_TAKE_KEPT):BB_TX(TX_STALE_TAKE_PROJECT_KEPT));return 1;}
   unsigned row=bb_arr_workspace.controller.selected;
   (void)bb_arr_controller_init(&bb_arr_workspace.controller,&bb_arr_workspace.model,&bb_arr_workspace.sources);
   bb_arr_workspace.controller.selected=(uint8_t)row;bb_record_clear();bb_say(result?BB_TX(TX_NO_NEW_HITS_PROJECT_KEPT):"TAKE KEPT / RAM UNSAVED");return 1;
  }
  if(pressed)bb_say(bb_record.clear_undo?BB_TX(TX_EDIT_CONFIRM_REC_CANCEL):BB_TX(TX_EDIT_KEEP_REC_DISCARD));return 1;
 }
 if(pressed&((1u<<panel.btn[B_REC])|(1u<<panel.btn[B_PLAY])|(1u<<panel.btn[B_HOME])|(1u<<panel.btn[B_GLO]))){
  bb_record_stop(NULL);
  if(bb_record.pattern&&(pressed&(1u<<panel.btn[B_PLAY])))bb_nav_transport(0);   /* PLAY is start/stop, always */
  return 1;}
 if(bb_record.phase==BB_RECORD_LIVE||bb_record.phase==BB_RECORD_COUNT){
  if(knobs[2])bb_record.accent=(uint8_t)(knobs[2]>0);
  for(unsigned lane=0;lane<11;lane++)if(keys&(1u<<lane)){
   if(bb_record.clock.kit!=BB_SAMPLE_808_ID&&(bb_record.clock.kit>=bb_sound.sample_catalogue_count||
      !(bb_sound.sample_catalogue_lanes[bb_record.clock.kit]&(1u<<lane)))){bb_say(BB_TX(TX_PAD_NOT_IN_THIS_KIT));continue;}
   bb_command command={0};command.type=BB_CMD_DRUM_TRIGGER;command.target=bb_record.target;
   command.voice=(uint8_t)lane;command.value=(uint16_t)(bb_record.accent?127:88);
   if(!bb_command_push(&bb_sound.commands,&command)){bb_record_stop(BB_TX(TX_INPUT_QUEUE_FULL_TAKE_REFUSED));break;}
  }
 }
 return 1;
}
#else
static struct {uint8_t phase;} bb_record;
static void bb_record_boot(void){bb_record.phase=0;}
static void bb_record_owner_reset(void){}
static void bb_record_poll(void){}
static int bb_record_input(uint32_t pressed,uint32_t keys){(void)pressed;(void)keys;return 0;}
static void bb_record_pattern_begin(void){bb_say("RECORD: NOT IN THIS BUILD");}
#endif
