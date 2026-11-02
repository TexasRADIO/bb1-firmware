/* SPDX-License-Identifier: GPL-3.0-only
 * MIDI uses the existing shared v3 KNOBS frame, isolated from UI worker edits. */
static char bb_midi_value[4][8];
static int bb_midi_ui_draw(void){
 if(bb_nav.page!=BB_NAV_GLOBAL||bb_help_visible)return 0;
 bb_ui_in *in=&bb_v3_in;
 if(bb_v3_shown&&(int32_t)(fm1_ms-bb_v3_next)<0)return 1;
 bb_v3_next=fm1_ms+33u;
 if(ux.light||ux.mono){ux.light=0;ux.mono=0;ux.gen++;}
 if(!bb_v3_shown){bb_v3_init(&bb_v3,fm1_ms,bb_face.requested.running);bb_v3_invalidate(&bb_v3);}
 memset(in,0,sizeof *in);bb_v3_common(in,BB_UI_KNOBS);
 if(!bb_midi_page){
  bb_v3_knobs(in);in->subtitle=BB_TX(TX_WHITE_1_MIDI_WHITE_4_ARRANGE);
 }else{
  static const char *const channel[]={"KIT A CH","KIT B CH","BASS CH","LEAD CH"};
  static const char *const clock[]={"CLOCK IN","FOLLOW","TRANSPORT","CLOCK OUT"};
  in->page_name=BB_TX(TX_GLOBAL_MIDI);in->title=bb_midi_page==1?BB_TX(TX_MIDI_CHANNELS):"MIDI CLOCK";
  in->subtitle=bb_midi_page==1?BB_TX(TX_WHITE_2_CLOCK_EDIT_SAVE):bb_midi_compat_config.clock_out&&bb_midi_compat_config.clock_source==BB_MIDI_CLOCK_USB?"USB REALTIME IN OFF / OUT ON":"USB OUT / STOP TO EDIT / SAVE";
  for(unsigned k=0;k<4;k++){
   unsigned value=0;
   if(bb_midi_page==1){
    value=k<2?bb_midi_compat_config.drum_channel[k]:bb_midi_compat_config.synth_channel[k-2];
    if(!value)bb_v3_copy(bb_midi_value[k],sizeof bb_midi_value[k],"OFF");
    else fmt_int(bb_midi_value[k],value);
   }else{
    const char *t;
    if(!k){value=bb_midi_compat_config.clock_source==BB_MIDI_CLOCK_TRS;t=value?"TRS":"USB";}
    else if(k==1){value=bb_midi_compat_config.tempo_follow;t=value?"ON":"OFF";}
    else if(k==2){value=bb_midi_compat_config.transport;t=value?"ON":"OFF";}
    else {value=bb_midi_compat_config.clock_out;t=value?"USB ON":"OFF";}
    if(bb_midi_compat_config.clock_out&&bb_midi_compat_config.clock_source==BB_MIDI_CLOCK_USB&&k<3){
     value=0;t=k?"IGNORED":"USB OFF";
    }
    bb_v3_copy(bb_midi_value[k],sizeof bb_midi_value[k],t);
   }
   in->knob[k].label=bb_midi_page==1?(k==3&&!bb_midi_compat_synth(1)?"LEAD RSV":channel[k]):clock[k];in->knob[k].text=bb_midi_value[k];
   in->knob[k].bar=(uint8_t)(bb_midi_page==1?value*254u/16u:value?254u:0u);
   in->knob[k].flags=0; /* OFF is a visible read-only value, not an unused column. */
  }
  /* Explicit channels/noOmni. Slot2 is reserved until the Duo owner lands. */
  in->patch=bb_midi_page==1?(bb_midi_compat_synth(1)?"EXPLICIT CHANNELS / NO OMNI":BB_TX(TX_OMNI_OFF_LEAD_RESERVED)):"USB OUT / NO USB THRU";
 }
 bb_v3_frame(&bb_v3,in);bb_v3_shown=1;bb_dirty=0;return 1;
}
