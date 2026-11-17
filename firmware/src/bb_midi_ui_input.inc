/* SPDX-License-Identifier: GPL-3.0-only
 * GLOBAL > MIDI: main-loop producer. Four channel knobs or clock controls.
 * The existing transport/navigation retain HOME/PLAY; no audio-state writes. */
/* 2.1 P1: MIDI CHANNELS and MIDI CLOCK are GLOBAL's banks in bb_nav (one page variable): bb_midi_page (1 channels,
 * 2 clock) is a read-only mirror written by bb_nav_project() only */
static uint8_t bb_midi_page,bb_midi_save_wait;
static unsigned bb_midi_next_channel(const bb_midi_compat_settings *c,unsigned index,int delta){
 unsigned ch=index<2?c->drum_channel[index]:c->synth_channel[index-2];
 int n=bb_limit(delta,-16,16),dir=n<0?-1:1;unsigned count=(unsigned)(n<0?-n:n);
 while(count--){
  for(unsigned attempt=0;attempt<17;attempt++){
   ch=(unsigned)((int)ch+dir+17)%17u;
   bb_midi_compat_settings next=*c;
   if(index<2)next.drum_channel[index]=(uint8_t)ch;else next.synth_channel[index-2]=(uint8_t)ch;
   if(bb_midi_compat_settings_valid(&next))break;
  }
 }
 return ch;
}
static void bb_midi_ui_knob(unsigned k,int delta){
 bb_midi_compat_settings c=bb_midi_compat_config;
 if(bb_midi_page==1){
  unsigned ch=bb_midi_next_channel(&c,k,delta);
  if(k<2)c.drum_channel[k]=(uint8_t)ch;else c.synth_channel[k-2]=(uint8_t)ch;
 }else if(bb_midi_page==2){
  if(k==0)c.clock_source=(uint8_t)bb_limit((int)c.clock_source+bb_limit(delta,-1,1),BB_MIDI_CLOCK_USB,BB_MIDI_CLOCK_TRS);
  if(k==1)c.tempo_follow=(uint8_t)bb_limit((int)c.tempo_follow+bb_limit(delta,-1,1),0,1);
  if(k==2)c.transport=(uint8_t)bb_limit((int)c.transport+bb_limit(delta,-1,1),0,1);
  if(k==3)c.clock_out=(uint8_t)bb_limit((int)c.clock_out+bb_limit(delta,-1,1),0,1);
 }
 if(!memcmp(&c,&bb_midi_compat_config,sizeof c))return;
 if(bb_midi_configure(&c)){bb_dirty=1;bb_say(BB_TX(TX_MIDI_CHANGED_EDIT_SAVE_GLOBALS));}
 else bb_say(BB_TX(TX_STOP_WAIT_TO_CHANGE_CLOCK_OUT));
}
static void bb_midi_ui_save(void){
#if FELUCCA_FLASH
 if(!flash_ok){bb_say("STORAGE UNAVAILABLE");return;}
 if(bb_face.requested.running||bb_session_busy()||bb_asset_hold_request||bb_session_usb_recording()){
  bb_say(BB_TX(TX_STOP_WAIT_TO_SAVE_GLOBALS));return;
 }
 bb_midi_save_wait=1;settings_save();bb_say(BB_TX(TX_GLOBAL_SETTINGS_SAVE_QUEUED));
#endif
}
/* 2.1 P1: the MIDI banks' keys, buttons and knobs are bb_nav roles (white 1 / 2 the banks, black 1 back, K1-K4,
 * EDIT or SAVE: save, HOME and GLO: back); this keeps only the save's progress notice. Never intercepts input. */
static int bb_midi_ui_input(uint32_t pressed,uint32_t notes){
 (void)pressed;(void)notes;
#if FELUCCA_FLASH
 if(bb_midi_save_wait&&!persist_pending){bb_midi_save_wait=0;bb_say("GLOBAL SETTINGS SAVED");}
 else if(bb_midi_save_wait&&persist_pending==2)bb_say(BB_TX(TX_GLOBAL_SAVE_RETRY_WAIT));
#endif
 return 0;
}
