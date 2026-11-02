/* SPDX-License-Identifier: GPL-3.0-only */
#include "bb_rich_scene.h"
#include "bb_crc32.h"
#include "bb_wire.h"
#include <string.h>
_Static_assert(sizeof(bb_phrase)==292,"v2 canonical phrase shape");
static int nested_known(const uint8_t *p,size_t bytes){return bytes>=4&&(!memcmp(p,"BBEX",4)||!memcmp(p,"BBSC",4)||!memcmp(p,"BBFS",4)||!memcmp(p,"BBGM",4)||!memcmp(p,"BBKS",4));}
#define checksum(a,b) bb_crc32_record(a,b)
static int zero(const uint8_t *p,size_t n){for(size_t i=0;i<n;i++)if(p[i])return 0;return 1;}
static int canonical(const bb_phrase *p){
 bb_phrase_index scratch;if(!bb_phrase_index_prepare(p,UINT32_MAX,&scratch))return 0;
 for(unsigned i=p->count;i<BB_FACTORY_DRUM_EVENTS;i++)if(p->events[i]||((p->ratchets[i>>2]>>(2*(i&3)))&3))return 0;
 for(unsigned i=0;i<p->count;i++)if((p->events[i]&31u)==BB_FDV_ACCENT||(p->events[i]&31u)==BB_FDV_ACC)
  if(bb_phrase_ratchet_count(p,i)!=1)return 0;
 return 1;
}
static void read_phrase(const uint8_t *p,unsigned version,bb_phrase *out){
 memset(out,0,sizeof *out);for(unsigned i=0;i<BB_FACTORY_DRUM_EVENTS;i++)out->events[i]=(uint16_t)(p[2*i]|((unsigned)p[2*i+1]<<8));
 out->length=p[256];out->count=p[257];out->lattice=p[258];out->bars=p[259];
 if(version==2)memcpy(out->ratchets,p+260,sizeof out->ratchets);
}
static void write_phrase(uint8_t *p,const bb_phrase *in,unsigned version){
 for(unsigned i=0;i<BB_FACTORY_DRUM_EVENTS;i++){p[2*i]=(uint8_t)in->events[i];p[2*i+1]=(uint8_t)(in->events[i]>>8);}
 p[256]=in->length;p[257]=in->count;p[258]=in->lattice;p[259]=in->bars;
 if(version==2)memcpy(p+260,in->ratchets,sizeof in->ratchets);
}
typedef struct {size_t offset,phrase[2],bitmap,words;unsigned version,mask,has_grid;} envelope;
static int parse(const uint8_t *p,size_t bytes,envelope *v){
 memset(v,0,sizeof *v);if(bytes<4)return 0;
 if(memcmp(p,"BBRP",4))return nested_known(p,bytes);
 if(bytes<16||bytes>UINT32_MAX||p[6]!=2||bb_wire_get32(p+8)!=bytes||bb_wire_get32(p+12)!=checksum(p,bytes))return 0;
 v->version=p[4];v->mask=p[5];v->offset=16;
 if(v->mask>3)return 0;
 if(v->version==1){
  if(!v->mask||p[7]||bytes<BB_RICH_SCENE_LEGACY_OVERHEAD+4)return 0;
  for(unsigned m=0;m<2;m++){
   v->phrase[m]=16+260*m;
   if(!(v->mask&(1u<<m))&&!zero(p+v->phrase[m],260))return 0;
  }
  v->offset=BB_RICH_SCENE_LEGACY_OVERHEAD;
 }else if(v->version==2){
  if(p[7]>1)return 0;v->has_grid=p[7];
  for(unsigned m=0;m<2;m++)if(v->mask&(1u<<m)){
   if(v->offset>bytes||292>bytes-v->offset)return 0;v->phrase[m]=v->offset;v->offset+=292;
  }
  if(v->has_grid){
   if(v->offset>bytes||16>bytes-v->offset)return 0;v->bitmap=v->offset;v->offset+=16;v->words=v->offset;
   unsigned n=0;for(unsigned row=0;row<128;row++)if(p[v->bitmap+(row>>3)]&(1u<<(row&7))){
    if(v->offset>bytes||4>bytes-v->offset||!bb_wire_get32(p+v->offset))return 0;v->offset+=4;n++;
   }
   if(!n)return 0; /* canonical: omit an empty native sidecar */
  }
 }else return 0;
 if(v->offset>bytes||!nested_known(p+v->offset,bytes-v->offset))return 0;
 for(unsigned m=0;m<2;m++)if(v->mask&(1u<<m)){
  bb_phrase scratch;read_phrase(p+v->phrase[m],v->version,&scratch);if(!canonical(&scratch))return 0;
 }
 return 1;
}
static size_t finish(void *output,size_t capacity,size_t nested,const bb_phrase phrases[2],unsigned mask,const bb_core *core,unsigned version){
 if(!output||!phrases||mask>3||capacity<BB_RICH_SCENE_OVERHEAD||nested>UINT32_MAX-BB_RICH_SCENE_OVERHEAD||nested>capacity-BB_RICH_SCENE_OVERHEAD||
    !bb_wire_separated(output,BB_RICH_SCENE_OVERHEAD+nested,phrases,2*sizeof *phrases)||
    (core&&!bb_wire_separated(output,BB_RICH_SCENE_OVERHEAD+nested,core,sizeof *core)))return 0;
 uint8_t *dst=output;if(!nested_known(dst+BB_RICH_SCENE_OVERHEAD,nested))return 0;
 unsigned rows=0;
 for(unsigned m=0;m<2;m++){
  if(mask&(1u<<m)){if(!canonical(&phrases[m]))return 0;}
  else if(!zero((const uint8_t *)&phrases[m],sizeof phrases[m]))return 0;
  if(version==1&&!zero(phrases[m].ratchets,sizeof phrases[m].ratchets))return 0;
 }
 if(core){
  for(unsigned m=0;m<BB_MACHINES;m++)for(unsigned s=0;s<BB_STEPS;s++){
   if(m>=2&&core->drums[m].steps[s].ratchets)return 0;
   if(m<2)rows+=core->drums[m].steps[s].ratchets!=0;
  }
 }
 size_t header=version==1?(mask?BB_RICH_SCENE_LEGACY_OVERHEAD:0):16+292*((mask&1u)!=0)+292*((mask&2u)!=0)+(rows?16+4*rows:0);
 volatile uint8_t *to=dst+header;const volatile uint8_t *from=dst+BB_RICH_SCENE_OVERHEAD;
 for(size_t i=0;i<nested;i++)to[i]=from[i];
 if(!header)return nested;
 memset(dst,0,header);memcpy(dst,"BBRP",4);dst[4]=(uint8_t)version;dst[5]=(uint8_t)mask;dst[6]=2;dst[7]=(uint8_t)(rows!=0);
 size_t at=16;
 for(unsigned m=0;m<2;m++){
  if(version==1){if(mask&(1u<<m))write_phrase(dst+16+260*m,&phrases[m],1);}
  else if(mask&(1u<<m)){write_phrase(dst+at,&phrases[m],2);at+=292;}
 }
 if(rows){size_t bitmap=at;at+=16;
  for(unsigned m=0;m<2;m++)for(unsigned s=0;s<BB_STEPS;s++)if(core->drums[m].steps[s].ratchets){
   unsigned row=m*BB_STEPS+s;dst[bitmap+(row>>3)]|=(uint8_t)(1u<<(row&7));bb_wire_put32(dst+at,core->drums[m].steps[s].ratchets);at+=4;
  }
 }
 bb_wire_put32(dst+8,(uint32_t)(header+nested));bb_wire_put32(dst+12,checksum(dst,header+nested));return header+nested;
}
size_t bb_rich_scene_finish(void *out,size_t cap,size_t n,const bb_phrase p[2],unsigned mask){return finish(out,cap,n,p,mask,NULL,1);}
size_t bb_rich_scene_finish_v2(void *out,size_t cap,size_t n,const bb_phrase p[2],unsigned mask,const bb_core *core){
 if(!core)return 0;return finish(out,cap,n,p,mask,core,2);
}
int bb_rich_scene_open(const void *input,size_t bytes,bb_phrase p[2],uint8_t *mask,const void **nested,size_t *length){
 if(!input||!p||!mask||!nested||!length)return 0;
 const void *outputs[]={p,mask,nested,length};const size_t sizes[]={2*sizeof *p,sizeof *mask,sizeof *nested,sizeof *length};
 for(unsigned i=0;i<4;i++){
  if(!bb_wire_separated(input,bytes,outputs[i],sizes[i]))return 0;
  for(unsigned j=0;j<i;j++)if(!bb_wire_separated(outputs[j],sizes[j],outputs[i],sizes[i]))return 0;
 }
 envelope v;if(!parse(input,bytes,&v))return 0;
 memset(p,0,2*sizeof *p);for(unsigned m=0;m<2;m++)if(v.mask&(1u<<m))read_phrase((const uint8_t *)input+v.phrase[m],v.version,&p[m]);
 *mask=(uint8_t)v.mask;*nested=(const uint8_t *)input+v.offset;*length=bytes-v.offset;return 1;
}
int bb_rich_scene_apply_ratchets(const void *input,size_t bytes,bb_core *core){
 if(!input||!core||!bb_wire_separated(input,bytes,core,sizeof *core))return 0;
 envelope v;if(!parse(input,bytes,&v))return 0;
 for(unsigned m=0;m<BB_MACHINES;m++)for(unsigned s=0;s<BB_STEPS;s++)core->drums[m].steps[s].ratchets=0;
 if(v.has_grid){const uint8_t *p=input;size_t at=v.words;
  for(unsigned row=0;row<128;row++)if(p[v.bitmap+(row>>3)]&(1u<<(row&7))){core->drums[row/BB_STEPS].steps[row%BB_STEPS].ratchets=bb_wire_get32(p+at);at+=4;}
 }
 return 1;
}
