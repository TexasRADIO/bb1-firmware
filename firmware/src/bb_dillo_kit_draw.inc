/* SPDX-License-Identifier: GPL-3.0-only */
#if BB_WITH_DILLO_KIT_VIEW
#include "bb/bb_dillo_kit_draw.c"
static bb_dillo_activity bb_dillo_motion[2];
static bb_dillo_strikes bb_dillo_snapshot;
static uint32_t bb_dillo_generation[2],bb_dillo_next;
static uint8_t bb_dillo_was_visible,bb_dillo_running[2];
static void bb_dillo_line_draw(void *ctx,int x,int y,int xx,int yy){
 (void)ctx;cv_line_t(x,y,xx,yy,BB_ORANGE,1);
}
static int bb_dillo_kit_draw_page(void){
 unsigned recording=0;
#if BB_WITH_LIVE_CAPTURE
 recording=bb_record.phase==BB_RECORD_COUNT||bb_record.phase==BB_RECORD_LIVE;
#endif
 if(((!bb_dillo_kit_page||bb_nav.page!=BB_NAV_PLAY||!bb_nav_is_drum())&&!recording)||bb_help_visible){
  if(bb_dillo_was_visible){bb_dillo_was_visible=0;bb_dirty=1;bb_v3_invalidate(&bb_v3);}
  return 0;
 }
 if(bb_dillo_was_visible&&(int32_t)(fm1_ms-bb_dillo_next)<0)return 1;
 bb_dillo_next=fm1_ms+50u; /* bounded 20Hz visual budget, never audio scheduling */
 unsigned machine=bb_face.machine;
#if BB_WITH_LIVE_CAPTURE
 if(recording)machine=bb_record.target;
#endif
 if(machine>=2)return 0;
 if(!bb_audio_dillo_strikes_read(&bb_sound,machine,&bb_dillo_snapshot))return 1;
 bb_dillo_activity *motion=&bb_dillo_motion[machine];
 if(bb_dillo_was_visible!=machine+1u||(bb_dillo_running[machine]&&!bb_dillo_snapshot.running))
  bb_dillo_activity_reset(motion);
 else if(bb_dillo_generation[machine]!=bb_dillo_snapshot.generation)
  memset(motion->pulses,0,sizeof motion->pulses); /* retain monotonic counters: keep first new strike */
 bb_dillo_running[machine]=bb_dillo_snapshot.running;
 bb_dillo_generation[machine]=bb_dillo_snapshot.generation;
 bb_dillo_activity_read(motion,bb_dillo_snapshot.strikes,fm1_ms);bb_dillo_was_visible=(uint8_t)(machine+1u);
 /* The retained canvas is only 124 rows. Paint both strips explicitly;
  * never leave the GLOBAL page's upper half behind the activity panel. */
 cv_begin(240,120,BB_BG);
 cv_text(6,5,&AF_M,recording?(machine?BB_TX(TX_GRID_REC_B):BB_TX(TX_GRID_REC_A)):(machine?BB_TX(TX_DILLO_KIT_B):BB_TX(TX_DILLO_KIT_A)),BB_TEXT);
 cv_text(6,30,&AF_S,bb_ui_kit_name(machine),BB_ORANGE);
#if BB_WITH_LIVE_CAPTURE
 if(recording){
  cv_text(6,50,&AF_S,bb_record.phase==BB_RECORD_COUNT?BB_TX(TX_COUNT_IN_NEXT_BAR):BB_TX(TX_RECORDING_NEW_HITS),BB_TEXT);
  bb_number(182,49,bb_record.take.take.count-bb_record.take.before.count,&AF_M,BB_ORANGE);
  cv_text(6,72,&AF_S,BB_TX(TX_11_PADS_SOURCE_GRID_KEPT),BB_DIM);
  cv_text(6,94,&AF_S,BB_TX(TX_K3_ACCENT_REC_FINISH),BB_DIM);
  cv_text(6,107,&AF_S,BB_TX(TX_HOME_STOP_AND_REVIEW_TAKE),BB_DIM);
 }else
#endif
 {
 cv_text(6,50,&AF_S,bb_dillo_snapshot.active?BB_TX(TX_PLAYING_TRIGGERS):bb_dillo_snapshot.running?BB_TX(TX_LAYER_OFF_MUTED):BB_TX(TX_STOPPED_PADS),BB_TEXT);
 bb_number(170,49,bb_face.requested.bpm,&AF_M,BB_TEXT);
 cv_text(6,72,&AF_S,bb_player_pending(bb_library_current())?BB_TX(TX_PATTERN_QUEUED):BB_TX(TX_CURRENT_AUDIO_OWNER),BB_DIM);
 cv_text(6,94,&AF_S,BB_TX(TX_K1_KICK_K2_SNARE_K3_HATS),BB_DIM);
 cv_text(6,107,&AF_S,BB_TX(TX_K4_PERC_HOME_STUDIO),BB_DIM);
 }
 cv_blit(0,0);
 cv_begin(240,120,BB_BG);
 cv_text(5,2,&AF_S,machine?BB_TX(TX_DILLO_KIT_B_2):BB_TX(TX_DILLO_KIT_A_2),BB_TEXT);
 cv_text(127,2,&AF_S,bb_dillo_snapshot.active?"PLAYING":bb_dillo_snapshot.running?BB_TX(TX_OFF_MUTED):BB_TX(TX_STOPPED_PADS),BB_DIM);
 static const uint8_t voices[11][3]={{0,0,0},{1,1,1},{2,5,5},{3,6,6},{4,7,7},{8,9,9},{10,11,21},{12,12,12},{13,16,16},{14,14,14},{15,15,15}};
 static const char *const labels[22]={"BD","SD","LT","MT","HT","LC","MC","HC","RIM","CLAV","CLAP","MARA","COW","CYM","OH","CH","RIDE","SY","ZAP","ACC","ACC","SH"};
 for(unsigned i=0;i<11;i++){
  int x=4+(int)(i%6)*39,y=18+(int)(i/6)*43;unsigned voice=voices[i][0];
  unsigned level=bb_dillo_activity_level(motion,voice,fm1_ms);
  for(unsigned candidate=1;candidate<3;candidate++){
   unsigned v=voices[i][candidate],l=bb_dillo_activity_level(motion,v,fm1_ms);
   if(l>level){voice=v;level=l;}
  }
  bb_dillo_kit_draw(bb_dillo_activity_pose(voice),level,x,y,bb_dillo_line_draw,NULL);
  cv_text(x+3,y+27,&AF_S,labels[voice],level?BB_ORANGE:BB_DIM);
 }
 cv_text(5,108,&AF_S,recording?BB_TX(TX_REC_FINISH_ORIGINAL_KEPT):BB_TX(TX_TRIGGERS_HOME_STUDIO),BB_DIM);cv_blit(0,120);return 1;
}
#else
static int bb_dillo_kit_draw_page(void){return 0;}
#endif
