/* SPDX-License-Identifier: GPL-3.0-only */
#include "bb_rich_scene.h"
#include "bb_crc32.h"
#include "bb_wire.h"
#include <string.h>
_Static_assert(sizeof(bb_phrase)==260,"Review canonical phrase shape");
static int nested_known(const uint8_t *p,size_t bytes){
 return bytes>=4&&(!memcmp(p,"BBEX",4)||!memcmp(p,"BBSC",4)||!memcmp(p,"BBFS",4)||!memcmp(p,"BBGM",4)||!memcmp(p,"BBKS",4));
}
static uint32_t checksum(const uint8_t *p,size_t bytes){return ~bb_crc32_add(bb_crc32_add(UINT32_MAX,p,12),p+16,bytes-16);}
static int canonical(const bb_phrase *p){
 bb_phrase_index scratch;
 if(!bb_phrase_index_prepare(p,UINT32_MAX,&scratch))return 0;
 for(unsigned i=p->count;i<BB_FACTORY_DRUM_EVENTS;i++)if(p->events[i])return 0;
 return 1;
}
static void read_phrase(const uint8_t *p,bb_phrase *out){
 for(unsigned i=0;i<BB_FACTORY_DRUM_EVENTS;i++)out->events[i]=(uint16_t)(p[2*i]|((unsigned)p[2*i+1]<<8));
 out->length=p[256];out->count=p[257];out->lattice=p[258];out->bars=p[259];
}
static int zero(const uint8_t *p,size_t bytes){for(size_t i=0;i<bytes;i++)if(p[i])return 0;return 1;}
size_t bb_rich_scene_finish(void *output,size_t capacity,size_t nested,const bb_phrase p[2],unsigned mask){
 const size_t overhead=BB_RICH_SCENE_OVERHEAD;
 if(!output||!p||mask>3||capacity<overhead||nested>UINT32_MAX-overhead||nested>capacity-overhead||
    !bb_wire_separated(output,overhead+nested,p,2*sizeof *p))return 0;
 uint8_t *dst=output;if(!nested_known(dst+overhead,nested))return 0;
 for(unsigned m=0;m<2;m++){
  if(mask&(1u<<m)){if(!canonical(&p[m]))return 0;}
  else if(!zero((const uint8_t *)&p[m],sizeof p[m]))return 0;
 }
 if(!mask){
  volatile uint8_t *out=dst;const volatile uint8_t *in=dst+overhead;
  for(size_t i=0;i<nested;i++)out[i]=in[i];return nested;
 }
 memset(dst,0,overhead);memcpy(dst,"BBRP",4);dst[4]=1;dst[5]=(uint8_t)mask;dst[6]=2;
 bb_wire_put32(dst+8,(uint32_t)(overhead+nested));
 for(unsigned m=0;m<2;m++)if(mask&(1u<<m)){
  uint8_t *q=dst+16+260*m;
  for(unsigned i=0;i<p[m].count;i++){q[2*i]=(uint8_t)p[m].events[i];q[2*i+1]=(uint8_t)(p[m].events[i]>>8);}
  q[256]=p[m].length;q[257]=p[m].count;q[258]=p[m].lattice;q[259]=p[m].bars;
 }
 bb_wire_put32(dst+12,checksum(dst,overhead+nested));return overhead+nested;
}
int bb_rich_scene_open(const void *input,size_t bytes,bb_phrase p[2],uint8_t *mask,const void **nested,size_t *length){
 if(!input||!p||!mask||!nested||!length||bytes<4)return 0;
 const void *outputs[]={p,mask,nested,length};const size_t sizes[]={2*sizeof *p,sizeof *mask,sizeof *nested,sizeof *length};
 for(unsigned i=0;i<4;i++){
  if(!bb_wire_separated(input,bytes,outputs[i],sizes[i]))return 0;
  for(unsigned j=0;j<i;j++)if(!bb_wire_separated(outputs[j],sizes[j],outputs[i],sizes[i]))return 0;
 }
 const uint8_t *src=input;size_t offset=0;unsigned bits=0;
 if(!memcmp(src,"BBRP",4)){
  offset=BB_RICH_SCENE_OVERHEAD;
  if(bytes<offset+4||bytes>UINT32_MAX)return 0;
  bits=src[5];
  if(src[4]!=1||!bits||bits>3||src[6]!=2||src[7]||
     bb_wire_get32(src+8)!=bytes||bb_wire_get32(src+12)!=checksum(src,bytes)||!nested_known(src+offset,bytes-offset))return 0;
  /* One temporary phrase; all outputs remain untouched until all parts pass. */
  for(unsigned m=0;m<2;m++){
   const uint8_t *q=src+16+260*m;
   if(bits&(1u<<m)){bb_phrase scratch;read_phrase(q,&scratch);if(!canonical(&scratch))return 0;}
   else if(!zero(q,260))return 0;
  }
 }else if(!nested_known(src,bytes))return 0;
 memset(p,0,2*sizeof *p);for(unsigned m=0;m<2;m++)if(bits&(1u<<m))read_phrase(src+16+260*m,&p[m]);
 *mask=(uint8_t)bits;*nested=src+offset;*length=bytes-offset;return 1;
}
