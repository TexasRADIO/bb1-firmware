/* SPDX-License-Identifier: GPL-3.0-only */
/* Main-loop project-file owner. Optional private 2.0 path. No flash/file driver;
 * callers supply bounded IO and must get named overwrite + Undo-reset consent.
 * Uses the real session/kit pause acknowledgement, never synthesizes an ack. */
#if BB_WITH_PROJECT_FILES
#include "bb/bb_project.h"
#if !defined(BB_SAMPLE_DUAL_TRIAL) || !BB_WITH_LIVE_CAPTURE
#error "Project owner currently requires paired scenes and live-capture ownership"
#endif
/* Resident identities are defined by the later sample-boot include. */
static uint32_t bb_sample_library_header_crc,bb_sample_library_payload_crc;
static uint32_t bb_project_loaded_edits;
static uint8_t bb_project_has_file;
static bb_arr_sources bb_project_sources;
static bb_arrangement bb_project_model __attribute__((section(".pool")));
static uint8_t bb_project_nested[BB_PROJECT_SCENE_MAX] __attribute__((section(".pool")));
enum {BB_PROJECT_OWNER_BUSY=64,BB_PROJECT_OWNER_PENDING,BB_PROJECT_OWNER_HELD};
static struct {uint32_t started;uint8_t phase,error,staged;} bb_project_owner;
static void bb_project_owner_scene_replaced(void){bb_project_has_file=0;bb_project_loaded_edits=0;}
/* "Edited since file load" is an event latch, not a bytewise content hash.
 * Accepted queued manual recalls remain edited even if later cancelled.
 * Saturation cannot make subsequent edits falsely clean. */
static int bb_project_owner_dirty(void){
 if(bb_project_owner.phase==3)return 1;
 uintptr_t token=bb_target_enter(0);uint32_t edits=bb_sound.commands.project_edits;bb_target_leave(0,token);
 return !bb_project_has_file||edits==UINT32_MAX||edits!=bb_project_loaded_edits||bb_arr_dirty(&bb_arr_workspace.model);
}

void bb_project_owner_release(void){
 if(!bb_project_owner.phase||bb_project_owner.phase==3)return;
 uintptr_t token=bb_target_enter(0);
#if BB_MIDI_INPUT
 bb_midi_project_release_owned();
#endif
 __asm__ volatile("" ::: "memory");
 bb_asset_hold_request=bb_session_hold_request=bb_kit_hold_request=0;
 bb_project_owner.phase=bb_project_owner.staged=0;bb_dirty=1;
 bb_target_leave(0,token);
}
static int bb_project_owner_conditions(void){
 return !bb_count_busy()&&bb_arr_ready&&!bb_record.phase&&!bb_sound.capture.armed&&!bb_sound.capture.count&&
  !bb_session_busy()&&!bb_session_usb_recording()&&bb_session_worker_ready(1)&&!bb_c1_live.busy&&
  !bb_sound.core.running&&bb_commands_idle(&bb_sound.commands)&&
  bb_session_patterns_idle()&&bb_session_families_match()&&
  bb_sound.section.phase==BB_SECTION_FREE;
}
static void bb_project_owner_poll(void){
 if(!bb_project_owner.phase||bb_project_owner.phase==3)return;
 if((uint32_t)(fm1_ms-bb_project_owner.started)>=1000u){
  bb_project_owner.error=BB_PROJECT_IO;bb_project_owner_release();return;
 }
 if(bb_project_owner.phase==1&&bb_asset_hold_ack&&bb_session_hold_ack&&bb_kit_hold_ack){
  __asm__ volatile("" ::: "memory");
  if(!bb_project_owner_conditions()||!pab_target_session_ready(&bb_sound)){
   bb_project_owner.error=BB_PROJECT_OWNER_BUSY;bb_project_owner_release();return;
  }
  bb_project_owner.phase=2;
 }
}
int bb_project_owner_acquire(void){
 if(bb_project_owner.phase==3)return BB_PROJECT_OWNER_HELD;
 if(bb_project_owner.phase){bb_project_owner_poll();return bb_project_owner.phase==2?0:
   bb_project_owner.phase?BB_PROJECT_OWNER_PENDING:bb_project_owner.error;}
 uintptr_t token=bb_target_enter(0);
 if(bb_asset_hold_request||bb_session_hold_request||bb_kit_hold_request||!bb_project_owner_conditions()){
  bb_target_leave(0,token);return BB_PROJECT_OWNER_BUSY;
 }
 memset(&bb_project_owner,0,sizeof bb_project_owner);bb_project_owner.phase=1;bb_project_owner.started=fm1_ms;
 bb_asset_hold_ack=bb_session_hold_ack=bb_kit_hold_ack=0;
 __asm__ volatile("" ::: "memory");bb_asset_hold_request=bb_session_hold_request=bb_kit_hold_request=1;
 bb_target_leave(0,token);return BB_PROJECT_OWNER_PENDING;
}
/* A callback may consume wall time without returning to the main loop.
 * Recheck the lease before each owner operation and after file admission;
 * only pre-mutation expiry may release the pause. A reset already in flight
 * must finish or remain held, never be interrupted into partial live state. */
static int bb_project_owner_ready(void){
 bb_project_owner_poll();
 if(bb_project_owner.phase==3)return BB_PROJECT_OWNER_HELD;
 if(bb_project_owner.phase!=2)return bb_project_owner.error?bb_project_owner.error:BB_PROJECT_OWNER_BUSY;
 return pab_target_session_ready(&bb_sound)&&bb_project_owner_conditions()?0:BB_PROJECT_OWNER_BUSY;
}
static bb_project_assets bb_project_owner_assets(void){
 bb_project_assets a={bb_sample_library_header_crc,bb_sample_library_payload_crc};return a;
}
static int bb_project_owner_check(void *context,const void *data,size_t bytes){
 (void)context;return bb_target_scene_decode(data,bytes);
}
int bb_project_owner_snapshot(size_t *bytes){
 int ready=bb_project_owner_ready();if(ready)return ready;
 size_t n=bb_pair_session_capture()?bb_pair_session_encode():0;
 if(!n)return BB_PROJECT_SCENE;*bytes=n;return 0;
}
int bb_project_owner_stage(bb_project_read_at read,void *context,size_t bytes){
 bb_project_owner.staged=0;
 int ready=bb_project_owner_ready();if(ready)return ready;
 const void *scene=NULL;size_t n=0;
 int r=bb_project_read_stream_staged(read,context,bytes,&bb_factory_catalogue,bb_project_owner_assets(),
  bb_project_owner_check,NULL,&bb_project_model,&bb_project_sources,bb_project_nested,sizeof bb_project_nested,&scene,&n);
 if(r)return r;
 r=bb_project_owner_ready();if(r)return r;
 /* Admit EVERY arranged section against the incoming scene's kit identities,
  * before any live engine/model mutation. This scratch is UI-only while held. */
 for(unsigned i=0;i<BB_ARR_SECTIONS;i++)if(bb_project_model.sections[i].id){
  if(bb_arr_prepare_section(&bb_project_model,&bb_project_sources,bb_project_model.sections[i].id,1,&bb_arr_workspace.preview))return BB_PROJECT_SOURCE;
  for(unsigned m=0;m<2;m++){
   const bb_sample_scene_identity *id=&bb_session_memory.pair.snapshot.identities[m];
#if BB_WITH_USER_KITS
   uint32_t voices=
#if BB_PAIR_SLOOP
    id->kind==3?bb_audio_sample_source_voices(&bb_sound,BB_SLOOP_ID_FIRST+id->kit):
#endif
    id->kind==2?bb_audio_sample_source_voices(&bb_sound,BB_SAMPLE_USER_ID):
    id->kind?(BB_RICH_808_VOICES|bb_audio_aux_capabilities(&bb_sound)):
    bb_audio_sample_source_voices(&bb_sound,id->kit);
#else
   uint32_t voices=id->kind?(BB_RICH_808_VOICES|bb_audio_aux_capabilities(&bb_sound)):
    bb_audio_sample_source_voices(&bb_sound,id->kit);
#endif
   if(!bb_audio_phrase_preflight_playable(&bb_arr_workspace.preview.drum[m],voices))return BB_PROJECT_SOURCE;
  }
 }
 r=bb_project_owner_ready();if(r)return r;
 bb_project_owner.staged=1;return 0;
}
int bb_project_owner_commit(void){
 int ready=bb_project_owner_ready();if(ready)return ready;
 if(!bb_project_owner.staged)return BB_PROJECT_OWNER_BUSY;
 int r=bb_pair_session_restore();bb_project_owner.staged=0;
 if(r==BB_SINGLE_REJECTED)return BB_PROJECT_SCENE;
 if(r!=BB_SINGLE_OK){bb_project_owner.phase=3;return BB_PROJECT_OWNER_HELD;}
 /* Owners remain paused. Prevalidated model copy and controller reset cannot
  * expose a partial project; no fallible audio work follows publication. */
 bb_scene_mirror_reset();
 bb_arr_workspace.model=bb_project_model;bb_arr_workspace.sources=bb_project_sources;
 if(bb_arr_controller_init(&bb_arr_workspace.controller,&bb_arr_workspace.model,&bb_arr_workspace.sources)){
  bb_project_owner.phase=3;return BB_PROJECT_OWNER_HELD;
 }
 bb_project_loaded_edits=bb_sound.commands.project_edits;bb_project_has_file=1;
 bb_ticks=0;bb_visual_peak=0;memset(bb_playhead,0,sizeof bb_playhead);memset(bb_synth_playhead,0,sizeof bb_synth_playhead);
 bb_nav_row=0;bb_nav_force(BB_NAV_GLOBAL);bb_arr_page=1;bb_arr_source_page=0;bb_dirty=1;
 bb_project_owner_release();bb_say(BB_TX(TX_PROJECT_LOADED_FRESH_UNDO));return 0;
}
#endif
