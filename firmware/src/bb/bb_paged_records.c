/* SPDX-License-Identifier: GPL-3.0-only */
#include "bb_paged_records.h"
#include <string.h>
#define VALIDATED UINT32_C(0x42505231)
typedef struct {uint32_t at,end,children;unsigned kind,field,count;} rec;
static uint16_t u16(const uint8_t*p){return (uint16_t)((unsigned)p[0]|((unsigned)p[1]<<8));}
static uint32_t u32(const uint8_t*p){return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);}
static int span(const void*p,size_t n){return p && n<=UINTPTR_MAX-(uintptr_t)p;}
static int disjoint(const void*a,size_t an,const void*b,size_t bn){
 uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;return x<=y?an<=y-x:bn<=x-y;
}
static int ok(const bb_pr_view*v){return span(v,sizeof *v) && !( (uintptr_t)v%_Alignof(bb_pr_view)) && v->validated==VALIDATED;}
static int output(const bb_pr_view*v,const void*p,size_t n){return span(p,n)&&disjoint(p,n,v,sizeof *v)&&
 disjoint(p,n,v->container.bytes,v->container.length)&&disjoint(p,n,v->container.cache,sizeof *v->container.cache);}
static int read(const bb_pc_view*v,uint32_t at,void*out,size_t n){return bb_pc_read_metadata(v,at,out,n);}
static int var(const bb_pc_view*v,uint32_t*at,uint32_t end,unsigned*out){
 uint32_t pos=*at;unsigned value=0;
 for(unsigned shift=0;shift<=14;shift+=7){uint8_t b;if(pos>=end||!read(v,pos++,&b,1))return 0;
  if(shift==14&&b>3u)return 0;value|=(b&127u)<<shift;
  if(!(b&128u)){if(shift&&!(b&127u))return 0;*at=pos;*out=value;return 1;}}
 return 0;
}
static unsigned shape_id(const bb_pc_view*v,unsigned token){return token<v->shapes?u16(v->bytes+v->dictionary+4u*token):BB_PR_NONE;}
static unsigned shape_count(const bb_pc_view*v,unsigned token){return token<v->shapes?u16(v->bytes+v->dictionary+4u*token+2):0;}
/* Parse one record without retaining pointers into the evictable cache. */
static int parse(const bb_pc_view*v,uint32_t at,rec*r){
 uint8_t b[7];if(!read(v,at,b,1))return 0;
 unsigned kind=b[0]&7u;if(kind>BB_PR_OBJECT||(kind!=BB_PR_OBJECT&&b[0]!=kind))return 0;
 uint32_t n;unsigned field=0,count=0;uint32_t children=at;
 if(kind<=BB_PR_TRUE)n=1;
 else if(kind==BB_PR_NUMBER)n=9;
 else if(kind==BB_PR_TEXT)n=7;
 else {
  if(!read(v,at+1,b+1,1))return 0;n=b[1];unsigned header=2;
  if(n==255u){if(!read(v,at+2,b+2,4))return 0;n=u32(b+2);header=6;if(n<259u)return 0;}
  if(n<header||n>v->metadata_bytes-at)return 0;children=at+header;
  if(kind==BB_PR_OBJECT){field=b[0]>>3;if(field==31u){unsigned extra;if(!var(v,&children,at+n,&extra)||extra>65504u)return 0;field+=extra;}
   if(field>=v->shapes)return 0;count=shape_count(v,field);
  }else{if(!var(v,&children,at+n,&field))return 0;count=field;}
 }
 if(n>v->metadata_bytes-at)return 0;
 *r=(rec){at,at+n,children,kind,field,count};return 1;
}
static int get(const bb_pc_view*v,unsigned id,rec*r){
 if(id>=v->nodes)return 0;uint32_t at=u32(v->bytes+v->sparse+4u*(id>>4));
 for(unsigned skip=id&15u;skip;skip--){rec q;if(!parse(v,at,&q))return 0;at=q.end;}
 return parse(v,at,r);
}
static uint16_t child(const bb_pc_view*v,const rec*r,size_t index){
 if(index>=r->count)return BB_PR_NONE;uint32_t pos=r->children;unsigned id=0;
 for(size_t i=0;i<=index;i++)if(!var(v,&pos,r->end,&id))return BB_PR_NONE;
 return (uint16_t)id;
}
static int text(const bb_pc_view*v,const rec*r,const uint8_t**p,size_t*n){
 uint8_t b[6];if(r->kind!=BB_PR_TEXT||!read(v,r->at+1,b,6))return 0;
 size_t count=u16(b);const uint8_t*value;
 if(!bb_pc_text_bytes(v,u32(b+2),count,&value))return 0;*p=value;*n=count;return 1;
}
static int validate(const bb_pc_view*v){
 uint32_t at=0,text_end=0;
 /* First pass proves exact record boundaries, numeric/text spans, backward
  * references and canonical child encodings before indexed traversal. */
 for(unsigned id=0;id<v->nodes;id++){
  if(!(id&15u)&&u32(v->bytes+v->sparse+4u*(id>>4))!=at)return 0;
  rec r;if(!parse(v,at,&r))return 0;
  if(r.kind==BB_PR_NUMBER){uint8_t b[2];if(!read(v,at+7,b,2)||(u16(b)&0x7ff0u)==0x7ff0u)return 0;}
  if(r.kind==BB_PR_TEXT){uint8_t b[6];if(!read(v,at+1,b,6))return 0;
   unsigned n=u16(b);if(u32(b+2)!=text_end||n>v->text_bytes-text_end)return 0;text_end+=n;}
  if(r.kind>=BB_PR_ARRAY){uint32_t pos=r.children;unsigned value;
   if(r.kind==BB_PR_OBJECT&&shape_id(v,r.field)>=id)return 0;
   for(unsigned i=0;i<r.count;i++)if(!var(v,&pos,r.end,&value)||value>=id)return 0;
   if(pos!=r.end)return 0;}
  at=r.end;
 }
 if(at!=v->metadata_bytes||text_end!=v->text_bytes)return 0;
 unsigned previous=0;
 for(unsigned token=0;token<v->shapes;token++){
  unsigned id=shape_id(v,token);rec r;
  if((token&&id<=previous)||!get(v,id,&r)||r.kind!=BB_PR_ARRAY||r.count!=shape_count(v,token))return 0;
  previous=id;
 }
 /* Every used key shape is text-only and duplicate-free by ID AND bytes.
  * Match existing catalogue semantics: unused shapes need only be arrays. */
 uint32_t checked[2]={0,0};unsigned last=BB_PR_NONE;at=0;
 for(unsigned id=0;id<v->nodes;id++){
  /* The first pass already proved every record boundary. Walk those same
   * boundaries directly instead of rescanning up to fifteen earlier nodes. */
  rec r;if(!parse(v,at,&r))return 0;at=r.end;if(r.kind!=BB_PR_OBJECT)continue;
  unsigned token=r.field;if(token==last||(token<64u&&(checked[token>>5]&(UINT32_C(1)<<(token&31u)))))continue;
  rec shape;if(!get(v,shape_id(v,token),&shape))return 0;uint32_t pos=shape.children;
  for(unsigned k=0;k<shape.count;k++){
   unsigned key;rec kr;const uint8_t*kp;size_t kn;
   if(!var(v,&pos,shape.end,&key)||!get(v,key,&kr)||!text(v,&kr,&kp,&kn))return 0;
   uint32_t before=shape.children;
   for(unsigned j=0;j<k;j++){
    unsigned old;rec oldr;const uint8_t*op;size_t on;
    if(!var(v,&before,shape.end,&old)||old==key||!get(v,old,&oldr)||!text(v,&oldr,&op,&on)||
       (on==kn&&!memcmp(op,kp,kn)))return 0;
   }
  }
  last=token;if(token<64u)checked[token>>5]|=UINT32_C(1)<<(token&31u);
 }
 return 1;
}
int bb_pr_open(bb_pr_view*out,const uint8_t*bytes,size_t n,bb_pc_cache*cache){
 if(!span(out,sizeof *out)||(uintptr_t)out%_Alignof(bb_pr_view)||!span(bytes,n)||!span(cache,sizeof *cache)||
  !disjoint(out,sizeof *out,bytes,n)||!disjoint(out,sizeof *out,cache,sizeof *cache))return 0;
 bb_pr_view next;
 if(!bb_pc_open_container(&next.container,bytes,n,cache)||!validate(&next.container))return 0;
 next.validated=VALIDATED;*out=next;return 1;
}
int bb_pr_kind(const bb_pr_view*v,uint16_t id){rec r;return ok(v)&&get(&v->container,id,&r)?(int)r.kind:-1;}
size_t bb_pr_count(const bb_pr_view*v,uint16_t id){rec r;return ok(v)&&get(&v->container,id,&r)&&r.kind>=BB_PR_ARRAY?r.count:0;}
uint16_t bb_pr_at(const bb_pr_view*v,uint16_t id,size_t i){rec r;return ok(v)&&get(&v->container,id,&r)&&r.kind>=BB_PR_ARRAY?child(&v->container,&r,i):BB_PR_NONE;}
uint16_t bb_pr_key_at(const bb_pr_view*v,uint16_t id,size_t i){rec r,s;
 return ok(v)&&get(&v->container,id,&r)&&r.kind==BB_PR_OBJECT&&get(&v->container,shape_id(&v->container,r.field),&s)?child(&v->container,&s,i):BB_PR_NONE;}
int bb_pr_text(const bb_pr_view*v,uint16_t id,const uint8_t**p,size_t*n){
 rec r;const uint8_t*value;size_t count;
 if(!ok(v)||!output(v,p,sizeof *p)||!output(v,n,sizeof *n)||(uintptr_t)p%_Alignof(const uint8_t*)||
  (uintptr_t)n%_Alignof(size_t)||!disjoint(p,sizeof *p,n,sizeof *n)||!get(&v->container,id,&r)||!text(&v->container,&r,&value,&count))return 0;
 *p=value;*n=count;return 1;
}
uint16_t bb_pr_find(const bb_pr_view*v,uint16_t id,const char*key){
 rec r,s;if(!key||!ok(v)||!get(&v->container,id,&r)||r.kind!=BB_PR_OBJECT)return BB_PR_NONE;size_t n=0;
 while(n<65536u&&key[n])n++;if(n==65536u)return BB_PR_NONE;
 if(!get(&v->container,shape_id(&v->container,r.field),&s))return BB_PR_NONE;
 /* Search the immutable shape first. Only traverse the object values once
  * the key position is known, avoiding repeated page switches between keys
  * and values. No cache pointer or additional persistent storage is used. */
 uint32_t keys=s.children;
 for(unsigned i=0;i<r.count;i++){unsigned key_id;rec kr;const uint8_t*p;size_t len;
  if(!var(&v->container,&keys,s.end,&key_id)||
     !get(&v->container,key_id,&kr)||!text(&v->container,&kr,&p,&len))return BB_PR_NONE;
  if(len==n&&!memcmp(p,key,n))return child(&v->container,&r,i);}
 return BB_PR_NONE;
}
int bb_pr_number_bits(const bb_pr_view*v,uint16_t id,uint8_t out[8]){
 rec r;uint8_t value[8];if(!ok(v)||!output(v,out,8)||!get(&v->container,id,&r)||r.kind!=BB_PR_NUMBER||
  !read(&v->container,r.at+1,value,8))return 0;
 memcpy(out,value,8);return 1;
}
int bb_pr_number(const bb_pr_view*v,uint16_t id,double*out){
 uint8_t bits[8];if(sizeof(double)!=8||!ok(v)||!output(v,out,sizeof *out)||
  (uintptr_t)out%_Alignof(double)||!bb_pr_number_bits(v,id,bits))return 0;
 memcpy(out,bits,8);return 1;
}
int bb_pr_unsigned(const bb_pr_view*v,uint16_t id,uint32_t maximum,uint32_t*out){
 uint8_t bits[8];if(!ok(v)||!output(v,out,sizeof *out)||(uintptr_t)out%_Alignof(uint32_t)||
  !bb_pr_number_bits(v,id,bits))return 0;
 uint32_t low=u32(bits),high=u32(bits+4),fraction=high&0xfffffu;
 unsigned exponent=(high>>20)&0x7ffu;
 if(!exponent&&!fraction&&!low){*out=0;return 1;}
 if((high>>31)||exponent<1023u||exponent>1054u)return 0;
 unsigned power=exponent-1023u;uint32_t significand=0x100000u|fraction,value;
 if(power<=20u){unsigned shift=20u-power;if(low||(fraction&((1u<<shift)-1u)))return 0;value=significand>>shift;}
 else{unsigned shift=power-20u,discard=32u-shift;if(low&((1u<<discard)-1u))return 0;value=(significand<<shift)|(low>>discard);}
 if(value>maximum)return 0;*out=value;return 1;
}
int bb_pr_copy_text(const bb_pr_view*v,uint16_t id,char*out,size_t capacity){
 const uint8_t*p;size_t n;
 if(!ok(v)||!output(v,out,capacity)||!bb_pr_text(v,id,&p,&n)||capacity<=n)return 0;
 memcpy(out,p,n);out[n]=0;return 1;
}
/* Preserve the existing exact binary64-to-scaled-integer operation and its
 * half-up rule, without target double-conversion or division helpers. */
int bb_pr_scaled(const bb_pr_view*v,uint16_t id,unsigned numerator,unsigned denominator,uint32_t maximum,uint32_t*out){
 uint8_t bits[8];if(!ok(v)||!output(v,out,sizeof *out)||(uintptr_t)out%_Alignof(uint32_t)||
  !numerator||numerator>1024u||!denominator||denominator>1024u||!bb_pr_number_bits(v,id,bits))return 0;
 uint32_t low=u32(bits),high=u32(bits+4),fraction=high&0xfffffu;unsigned exponent=(high>>20)&0x7ffu;
 if(!exponent&&!fraction&&!low){*out=0;return 1;}
 if((high>>31)||exponent==2047u)return 0;
 uint64_t mantissa=((uint64_t)fraction<<32)|low;int power=-1074;
 if(exponent){mantissa|=UINT64_C(1)<<52;power=(int)exponent-1075;}
 uint64_t scaled=mantissa*numerator,whole,remainder=0;unsigned shift=0;
 if(power>=0){if(power>=64||(power&&scaled>>(64-(unsigned)power)))return 0;whole=scaled<<(unsigned)power;}
 else{shift=(unsigned)(-power);if(shift>64){*out=0;return 1;}
  if(shift==64){whole=0;remainder=scaled;}
  else{whole=scaled>>shift;remainder=scaled&((UINT64_C(1)<<shift)-1u);}}
 uint64_t quotient=0;unsigned residue=0;
 for(unsigned bit=64;bit;){--bit;residue=(residue<<1)|(unsigned)((whole>>bit)&1u);
  if(residue>=denominator){residue-=denominator;quotient|=UINT64_C(1)<<bit;}}
 unsigned twice=2u*residue;int up=twice>=denominator;
 if(!up&&twice+1u==denominator&&shift&&remainder>=(UINT64_C(1)<<(shift-1u)))up=1;
 if(quotient>maximum||(up&&quotient==maximum))return 0;
 *out=(uint32_t)quotient+(unsigned)up;return 1;
}
