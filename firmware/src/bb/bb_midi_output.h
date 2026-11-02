/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef BB_MIDI_OUTPUT_H
#define BB_MIDI_OUTPUT_H
#include <stdint.h>
typedef int (*bb_midi_emit_fn)(void *,unsigned);
typedef struct {
 uint32_t phase,clocks_sent,clock_drops,transport_sent,transport_drops;
 uint8_t enabled,running;
 bb_midi_emit_fn emit;
 void *context;
} bb_midi_output;
static inline void bb_midi_output_send(bb_midi_output *s,unsigned status) {
 int ok=s->emit&&s->emit(s->context,status);
 if(status==0xf8){if(ok)s->clocks_sent++;else s->clock_drops++;}
 else {if(ok)s->transport_sent++;else s->transport_drops++;}
}
static inline void bb_midi_output_transport(bb_midi_output *s,unsigned status) {
 s->running=(uint8_t)(status!=0xfc);
 if(status==0xfa)s->phase=0; /* Continue preserves fractional clock position. */
 if(s->enabled)bb_midi_output_send(s,status);
}
static inline void bb_midi_output_advance(bb_midi_output *s,uint32_t frames,
 uint32_t rate,unsigned bpm,int running) {
 if(!s->enabled||!running||!rate||rate>192000||bpm<30||bpm>300)return;
 const uint32_t threshold=rate*60u;
 while(frames){
  unsigned n=frames>128u?128u:frames;frames-=n;
  s->phase+=n*bpm*24u;
  while(s->phase>=threshold){s->phase-=threshold;bb_midi_output_send(s,0xf8);}
 }
}
#endif
