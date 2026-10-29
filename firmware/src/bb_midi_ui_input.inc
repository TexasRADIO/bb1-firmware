/* SPDX-License-Identifier: GPL-3.0-only
 * GLOBAL > MIDI: main-loop producer. Four channel knobs or clock controls.
 * The existing transport/navigation retain HOME/PLAY; no audio-state writes. */
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
  if(k==3){bb_say("CLOCK OUT: STAGE 2 / OFF");return;}
 }
 if(!memcmp(&c,&bb_midi_compat_config,sizeof c))return;
 if(bb_midi_configure(&c)){bb_dirty=1;bb_say("MIDI CHANGED / EDIT: SAVE GLOBALS");}
}
static void bb_midi_ui_save(void){
#if FELUCCA_FLASH
 if(!flash_ok){bb_say("STORAGE UNAVAILABLE");return;}
 if(bb_face.requested.running||bb_session_busy()||bb_asset_hold_request||bb_session_usb_recording()){
  bb_say("STOP / WAIT TO SAVE GLOBALS");return;
 }
 bb_midi_save_wait=1;settings_save();bb_say("GLOBAL SETTINGS SAVE QUEUED");
#endif
}
static int bb_midi_ui_input(uint32_t pressed,uint32_t notes){
#if FELUCCA_FLASH
 if(bb_midi_save_wait&&!persist_pending){bb_midi_save_wait=0;bb_say("GLOBAL SETTINGS SAVED");}
 else if(bb_midi_save_wait&&persist_pending==2)bb_say("GLOBAL SAVE RETRY / WAIT");
#endif
 if(bb_nav.page!=BB_NAV_GLOBAL){bb_midi_page=0;return 0;}
 uint32_t keys=bb_nav_keys_logical(notes);
 if(!bb_midi_page){if(!(keys&1u))return 0;bb_midi_page=1;bb_dirty=1;}
 if((pressed&(1u<<panel.btn[B_HOME]))||(pressed&(1u<<panel.btn[B_GLO]))){bb_midi_page=0;bb_dirty=1;return 0;}
 if(keys&1u){bb_midi_page=1;bb_dirty=1;}
 if(keys&2u){bb_midi_page=2;bb_dirty=1;}
 if(keys&(1u<<16)){bb_midi_page=0;bb_dirty=1;return 1;}
 for(unsigned k=0;k<4;k++){int d=bb_read_encoder(EN_K1+k);if(d)bb_midi_ui_knob(k,d);}
 if(pressed&((1u<<panel.btn[B_EDIT])|(1u<<panel.btn[B_SAVE])))bb_midi_ui_save();
 if(pressed&(1u<<panel.btn[B_PLAY]))bb_send(BB_CMD_TRANSPORT,!bb_face.requested.running);
 /* Big encoders have no MIDI-page job; avoid a hidden instrument/kit edit. */
 (void)bb_read_encoder(EN_SELECT);(void)bb_read_encoder(EN_ALGO);(void)bb_read_encoder(EN_PRESET);
 return 1;
}
