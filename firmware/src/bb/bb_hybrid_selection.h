/* SPDX-License-Identifier: GPL-3.0-only
 * Selection contract only: does not change audio ownership or stored formats.
 * UI index zero is the retained 808; sample indices remain unchanged on disk.
 */
#ifndef BB_HYBRID_SELECTION_H
#define BB_HYBRID_SELECTION_H
#include <stdint.h>
enum { BB_HYBRID_808=0, BB_HYBRID_SAMPLE=1, BB_HYBRID_MAX_SAMPLES=8 };
typedef struct { uint8_t kind, sample; } bb_hybrid_selection;
static inline int bb_hybrid_from_index(unsigned index,unsigned count,
                                       bb_hybrid_selection *out){
    if(!out||count>BB_HYBRID_MAX_SAMPLES||index>count)return 0;
    bb_hybrid_selection s={BB_HYBRID_808,0};
    if(index){s.kind=BB_HYBRID_SAMPLE;s.sample=(uint8_t)(index-1);}
    *out=s;return 1;
}
static inline int bb_hybrid_to_index(bb_hybrid_selection s,unsigned count,
                                     unsigned *out){
    if(!out||count>BB_HYBRID_MAX_SAMPLES)return 0;
    if(s.kind==BB_HYBRID_808){if(s.sample)return 0;*out=0;return 1;}
    if(s.kind!=BB_HYBRID_SAMPLE||s.sample>=count)return 0;
    *out=(unsigned)s.sample+1;return 1;
}
/* Caller snapshots all stored steps, not merely those in the current loop.
 * Missing lanes reject the change; never silently substitute an 808 sound.
 * This is pure validation. Audio consumer publishes a selection separately.
 */
static inline int bb_hybrid_admit(bb_hybrid_selection s,unsigned count,
                                  unsigned required,unsigned synth_lanes,
                                  const uint16_t *sample_lanes){
    unsigned index;
    if(!bb_hybrid_to_index(s,count,&index))return 0;
    if(!index)return !(required&~synth_lanes);
    return sample_lanes&&!(required&~(unsigned)sample_lanes[s.sample]);
}
#endif
