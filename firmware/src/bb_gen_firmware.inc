/* SPDX-License-Identifier: GPL-3.0-only
 * Main-loop producer only. Immutable authored homes and generated working
 * copies use existing copied queues; the audio owner applies every change. */
#if BB_WITH_EVOLVE && BB_WITH_RICH_DRUM_RENDER
#include "bb/bb_gen.h"
static struct {
 struct {bb_phrase_view view;bb_gen_rich evolve;uint32_t ticket;uint8_t last_step;} drum[2];
 bb_pattern_data dillo_home;
 bb_phrase phrase;bb_phrase_index index;bb_pattern_data line;
 uint32_t synth_ticket,synth_source,seed;
 uint8_t synth_home_valid,mode,voice,hits,steps,rotation,accents,density,accent,anchor,slide;
 uint8_t capture_pending,return_pending;
 uint32_t fb_ticket;uint8_t fb_kind;   /* 2.0 D4: the APPLY waiting for the owner: 1 drum A, 2 drum B, 3 Dillo; 0 none */
} bb_gen_state __attribute__((section(".pool")));
static void bb_gen_reset(void){
 memset(&bb_gen_state,0,sizeof bb_gen_state);bb_gen_state.seed=1;bb_gen_state.voice=BB_FDV_HH;
 bb_gen_state.hits=5;bb_gen_state.steps=16;bb_gen_state.accents=2;
 bb_gen_state.density=70;bb_gen_state.accent=30;bb_gen_state.anchor=100;bb_gen_state.slide=25;
}
static int bb_gen_synth_snapshot(bb_pattern_data *out){
 bb_commands *q=&bb_sound.commands;uintptr_t token=q->enter?q->enter(q->lock_context):0;
 int ok=bb_commands_idle(q)&&!bb_sound.patterns.pending[0].ready;
 if(ok){bb_gen_state.synth_source=bb_sound.patterns.status[0].applied;out->length=bb_sound.core.synths[0].length;
  memcpy(out->notes,bb_sound.core.synths[0].notes,sizeof out->notes);
  memcpy(out->expression,bb_sound.patterns.expression[0],sizeof out->expression);
 }
 if(q->leave)q->leave(q->lock_context,token);return ok;
}
static int bb_gen_submit_rich(unsigned m,const bb_phrase *p){
 uint32_t playable=bb_audio_phrase_source_voices(&bb_sound,m);
 if(!bb_phrase_index_prepare(p,playable,&bb_gen_state.index)||
    !bb_audio_phrase_preflight_playable(&bb_gen_state.index,playable))return 0;
 return bb_audio_phrase_submit_seamless(&bb_sound,m,&bb_gen_state.index,&bb_gen_state.drum[m].ticket);
}
static void bb_gen_on_evolve(unsigned action){
 if(action==BB_EV_CAPTURE)bb_gen_state.capture_pending=1;
 if(action==BB_EV_HOME)bb_gen_state.return_pending=7u;
}
static void bb_gen_mode_next(void){
 bb_gen_state.mode=(uint8_t)((bb_gen_state.mode+1u)%3u);bb_dirty=1;
 bb_say(bb_gen_state.mode==1?BB_TX(TX_EUCLID_EDIT_APPLY):bb_gen_state.mode==2?BB_TX(TX_RANDOM_EDIT_APPLY):BB_TX(TX_EVOLVE_EDIT_CAPTURE));
}
static void bb_gen_knob(unsigned k,int32_t delta){
 int d=bb_limit(delta,-100,100);if(!bb_gen_state.mode)return;
 if(bb_gen_state.mode==1){
  if(k==0)bb_gen_state.hits=(uint8_t)bb_limit((int)bb_gen_state.hits+d,0,bb_gen_state.steps);
  if(k==1){bb_gen_state.steps=(uint8_t)bb_limit((int)bb_gen_state.steps+d,1,64);if(bb_gen_state.hits>bb_gen_state.steps)bb_gen_state.hits=bb_gen_state.steps;if(bb_gen_state.accents>bb_gen_state.steps)bb_gen_state.accents=bb_gen_state.steps;}
  if(k==2)bb_gen_state.rotation=(uint8_t)bb_limit((int)bb_gen_state.rotation+d,0,bb_gen_state.steps-1);
  if(k==3)bb_gen_state.accents=(uint8_t)bb_limit((int)bb_gen_state.accents+d,0,bb_gen_state.steps);
 }else{
  if(k==0)bb_gen_state.density=(uint8_t)bb_limit((int)bb_gen_state.density+d,0,100);
  if(k==1)bb_gen_state.accent=(uint8_t)bb_limit((int)bb_gen_state.accent+d,0,100);
  if(k==2){uint8_t *v=bb_face.requested.page==BB_DRUMS?&bb_gen_state.anchor:&bb_gen_state.slide;*v=(uint8_t)bb_limit((int)*v+d,0,100);}
  if(k==3)bb_gen_state.seed=(uint32_t)bb_limit((int)bb_gen_state.seed+d,1,9999);
 }
 bb_dirty=1;
}
static void bb_gen_apply(void){
 if(!bb_gen_state.mode)return;
 if(!bb_evo_view.ready){bb_say(BB_TX(TX_WAIT_FOR_CAPTURE));return;}
 if(bb_face.requested.page==BB_DRUMS){
  unsigned m=bb_face.machine;if(m>=2){bb_say(BB_TX(TX_CHOOSE_KIT_A_OR_B));return;}
  bb_phrase_view *v=&bb_gen_state.drum[m].view;
  if(!bb_audio_phrase_view_read(&bb_sound,m,v)||!v->valid){bb_say(BB_TX(TX_LOAD_A_SOURCE_PATTERN_FIRST));return;}
  const bb_phrase *home=bb_gen_state.drum[m].evolve.ready?&bb_gen_state.drum[m].evolve.home:&v->phrase;
  int ok;
  if(bb_gen_state.mode==1){
   uint64_t hit=bb_gen_euclid(bb_gen_state.hits,bb_gen_state.steps,bb_gen_state.rotation,home->length);
   uint64_t acc=bb_gen_euclid(bb_gen_state.accents,bb_gen_state.steps,bb_gen_state.rotation,home->length);
   ok=bb_gen_lane_write(home,&bb_gen_state.phrase,bb_gen_state.voice,hit,acc,1,bb_evo_view.settings.locked[m]);
  }else ok=bb_gen_phrase_random(home,&bb_gen_state.phrase,bb_gen_state.density,bb_gen_state.accent,bb_gen_state.anchor,bb_gen_seed(bb_gen_state.seed,m),bb_evo_view.settings.locked[m]);
  if(!ok||!bb_gen_submit_rich(m,&bb_gen_state.phrase)){bb_say(BB_TX(TX_BUSY_OR_PATTERN_REFUSED));return;}
  bb_gen_state.fb_kind=(uint8_t)(1u+m);bb_gen_state.fb_ticket=bb_gen_state.drum[m].ticket;
 }else{
  if(!bb_gen_state.synth_home_valid){if(!bb_gen_synth_snapshot(&bb_gen_state.dillo_home)){bb_say(BB_TX(TX_BUSY_RETRY_2));return;}bb_gen_state.synth_home_valid=1;}
  bb_gen_state.line=bb_gen_state.dillo_home;
  bb_gen_dillo_args g={bb_gen_euclid(bb_gen_state.hits,bb_gen_state.steps,bb_gen_state.rotation,bb_gen_state.line.length),
   bb_gen_euclid(bb_gen_state.accents,bb_gen_state.steps,bb_gen_state.rotation,bb_gen_state.line.length),
   bb_evo_view.settings.locked[BB_MACHINES],bb_gen_seed(bb_gen_state.seed,BB_MACHINES),
   bb_gen_state.mode==2,bb_gen_state.mode==1,bb_gen_state.density,bb_gen_state.accent,bb_gen_state.slide};
  if(!bb_gen_dillo(bb_gen_state.dillo_home.notes,bb_gen_state.line.length,bb_gen_state.line.notes,&bb_evo_view.settings,&g)||
    !bb_audio_pattern_submit(&bb_sound,0,&bb_gen_state.line,16,&bb_gen_state.synth_ticket)){bb_say(BB_TX(TX_BUSY_OR_PATTERN_REFUSED));return;}
  bb_gen_state.fb_kind=3;bb_gen_state.fb_ticket=bb_gen_state.synth_ticket;
 }
 bb_dirty=1;bb_say(BB_TX(TX_VARIATION_QUEUED_B2_RETURN));
}
static void bb_gen_poll(void){
 /* Main has no permission to read mutable owner state without its guard. */
 bb_commands *q=&bb_sound.commands;uint8_t step[2],valid[2],running;uint32_t pending[2],synth_applied;
 uintptr_t token=q->enter?q->enter(q->lock_context):0;
 if(!bb_commands_idle(q)){if(q->leave)q->leave(q->lock_context,token);return;}
 running=bb_sound.core.running;synth_applied=bb_sound.patterns.status[0].applied;
 for(unsigned m=0;m<2;m++){step[m]=bb_sound.rich.player[m].step;valid[m]=bb_sound.rich.valid[m];pending[m]=bb_sound.rich.status[m].queued;}
 if(q->leave)q->leave(q->lock_context,token);
 if(bb_gen_state.synth_home_valid&&synth_applied&&synth_applied!=bb_gen_state.synth_ticket&&synth_applied!=bb_gen_state.synth_source)bb_gen_state.synth_home_valid=0;
 for(unsigned m=0;m<2;m++){
  if(!valid[m]){memset(&bb_gen_state.drum[m],0,sizeof bb_gen_state.drum[m]);bb_gen_state.return_pending&=(uint8_t)~(1u<<m);continue;}
  bb_phrase_view *v=&bb_gen_state.drum[m].view;uint32_t before=v->applied;
  if(!bb_audio_phrase_view_read(&bb_sound,m,v)||!v->valid)continue;
  /* D4: QUEUED -> APPLIED when the owner reports this APPLY's own ticket as applied (a later or refused ticket never matches) */
  if(bb_gen_state.fb_kind==1u+m&&v->applied==bb_gen_state.fb_ticket){bb_gen_state.fb_kind=0;bb_dirty=1;bb_say(BB_TX(TX_VARIATION_APPLIED));}
  if(bb_gen_state.capture_pending||!bb_gen_state.drum[m].evolve.ready||(v->applied!=before&&v->applied!=bb_gen_state.drum[m].ticket)){
   bb_gen_rich_capture(&bb_gen_state.drum[m].evolve,&v->phrase,bb_gen_state.seed,m);bb_gen_state.drum[m].last_step=step[m];
  }
  if((bb_gen_state.return_pending&(1u<<m))&&!pending[m]){
   if(bb_gen_submit_rich(m,&bb_gen_state.drum[m].evolve.home)){bb_gen_rich_capture(&bb_gen_state.drum[m].evolve,&bb_gen_state.drum[m].evolve.home,bb_gen_state.seed,m);bb_gen_state.return_pending&=(uint8_t)~(1u<<m);}
  }else if(running&&!pending[m]&&bb_evo_view.ready&&bb_evo_view.enabled&&!bb_evo_view.frozen&&!bb_evo_view.conflict&&!bb_evo_view.pending&&step[m]<bb_gen_state.drum[m].last_step){
   if(bb_gen_rich_pass(&bb_gen_state.drum[m].evolve,&v->phrase,&bb_gen_state.phrase,&bb_evo_view.settings,m))
    (void)bb_gen_submit_rich(m,&bb_gen_state.phrase);
  }
  bb_gen_state.drum[m].last_step=step[m];
 }
 if(bb_gen_state.fb_kind==3u&&synth_applied==bb_gen_state.fb_ticket){bb_gen_state.fb_kind=0;bb_dirty=1;bb_say(BB_TX(TX_VARIATION_APPLIED));}
 if(bb_gen_state.return_pending&4u){
  if(!bb_gen_state.synth_home_valid||bb_audio_pattern_submit(&bb_sound,0,&bb_gen_state.dillo_home,16,&bb_gen_state.synth_ticket))bb_gen_state.return_pending&=(uint8_t)~4u;
 }
 if(bb_gen_state.capture_pending){bb_gen_state.synth_home_valid=0;bb_gen_state.capture_pending=0;}
}
static void bb_gen_draw(void);

#else
static void bb_gen_reset(void){}
static void bb_gen_on_evolve(unsigned action){(void)action;}
static void bb_gen_poll(void){}
#endif
