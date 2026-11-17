/* SPDX-License-Identifier: GPL-3.0-only
 * Cold resident catalogue candidate. Allocate in .pool on the FM1, alongside
 * bb_sound; never silently consume the small-variable RAM reserve.
 * Prepare before audio starts. No flash writes, heap, or live bank mutation.
 */
#ifndef BB_SAMPLE_RESIDENT_R24_H
#define BB_SAMPLE_RESIDENT_R24_H
#ifndef BB_SAMPLE_TARGET_CANDIDATE
#include <string.h>
#endif
#include "bb_audio.h"
#include "bb_sample_pack_r20.h"
#include "bb_sample_gain.h"
/* Caller-owned sample polyphony; zone/directory bounds are independent. */
#define BB_SAMPLE_OWNER_SLOTS 24u
_Static_assert(BB_SAMPLE_OWNER_SLOTS>0u && BB_SAMPLE_OWNER_SLOTS<=BB_SK_MAX_SLOTS,
               "sample owner fits the existing slot contract");
typedef struct {
 bb_sample_zone zones[8][16];
 bb_sample_slot slots[BB_SAMPLE_OWNER_SLOTS]; /* capacity, not a hardware timing claim */
 bb_sample_audio_binding binding;
 const char *names[8];
 uint8_t counts[8],count;
} bb_sample_resident;
/* Zero-init caller-owned resident storage. Immutable pack stays mapped until
 * all voices are stopped and the entire audio catalogue is retired. */
static int bb_sample_resident_prepare(bb_sample_resident*r,const uint8_t*p,size_t bytes){
 if(!r||r->count||!p||bb_fsmp_overlap(r,sizeof *r,p,bytes)||!bb_sample_pack_valid(p,bytes))return 0;
 for(unsigned i=0;i<p[6];i++){
  unsigned n=0;const uint8_t*e=p+32+i*28;uint32_t off=bb_fsmp_u32(e),size=bb_fsmp_u32(e+4);
  /* Full pack validation above proved each entry; immutable source is a requirement. */
  if(!bb_sample_fsmp_parse(p+off+32,size-32,p+off,p+off+16,p[off+38],r->zones[i],&n))return 0;
  r->counts[i]=(uint8_t)n;r->names[i]=(const char*)e+12;
 }
 memset(r->slots,0,sizeof r->slots);
 r->count=p[6];return 1;
}
static int bb_sample_resident_binding(bb_sample_resident*r,unsigned id,bb_sample_audio_binding*out){
 static const uint8_t lane_notes[11]={36,38,45,47,50,37,39,56,49,46,42};
 if(!r||!out||!r->count||id>=r->count||
    bb_fsmp_overlap(out,sizeof *out,r,sizeof *r))return 0;
 bb_sample_audio_binding b={0};b.slots=r->slots;b.count=BB_SAMPLE_OWNER_SLOTS;b.zones=r->zones[id];b.nz=r->counts[id];
 b.gain=BB_SAMPLE_DEFAULT_GAIN;b.echo=64;b.spring=48; /* gain: see bb_control.h */
 for(unsigned lane=0;lane<11;lane++){
  b.lane_notes[lane]=255;
  for(unsigned i=0;i<b.nz;i++)if(b.zones[i].note==lane_notes[lane])b.lane_notes[lane]=lane_notes[lane];
 }
 *out=b;return 1;
}
#endif
