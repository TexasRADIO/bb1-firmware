/* SPDX-License-Identifier: GPL-3.0-only
 * One global MIDI routing contract. Channels are explicit1..16;0 disables.
 * Lead channel is reserved even when that owner is not compiled in. */
#ifndef BB_MIDI_SETTINGS_H
#define BB_MIDI_SETTINGS_H
#include <stdint.h>
enum {BB_MIDI_CLOCK_USB=1,BB_MIDI_CLOCK_TRS=2};
typedef struct {
 uint8_t clock_source,tempo_follow,transport,clock_out;
 uint8_t drum_channel[2],synth_channel[2];
} bb_midi_compat_settings;
#define BB_MIDI_SETTINGS_DEFAULT {BB_MIDI_CLOCK_USB,1,1,0,{10,11},{1,2}}
static inline bb_midi_compat_settings bb_midi_settings_default(void){
 const bb_midi_compat_settings c=BB_MIDI_SETTINGS_DEFAULT;return c;
}
static inline int bb_midi_compat_settings_valid(const bb_midi_compat_settings *c){
 if(!c||(c->clock_source!=BB_MIDI_CLOCK_USB&&c->clock_source!=BB_MIDI_CLOCK_TRS)||
    c->tempo_follow>1||c->transport>1||c->clock_out)return 0; /* out is stage2 */
 uint32_t used=0;
 for(unsigned i=0;i<4;i++){
  unsigned ch=i<2?c->drum_channel[i]:c->synth_channel[i-2];
  if(ch>16)return 0;
  if(ch){uint32_t bit=UINT32_C(1)<<ch;if(used&bit)return 0;used|=bit;}
 }
 return 1;
}
#endif
