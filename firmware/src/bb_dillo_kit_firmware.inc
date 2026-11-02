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
 if(!bb_arr_page&&!bb_midi_page&&bb_nav.page==BB_NAV_GLOBAL&&bb_nav_is_drum()&&
    (bb_nav_keys_logical(notes)&(1u<<4))){
  bb_nav_force(BB_NAV_PLAY);bb_dillo_kit_page=1;bb_dirty=1;bb_say(BB_TX(TX_DILLO_KIT_TRIGGER_ACTIVITY));return 1;
 }
 return 0;
}
#else
static int bb_dillo_kit_input(uint32_t pressed,uint32_t notes){(void)pressed;(void)notes;return 0;}
#endif
