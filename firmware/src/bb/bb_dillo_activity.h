/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef BB_DILLO_ACTIVITY_H
#define BB_DILLO_ACTIVITY_H
#include <stdint.h>
/* Read-only UI consumer of an audio-owned accepted-strike counter snapshot.
 * Not key animation: speculative taps and browsed patterns never trigger it.
 * One pulse per voice coalesces multiple strikes between redraws, explicitly
 * reporting that count. Caller supplies resolved sounding voice, not source.
 * Snapshot producer/locking is a separate target-integration gate. */
enum { BB_DILLO_KIT_VOICES=22, BB_DILLO_PULSE_MS=160 };
enum { BB_DILLO_CURLED, BB_DILLO_FRONT, BB_DILLO_SIDE, BB_DILLO_SHELL };
typedef struct {
 uint32_t seen[BB_DILLO_KIT_VOICES],last_ms[BB_DILLO_KIT_VOICES];
 uint8_t pulses[BB_DILLO_KIT_VOICES],ready;
} bb_dillo_activity;
static inline void bb_dillo_activity_reset(bb_dillo_activity *a){
 for(unsigned i=0;i<BB_DILLO_KIT_VOICES;i++)a->seen[i]=a->last_ms[i]=a->pulses[i]=0;
 a->ready=0;
}
static inline void bb_dillo_activity_read(bb_dillo_activity *a,const uint32_t *counts,uint32_t now){
 for(unsigned i=0;i<BB_DILLO_KIT_VOICES;i++){
  uint32_t delta=counts[i]-a->seen[i];a->seen[i]=counts[i];
  if(a->ready&&delta){a->last_ms[i]=now;a->pulses[i]=(uint8_t)(delta>255?255:delta);}
  else if((uint32_t)(now-a->last_ms[i])>=BB_DILLO_PULSE_MS)a->pulses[i]=0;
 }
 a->ready=1;
}
static inline unsigned bb_dillo_activity_level(const bb_dillo_activity *a,unsigned voice,uint32_t now){
 if(voice>=BB_DILLO_KIT_VOICES||!a->pulses[voice])return 0;
 uint32_t age=now-a->last_ms[voice];return age<BB_DILLO_PULSE_MS?1u+(BB_DILLO_PULSE_MS-1u-age)/40u:0;
}
/* BB1 sounding voice ordinal, never a hidden replacement of displayed labels. */
static inline unsigned bb_dillo_activity_pose(unsigned voice){
 return voice==0?BB_DILLO_CURLED:voice==1||voice==8||voice==10?BB_DILLO_FRONT:
        voice>=13&&voice<=16?BB_DILLO_SHELL:BB_DILLO_SIDE;
}
#endif
