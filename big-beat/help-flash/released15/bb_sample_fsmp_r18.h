/* SPDX-License-Identifier: GPL-3.0-only
 * FSMP format: Felucca/Sloop eng_sample.c and sampleio.py,
 * Copyright Leo Kuroshita, Huegelton Instruments.
 * Cold-start one-shot parser. No flash I/O, allocation or audio-thread work.
 * Caller retains immutable file bytes, maps and output for catalogue lifetime.
 */
#ifndef BB_SAMPLE_FSMP_R18_H
#define BB_SAMPLE_FSMP_R18_H
#include "bb_sample_kit_r1.h"
#include "bb_crc32.h"
static uint32_t bb_fsmp_u32(const uint8_t*p){return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static int bb_fsmp_overlap(const void*a,size_t an,const void*b,size_t bn){
 uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;
 return x<=y?y-x<an:x-y<bn;
}
/* Mapping is explicit musical identity, not an assumption about source note
 * numbers. Choke groups are likewise chosen by the kit, not filename guesses.
 * Exact file length required: callers must not pass an entire padded slot.
 * On rejection neither output zones nor output count is modified. */
static int bb_sample_fsmp_parse(const uint8_t*p,size_t bytes,
 const uint8_t*map,const uint8_t*choke,unsigned count,bb_sample_zone*out,unsigned*out_count){
 if(!p||!map||!choke||!out||!out_count||!count||count>16||bytes<512||bytes>81920)return 0;
 size_t output_bytes=count*sizeof *out;
 if(bb_fsmp_overlap(out,output_bytes,p,bytes)||bb_fsmp_overlap(out,output_bytes,map,count)||
    bb_fsmp_overlap(out,output_bytes,choke,count)||bb_fsmp_overlap(out,output_bytes,out_count,sizeof *out_count)||
    bb_fsmp_overlap(out_count,sizeof *out_count,p,bytes)||bb_fsmp_overlap(out_count,sizeof *out_count,map,count)||
    bb_fsmp_overlap(out_count,sizeof *out_count,choke,count))return 0;
 if(bb_fsmp_u32(p)!=0x504d5346u||p[4]!=1||p[5]||p[6]!=count||p[7])return 0;
 uint32_t len=bb_fsmp_u32(p+16);
 if(len!=bytes-512||~bb_crc32_add(~0u,p+512,len)!=bb_fsmp_u32(p+20))return 0;
 uint8_t source_notes[16]={0},mapped[16]={0};uint32_t end=0;
 for(unsigned i=0;i<count;i++){
  const uint8_t*z=p+32+i*28;
  uint32_t off=bb_fsmp_u32(z),n=bb_fsmp_u32(z+4),rate=bb_fsmp_u32(z+16);
  if(!n||n>162816u||off!=end||off>len||(n+1u)/2u>len-off)return 0;
  if(bb_fsmp_u32(z+8)||bb_fsmp_u32(z+12)!=n-1||!rate||rate>262144u||z[22]||z[23]||z[24]||z[27])return 0;
  unsigned lo=z[25],hi=z[26];if(lo>hi||hi>127||map[i]>127)return 0;
  unsigned note=map[i],bit=1u<<(note&7u);
  if(mapped[note>>3]&bit)return 0;
  mapped[note>>3]|=(uint8_t)bit;
  for(unsigned k=lo;k<=hi;k++){
   unsigned mask=1u<<(k&7u);if(source_notes[k>>3]&mask)return 0;
   source_notes[k>>3]|=(uint8_t)mask;
  }
  end=off+(n+1u)/2u;
 }
 if(end!=len)return 0;
 /* Publish only after the full immutable input has been validated. */
 for(unsigned i=0;i<count;i++){
  const uint8_t*z=p+32+i*28;uint32_t n=bb_fsmp_u32(z+4);
  out[i]=(bb_sample_zone){p+512+bb_fsmp_u32(z),(n+1u)/2u,n,bb_fsmp_u32(z+16),map[i],choke[i]};
 }
 *out_count=count;return 1;
}
#endif
