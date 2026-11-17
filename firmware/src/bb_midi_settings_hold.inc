/* SPDX-License-Identifier: GPL-3.0-only
 * Existing global settings journal, protected by the existing acknowledged
 * session+kit audio hold. No new storage object or automatic boot write. */
#if FELUCCA_FLASH
static struct {uint32_t started;uint8_t active;} bb_global_settings_hold;
static int bb_midi_global_settings_holding(void){return bb_global_settings_hold.active;}
static void bb_global_settings_release(void){
 if(!bb_global_settings_hold.active)return;
 __asm__ volatile("" ::: "memory");bb_session_hold_request=bb_kit_hold_request=0;
 bb_global_settings_hold.active=0;
}
static int bb_global_settings_acquire(void){
 if(!bb_global_settings_hold.active){
  if(bb_session_busy()||bb_kit_hold_request||bb_session_hold_request||bb_asset_hold_request||
     !bb_session_worker_ready(0)||bb_c1_live.busy||bb_session_usb_recording())return 0;
  uintptr_t t=bb_target_enter(0);
  if(bb_sound.core.running||!bb_commands_idle(&bb_sound.commands)||
     !bb_session_patterns_idle()||!bb_session_families_match()){
   bb_target_leave(0,t);return 0;
  }
  bb_global_settings_hold.active=1;bb_global_settings_hold.started=fm1_ms;
  bb_session_hold_ack=bb_kit_hold_ack=0;
  __asm__ volatile("" ::: "memory");bb_session_hold_request=bb_kit_hold_request=1;
  bb_target_leave(0,t);return 0;
 }
 if(!bb_session_hold_ack||!bb_kit_hold_ack){
  if((uint32_t)(fm1_ms-bb_global_settings_hold.started)<1000u)return 0;
  bb_global_settings_release();return -1;
 }
 __asm__ volatile("" ::: "memory");
 if(bb_session_usb_recording()||!bb_session_worker_ready(0)||bb_c1_live.busy||
    !bb_commands_idle(&bb_sound.commands)||
    !bb_session_patterns_idle()||!bb_session_families_match()||bb_sound.core.running){
  bb_global_settings_release();return -1;
 }
 return 1;
}
#else
static int bb_midi_global_settings_holding(void){return 0;}
#endif
