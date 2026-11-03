/* SPDX-License-Identifier: GPL-3.0-only */
#if BB_WITH_DILLO_KIT_VIEW
#include "bb/bb_dillo_activity.h"
static uint8_t bb_dillo_kit_page;
static int bb_dillo_kit_input(uint32_t pressed,uint32_t notes){
 if(bb_dillo_kit_page){
  if(bb_nav.page!=BB_NAV_PLAY||!bb_nav_is_drum()||bb_arr_page||bb_midi_page||
     (pressed&((1u<<panel.btn[B_HOME])|(1u<<panel.btn[B_GLO]))))bb_dillo_kit_page=0;
  return 0; /* normal navigation retains every instrument/transport control */
 }
 (void)notes;
 return 0;
}
/* 2.1 P1: GLOBAL white 5 (a bb_nav role, lit with a drum layer in focus) opens the view */
static void bb_dillo_kit_open(void){
 if(!bb_nav_is_drum())return;
 bb_nav_force(BB_NAV_PLAY);bb_dillo_kit_page=1;bb_dirty=1;bb_say(BB_TX(TX_DILLO_KIT_TRIGGER_ACTIVITY));
}
#else
static int bb_dillo_kit_input(uint32_t pressed,uint32_t notes){(void)pressed;(void)notes;return 0;}
static void bb_dillo_kit_open(void){}
#endif

/* 2.1 P1: the hook bb_nav_firmware.inc reaches SONG, MIDI and the Dillo kit view through (see BB_P1_* there) */
static int bb_p1_ops(unsigned op,unsigned a,int b){
 switch(op){
 case BB_P1_FACTS:return bb_arr_nav_facts();
 case BB_P1_SYNC:{
  unsigned page=a&255u,bank=(a>>8)&255u,was=(a>>16)&255u;
  if(was==BB_NAV_SONG&&page!=BB_NAV_SONG)bb_arr_left();      /* a waiting delete ends with the page */
  bb_arr_page=page==BB_NAV_SONG;bb_arr_source_page=bb_arr_page&&bank==BB_NAV_SONG_ADD;
  bb_midi_page=(uint8_t)(page==BB_NAV_GLOBAL?bank:0u);
  return 0;
 }
 case BB_P1_KEY:bb_arr_key(a,(unsigned)b);return 0;
 case BB_P1_EDIT:bb_arr_edit();return 0;
 case BB_P1_RECORD:bb_arr_record();return 0;
 case BB_P1_ARR_KNOB:bb_arr_knob(a,b);return 0;
 case BB_P1_BANK:bb_arr_source_move(b,0);return 0;
 case BB_P1_MIDI_KNOB:bb_midi_ui_knob(a,b);return 0;
 case BB_P1_SAVE:bb_midi_ui_save();return 0;
 case BB_P1_OPEN_SONG:bb_arr_open();return 0;
 case BB_P1_DILLO_VIEW:bb_dillo_kit_open();return 0;
 case BB_P1_SONG_ARMED:return bb_arr_ready&&bb_arr_workspace.controller.armed;   /* 2.1 P3b: CONDUCT keys off */
 default:return 0;
 }
}
