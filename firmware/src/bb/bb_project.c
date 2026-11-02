/* SPDX-License-Identifier: GPL-3.0-only */
#include "bb_project.h"
#include "bb_audio.h"
#include "bb_crc32.h"
#include "bb_wire.h"
#include <string.h>
typedef struct {
 uint8_t *base;size_t n,at,cache_at,cache_n;
 bb_project_read_at read;bb_project_write_at write;void *context;
 uint32_t crc;unsigned bad;uint8_t cache[256];
} stream;
static int flush(stream *s){
 if(s->bad)return 0;
 if(s->write&&s->cache_n){
  if(!s->write(s->context,s->cache_at,s->cache,s->cache_n)){s->bad=2;return 0;}
  s->cache_n=0;
 }
 return 1;
}
static void bytes(stream *s,void *p,size_t n,int read){
 if(s->bad)return;
 if(s->at>s->n||n>s->n-s->at){s->bad=1;return;}
 uint8_t *v=p;
 while(n){
  size_t chunk=n;
  if(s->base){
   if(read)memcpy(v,s->base+s->at,chunk);else memcpy(s->base+s->at,v,chunk);
   s->crc=bb_crc32_add(s->crc,v,chunk);
  }else if(read){
   if(!s->cache_n||s->at<s->cache_at||s->at>=s->cache_at+s->cache_n){
    s->cache_at=s->at;s->cache_n=s->n-s->at;if(s->cache_n>sizeof s->cache)s->cache_n=sizeof s->cache;
    if(!s->read||!s->read(s->context,s->at,s->cache,s->cache_n)){s->bad=2;return;}
   }
   size_t left=s->cache_at+s->cache_n-s->at;if(chunk>left)chunk=left;
   memcpy(v,s->cache+(s->at-s->cache_at),chunk);s->crc=bb_crc32_add(s->crc,v,chunk);
  }else{
   if(!s->cache_n)s->cache_at=s->at;
   size_t left=sizeof s->cache-s->cache_n;if(chunk>left)chunk=left;
   memcpy(s->cache+s->cache_n,v,chunk);s->cache_n+=chunk;s->crc=bb_crc32_add(s->crc,v,chunk);
   if(s->cache_n==sizeof s->cache&&!flush(s))return;
  }
  s->at+=chunk;v+=chunk;n-=chunk;
 }
}
static uint32_t word(stream *s,uint32_t v,unsigned n,int read){
 uint8_t data[4]={0};if(!n||n>4){s->bad=1;return 0;}
 if(!read)for(unsigned i=0;i<n;i++)data[i]=(uint8_t)(v>>(8*i));
 bytes(s,data,n,read);if(read){v=0;for(unsigned i=0;i<n;i++)v|=(uint32_t)data[i]<<(8*i);}
 return v;
}
static void part(stream *s,bb_arr_part *p,int read){
 p->id=word(s,p->id,4,read);p->revision=word(s,p->revision,4,read);p->source=(uint8_t)word(s,p->source,1,read);
}
static unsigned text_length(const char *p,unsigned max){unsigned n=0;while(n<max&&p[n])n++;return n;}
static void text(stream *s,char *p,int read){
 unsigned n=read?word(s,0,1,1):text_length(p,BB_ARR_SOURCE_KEY);
 if(!read)word(s,n,1,0);
 if(s->bad)return;
 if(!n||n>=BB_ARR_SOURCE_KEY){s->bad=1;return;}
 bytes(s,p,n,read);if(s->bad)return;if(read){for(unsigned i=0;i<n;i++)if(!p[i])s->bad=1;p[n]=0;}
}
static int ref_valid(const bb_arr_sources *s,bb_arr_part p,unsigned kind,unsigned before){
 if(p.source==BB_ARR_CURATED){bb_factory_choice choice;return !bb_arr_source_choice(s,p,kind,&choice);}
 return p.source==BB_ARR_WORKING&&p.id&&p.id<=s->work_count&&p.revision&&p.revision<before&&
        p.revision==s->work[p.id-1].revision&&s->work[p.id-1].kind==kind;
}
static int project_valid(const bb_arrangement *a,const bb_arr_sources *s){
 if(!bb_arr_validate(a)||!s->catalogue||!s->catalogue->validated||
    !s->catalogue_crc||s->catalogue_crc!=s->catalogue->container.crc||
    s->catalogue_stamp!=s->catalogue->container.stamp||s->count>BB_ARR_SOURCE_REFS||
    s->work_count>BB_ARR_WORK_PARTS||!s->next_revision)return 0;
 for(unsigned i=0;i<s->count;i++){
  const bb_arr_source_entry *e=&s->entry[i];
  if(e->kind>BB_SOURCE_DILLO||e->group>2||e->bank_name>1||e->part_name>1||
     text_length(e->bank,BB_ARR_SOURCE_KEY)>=BB_ARR_SOURCE_KEY||text_length(e->part,BB_ARR_SOURCE_KEY)>=BB_ARR_SOURCE_KEY||
     !ref_valid(s,(bb_arr_part){i+1,s->catalogue_crc,BB_ARR_CURATED},e->kind,0))return 0;
 }
 for(unsigned i=0;i<s->work_count;i++){
  const bb_arr_work_part *w=&s->work[i];
  if(!w->revision){
   const uint8_t *bytes=(const uint8_t*)w;
   for(unsigned j=0;j<sizeof *w;j++)if(bytes[j])return 0;
   continue; /* Explicit cleared slot; never infer liveness from current rows. */
  }
  if(w->kind>BB_SOURCE_DILLO||w->revision>=s->next_revision||
     !ref_valid(s,w->origin,w->kind,w->revision))return 0;
  for(unsigned j=0;j<i;j++)if(s->work[j].revision==w->revision)return 0;
  unsigned length=w->kind==BB_SOURCE_DRUM?w->data.drum.phrase.length:w->data.dillo.length;
  if(!length||length>BB_STEPS||(length<64&&(w->protected_steps>>length)))return 0;
  if(w->kind==BB_SOURCE_DRUM){
   if(!bb_phrase_index_valid(&w->data.drum,BB_RICH_REPRESENTABLE_VOICES))return 0;
   for(unsigned n=w->data.drum.phrase.count;n<BB_FACTORY_DRUM_EVENTS;n++)if(w->data.drum.phrase.events[n])return 0;
  }else if(!bb_audio_pattern_data_valid(&w->data.dillo))return 0;
 }
 for(unsigned i=0;i<BB_ARR_SECTIONS;i++)if(a->sections[i].id)
  for(unsigned m=0;m<3;m++){
   bb_arr_part r=a->sections[i].part[m];
   if(m==2&&!r.id)continue;
   if(!ref_valid(s,r,m==2?BB_SOURCE_DILLO:BB_SOURCE_DRUM,UINT32_MAX))return 0;
  }
 return 1;
}
static uint32_t checksum(const uint8_t *p,size_t n){
 uint32_t crc=bb_crc32_add(UINT32_MAX,p,12);crc=bb_crc32_add(crc,p+16,n-16);return ~crc;
}
/* Little endian fields, no struct padding, cached indexes or runtime pointers.
 * Working phrases retain source event order; indexes are rebuilt on open. */
static void body(stream *s,bb_arrangement *a,bb_arr_sources *v,int read,unsigned sections,unsigned version){
 unsigned found=0;
 for(unsigned i=0;i<BB_ARR_SECTIONS;i++){
  bb_arr_section *q=&a->sections[i];if(!read&&!q->id)continue;
  if(read&&found==sections)break;found++;
  q->id=word(s,q->id,4,read);if(s->bad)return;if(read&&!q->id){s->bad=1;return;}q->revision=word(s,q->revision,4,read);bytes(s,q->name,BB_ARR_NAME,read);
  for(unsigned m=0;m<3;m++)part(s,&q->part[m],read);
  q->duration=(uint16_t)word(s,q->duration,2,read);
 }
 for(unsigned i=0;i<a->row_count;i++){a->rows[i].section=word(s,a->rows[i].section,4,read);a->rows[i].repeats=(uint8_t)word(s,a->rows[i].repeats,1,read);}
 for(unsigned i=0;i<v->count;i++){
  bb_arr_source_entry *q=&v->entry[i];text(s,q->bank,read);text(s,q->part,read);
  q->group=(uint8_t)word(s,q->group,1,read);q->kind=(uint8_t)word(s,q->kind,1,read);
  q->bank_name=(uint8_t)word(s,q->bank_name,1,read);q->part_name=(uint8_t)word(s,q->part_name,1,read);
 }
 for(unsigned i=0;i<v->work_count;i++){
  bb_arr_work_part *q=&v->work[i];q->revision=word(s,q->revision,4,read);part(s,&q->origin,read);
  uint32_t lo=word(s,(uint32_t)q->protected_steps,4,read),hi=word(s,(uint32_t)(q->protected_steps>>32),4,read);
  q->protected_steps=lo|((uint64_t)hi<<32);q->kind=(uint8_t)word(s,q->kind,1,read);
  if(q->kind==BB_SOURCE_DRUM){
   bb_phrase *r=&q->data.drum.phrase;
   r->length=(uint8_t)word(s,r->length,1,read);r->count=(uint8_t)word(s,r->count,1,read);
   r->lattice=(uint8_t)word(s,r->lattice,1,read);r->bars=(uint8_t)word(s,r->bars,1,read);
   if(r->count>BB_FACTORY_DRUM_EVENTS){s->bad=1;return;}
   for(unsigned n=0;n<r->count;n++)r->events[n]=(uint16_t)word(s,r->events[n],2,read);
   if(version>=2u)bytes(s,r->ratchets,(r->count+3u)/4u,read);
  }else if(q->kind==BB_SOURCE_DILLO){
   bb_pattern_data *r=&q->data.dillo;r->length=(uint8_t)word(s,r->length,1,read);
   for(unsigned n=0;n<BB_STEPS;n++){
    r->notes[n].note=(uint8_t)word(s,r->notes[n].note,1,read);r->notes[n].gate=(uint8_t)word(s,r->notes[n].gate,1,read);
    r->notes[n].accent=(uint8_t)word(s,r->notes[n].accent,1,read);r->notes[n].slide=(uint8_t)word(s,r->notes[n].slide,1,read);
    r->expression[n]=(bb_note_expression)word(s,r->expression[n],2,read);
   }
  }else{s->bad=1;return;}
 }
}
/* Runtime slots keep stable ID/revision identities. Wire v2 remains dense and
 * chronological: map every occupied payload and both reference families.
 * This is serialization, not ownership discovery or reclamation. */
static unsigned work_map(const bb_arr_sources *s,uint8_t *order,uint8_t *map){
 unsigned n=0;memset(map,0,BB_ARR_WORK_PARTS);
 for(unsigned i=0;i<s->work_count;i++)if(s->work[i].revision){
  unsigned at=n;
  while(at&&s->work[order[at-1u]].revision>s->work[i].revision){order[at]=order[at-1u];at--;}
  order[at]=(uint8_t)i;n++;
 }
 for(unsigned i=0;i<n;i++)map[order[i]]=(uint8_t)(i+1u);
 return n;
}
static void mapped_part(stream *s,bb_arr_part p,const uint8_t *map){
 if(p.source==BB_ARR_WORKING)p.id=map[p.id-1u];
 part(s,&p,0);
}
static size_t extent(const bb_arrangement *a,const bb_arr_sources *s,size_t scene,unsigned *sections){
 size_t n=BB_PROJECT_HEADER+scene+5u*a->row_count;*sections=0;
 for(unsigned i=0;i<BB_ARR_SECTIONS;i++)if(a->sections[i].id){n+=61;(*sections)++;}
 for(unsigned i=0;i<s->count;i++)n+=6u+text_length(s->entry[i].bank,BB_ARR_SOURCE_KEY)+text_length(s->entry[i].part,BB_ARR_SOURCE_KEY);
 for(unsigned i=0;i<s->work_count;i++)if(s->work[i].revision)n+=22u+(s->work[i].kind==BB_SOURCE_DRUM?4u+2u*s->work[i].data.drum.phrase.count+(s->work[i].data.drum.phrase.count+3u)/4u:1u+6u*BB_STEPS);
 return n;
}
/* No project-sized copy: the two immutable source objects may be independently
 * placed. Both input memory and callbacks stay under caller snapshot ownership. */
static int encode(const bb_arrangement *a,const bb_arr_sources *v,bb_project_assets assets,
                   const void *scene,size_t n,bb_project_scene_check check,void *context,
                   stream *s,size_t *written){
 if(!a||!v||!written||!scene||!n||n>BB_PROJECT_SCENE_MAX||!check||!assets.library_header_crc||!assets.library_payload_crc)return BB_PROJECT_INVALID;
 if(!bb_wire_separated(a,sizeof *a,v,sizeof *v)||!bb_wire_separated(a,sizeof *a,written,sizeof *written)||
    !bb_wire_separated(v,sizeof *v,written,sizeof *written)||!bb_wire_separated(scene,n,written,sizeof *written))return BB_PROJECT_ALIAS;
 if(!project_valid(a,v))return BB_PROJECT_SOURCE;
 if(!bb_catalogue_private(v->catalogue,written,sizeof *written)||(s->base&&!bb_catalogue_private(v->catalogue,s->base,s->n)))return BB_PROJECT_ALIAS;
 if(!check(context,scene,n))return BB_PROJECT_SCENE;
 uint8_t order[BB_ARR_WORK_PARTS],map[BB_ARR_WORK_PARTS];
 unsigned mapped=work_map(v,order,map);
 unsigned count;size_t size=extent(a,v,n,&count);if(size>BB_PROJECT_MAX_BYTES||size>s->n)return BB_PROJECT_CAPACITY;
 uint32_t revision=a->revision,next=v->next_revision;unsigned refs=v->count,works=v->work_count,rows=a->row_count;
 uint8_t header[BB_PROJECT_HEADER]={0};memcpy(header,"BBP2",4);header[4]=2;header[6]=BB_PROJECT_HEADER;
 bb_wire_put32(header+8,(uint32_t)size);bb_wire_put32(header+16,2);bb_wire_put32(header+20,v->catalogue_crc);bb_wire_put32(header+24,v->catalogue_stamp);
 bb_wire_put32(header+28,assets.library_header_crc);bb_wire_put32(header+32,assets.library_payload_crc);bb_wire_put32(header+36,(uint32_t)n);
 header[40]=(uint8_t)count;header[41]=a->row_count;header[42]=v->count;header[43]=(uint8_t)mapped;
 bb_wire_put32(header+44,a->revision);bb_wire_put32(header+48,a->next_id);bb_wire_put32(header+52,v->next_revision);
 s->n=size;s->at=BB_PROJECT_HEADER;s->crc=bb_crc32_add(UINT32_MAX,header,12);s->crc=bb_crc32_add(s->crc,header+16,BB_PROJECT_HEADER-16);
 for(unsigned i=0;i<BB_ARR_SECTIONS;i++)if(a->sections[i].id){
  const bb_arr_section *q=&a->sections[i];word(s,q->id,4,0);word(s,q->revision,4,0);bytes(s,(void*)q->name,BB_ARR_NAME,0);
  for(unsigned m=0;m<3;m++)mapped_part(s,q->part[m],map);word(s,q->duration,2,0);
 }
 for(unsigned i=0;i<rows;i++){word(s,a->rows[i].section,4,0);word(s,a->rows[i].repeats,1,0);}
 for(unsigned i=0;i<refs;i++){
  const bb_arr_source_entry *q=&v->entry[i];text(s,(char*)q->bank,0);text(s,(char*)q->part,0);word(s,q->group,1,0);word(s,q->kind,1,0);word(s,q->bank_name,1,0);word(s,q->part_name,1,0);
 }
 for(unsigned i=0;i<mapped;i++){
  const bb_arr_work_part *q=&v->work[order[i]];word(s,q->revision,4,0);mapped_part(s,q->origin,map);
  word(s,(uint32_t)q->protected_steps,4,0);word(s,(uint32_t)(q->protected_steps>>32),4,0);word(s,q->kind,1,0);
  if(q->kind==BB_SOURCE_DRUM){const bb_phrase *r=&q->data.drum.phrase;unsigned events=r->count;if(events>BB_FACTORY_DRUM_EVENTS)return BB_PROJECT_CONFLICT;word(s,r->length,1,0);word(s,events,1,0);word(s,r->lattice,1,0);word(s,r->bars,1,0);for(unsigned j=0;j<events;j++)word(s,r->events[j],2,0);bytes(s,(void*)r->ratchets,(events+3u)/4u,0);}
  else{const bb_pattern_data *r=&q->data.dillo;word(s,r->length,1,0);for(unsigned j=0;j<BB_STEPS;j++){word(s,r->notes[j].note,1,0);word(s,r->notes[j].gate,1,0);word(s,r->notes[j].accent,1,0);word(s,r->notes[j].slide,1,0);word(s,r->expression[j],2,0);}}
 }
 bytes(s,(void*)scene,n,0);if(s->bad||!flush(s))return s->bad==2?BB_PROJECT_IO:BB_PROJECT_INVALID;
 if(s->at!=size)return BB_PROJECT_INVALID;
 if(a->revision!=revision||v->next_revision!=next||v->count!=refs||v->work_count!=works)return BB_PROJECT_CONFLICT;
 bb_wire_put32(header+12,~s->crc);
 if(s->base)memcpy(s->base,header,sizeof header);
 else if(!s->write(s->context,0,header,sizeof header))return BB_PROJECT_IO;
 *written=size;return BB_PROJECT_OK;
}
int bb_project_encode(const bb_project *p,bb_project_assets assets,const void *scene,size_t n,
                      bb_project_scene_check check,void *context,void *wire,size_t cap,size_t *written){
 if(!p||!wire||!written||!scene)return BB_PROJECT_INVALID;
 if(!bb_wire_separated(p,sizeof *p,wire,cap)||!bb_wire_separated(scene,n,wire,cap)||!bb_wire_separated(written,sizeof *written,wire,cap))return BB_PROJECT_ALIAS;
 stream s;memset(&s,0,sizeof s);s.base=wire;s.n=cap;
 return encode(&p->arrangement,&p->sources,assets,scene,n,check,context,&s,written);
}
int bb_project_write_stream(const bb_arrangement *a,const bb_arr_sources *v,bb_project_assets assets,
                            const void *scene,size_t n,bb_project_scene_check check,void *context,
                            bb_project_write_at write,void *io_context,size_t *written){
 if(!write)return BB_PROJECT_INVALID;
 stream s;memset(&s,0,sizeof s);s.write=write;s.context=io_context;s.n=BB_PROJECT_MAX_BYTES;
 return encode(a,v,assets,scene,n,check,context,&s,written);
}
static int open_stage(stream *s,const bb_catalogue *c,bb_project_assets assets,
                       bb_project_scene_check check,void *context,bb_arrangement *a,bb_arr_sources *v,
                       void *nested_buffer,size_t nested_capacity,const void **scene,size_t *scene_bytes){
 if(!a||!v||!c||!check||!scene||!scene_bytes||s->n<BB_PROJECT_HEADER||s->n>BB_PROJECT_MAX_BYTES)return BB_PROJECT_INVALID;
 const void *out[4]={a,v,scene,scene_bytes};size_t sizes[4]={sizeof *a,sizeof *v,sizeof *scene,sizeof *scene_bytes};
 for(unsigned i=0;i<4;i++){
  if((s->base&&!bb_wire_separated(s->base,s->n,out[i],sizes[i]))||!bb_catalogue_private(c,out[i],sizes[i]))return BB_PROJECT_ALIAS;
  for(unsigned j=0;j<i;j++)if(!bb_wire_separated(out[i],sizes[i],out[j],sizes[j]))return BB_PROJECT_ALIAS;
 }
 uint8_t header[BB_PROJECT_HEADER];
 if(s->base)memcpy(header,s->base,sizeof header);else if(!s->read||!s->read(s->context,0,header,sizeof header))return BB_PROJECT_IO;
 if(memcmp(header,"BBP2",4)||(header[4]!=1&&header[4]!=2)||header[5]||header[6]!=BB_PROJECT_HEADER||header[7]||bb_wire_get32(header+16)!=header[4])return BB_PROJECT_VERSION;
 for(unsigned i=56;i<64;i++)if(header[i])return BB_PROJECT_VERSION;
 if(bb_wire_get32(header+8)!=s->n)return BB_PROJECT_INVALID;
 /* Memory input retains its previous checksum-first / borrowed-scene contract. */
 if(s->base&&bb_wire_get32(header+12)!=checksum(s->base,s->n))return BB_PROJECT_CHECKSUM;
 if(!c->validated||bb_wire_get32(header+20)!=c->container.crc||bb_wire_get32(header+24)!=c->container.stamp)return BB_PROJECT_CATALOGUE;
 if(!assets.library_header_crc||!assets.library_payload_crc||bb_wire_get32(header+28)!=assets.library_header_crc||bb_wire_get32(header+32)!=assets.library_payload_crc)return BB_PROJECT_ASSETS;
 size_t n=bb_wire_get32(header+36);if(!n||n>BB_PROJECT_SCENE_MAX||n>s->n-BB_PROJECT_HEADER||header[40]>BB_ARR_SECTIONS||header[41]>BB_ARR_ROWS||header[42]>BB_ARR_SOURCE_REFS||header[43]>BB_ARR_WORK_PARTS)return BB_PROJECT_INVALID;
 if(!s->base){
  if(!nested_buffer||nested_capacity<n)return BB_PROJECT_CAPACITY;
  if(!bb_catalogue_private(c,nested_buffer,n))return BB_PROJECT_ALIAS;
  for(unsigned i=0;i<4;i++)if(!bb_wire_separated(nested_buffer,n,out[i],sizes[i]))return BB_PROJECT_ALIAS;
 }
 memset(a,0,sizeof *a);memset(v,0,sizeof *v);v->catalogue=c;v->catalogue_crc=c->container.crc;v->catalogue_stamp=c->container.stamp;
 a->revision=a->saved_revision=bb_wire_get32(header+44);a->next_id=bb_wire_get32(header+48);v->next_revision=bb_wire_get32(header+52);
 a->row_count=header[41];v->count=header[42];v->work_count=header[43];s->at=BB_PROJECT_HEADER;s->crc=bb_crc32_add(UINT32_MAX,header,12);s->crc=bb_crc32_add(s->crc,header+16,BB_PROJECT_HEADER-16);
 body(s,a,v,1,header[40],header[4]);if(s->bad)return s->bad==2?BB_PROJECT_IO:BB_PROJECT_INVALID;if(s->at!=s->n-n)return BB_PROJECT_INVALID;
 const void *nested;
 if(s->base){nested=s->base+s->at;s->crc=bb_crc32_add(s->crc,nested,n);s->at+=n;}
 else{bytes(s,nested_buffer,n,1);nested=nested_buffer;}
 if(s->bad)return s->bad==2?BB_PROJECT_IO:BB_PROJECT_INVALID;
 if(s->at!=s->n||~s->crc!=bb_wire_get32(header+12))return BB_PROJECT_CHECKSUM;
 if(!check(context,nested,n))return BB_PROJECT_SCENE;
 for(unsigned i=0;i<v->work_count;i++){
  const bb_arr_work_part *w=&v->work[i];
  if(!w->revision||(i&&w->revision<=v->work[i-1u].revision)||
     (w->origin.source==BB_ARR_WORKING&&(!w->origin.id||w->origin.id>i)))return BB_PROJECT_SOURCE;
 }
 if(bb_arr_sources_reindex_staged(v))return BB_PROJECT_SOURCE;
 for(unsigned i=0;i<v->work_count;i++)if(v->work[i].kind==BB_SOURCE_DRUM){
  bb_phrase phrase=v->work[i].data.drum.phrase;
  if(!bb_phrase_index_prepare(&phrase,BB_RICH_REPRESENTABLE_VOICES,&v->work[i].data.drum))return BB_PROJECT_SOURCE;
 }
 if(bb_arr_work_rebuild(a)||bb_arr_source_owners_rebuild(v)||!project_valid(a,v))return BB_PROJECT_SOURCE;*scene=nested;*scene_bytes=n;return BB_PROJECT_OK;
}
int bb_project_open_staged(const void *wire,size_t size,const bb_catalogue *c,bb_project_assets assets,
                           bb_project_scene_check check,void *context,bb_project *p,const void **scene,size_t *scene_bytes){
 if(!wire||!p)return BB_PROJECT_INVALID;
 stream s;memset(&s,0,sizeof s);s.base=(uint8_t*)wire;s.n=size;
 return open_stage(&s,c,assets,check,context,&p->arrangement,&p->sources,NULL,0,scene,scene_bytes);
}
int bb_project_read_stream_staged(bb_project_read_at read,void *io_context,size_t size,const bb_catalogue *c,
                                  bb_project_assets assets,bb_project_scene_check check,void *context,
                                  bb_arrangement *a,bb_arr_sources *v,void *nested,size_t capacity,
                                  const void **scene,size_t *scene_bytes){
 if(!read)return BB_PROJECT_INVALID;
 stream s;memset(&s,0,sizeof s);s.read=read;s.context=io_context;s.n=size;
 return open_stage(&s,c,assets,check,context,a,v,nested,capacity,scene,scene_bytes);
}

/* File publication is kept outside the codec: no filename, flash allocation,
 * framebuffer borrowing or OS policy enters this main-loop transaction. */
typedef struct {
 const bb_project_file_ops *ops;void *context;
 uint8_t header[BB_PROJECT_HEADER];unsigned header_written,header_mismatch;
} project_file;
static int project_file_write(void *context,size_t at,const void *p,size_t n){
 project_file *f=context;
 if(!f->ops->write(f->context,at,p,n))return 0;
 if(!at){if(n!=sizeof f->header)return 0;memcpy(f->header,p,n);f->header_written=1;}
 return 1;
}
static int project_file_read(void *context,size_t at,void *p,size_t n){
 project_file *f=context;
 if(!f->ops->read(f->context,at,p,n))return 0;
 if(!at&&(n!=sizeof f->header||memcmp(f->header,p,n))){f->header_mismatch=1;return 0;}
 return 1;
}
int bb_project_save_verified(bb_arrangement *a,const bb_arr_sources *v,bb_project_assets assets,
 const void *scene,size_t scene_bytes,bb_project_scene_check check,void *check_context,
 const bb_project_file_ops *ops,void *io_context,bb_arrangement *stage,bb_arr_sources *sources,
 void *nested,size_t capacity,size_t *written){
 if(!a||!v||!scene||!scene_bytes||scene_bytes>BB_PROJECT_SCENE_MAX||!stage||!sources||!nested||!written||!check||!ops||!ops->begin||!ops->write||
    !ops->seal||!ops->read||!ops->publish||!ops->abort)return BB_PROJECT_INVALID;
 /* Reject aliases before begin can acquire a file or staging can mutate music. */
 const void *live[3]={a,v,scene};size_t lengths[3]={sizeof *a,sizeof *v,scene_bytes};
 const void *scratch[3]={stage,sources,nested};size_t sizes[3]={sizeof *stage,sizeof *sources,capacity};
 for(unsigned i=0;i<3;i++)for(unsigned j=0;j<3;j++)
  if(!bb_wire_separated(live[i],lengths[i],scratch[j],sizes[j]))return BB_PROJECT_ALIAS;
 for(unsigned i=0;i<3;i++){
  if(!bb_wire_separated(live[i],lengths[i],written,sizeof *written))return BB_PROJECT_ALIAS;
  if(!bb_catalogue_private(v->catalogue,scratch[i],sizes[i])||
     !bb_wire_separated(ops,sizeof *ops,scratch[i],sizes[i])||
     !bb_wire_separated(written,sizeof *written,scratch[i],sizes[i]))return BB_PROJECT_ALIAS;
  for(unsigned j=0;j<i;j++)if(!bb_wire_separated(scratch[i],sizes[i],scratch[j],sizes[j]))return BB_PROJECT_ALIAS;
 }
 if(!bb_catalogue_private(v->catalogue,written,sizeof *written))return BB_PROJECT_ALIAS;
 if(!bb_arr_validate(a))return BB_PROJECT_INVALID;
 uint32_t revision=a->revision,next=v->next_revision;unsigned refs=v->count,works=v->work_count;
 if(!ops->begin(io_context))return BB_PROJECT_IO;
 project_file f;memset(&f,0,sizeof f);f.ops=ops;f.context=io_context;
 size_t bytes=0,read_bytes=0,n=0;const void *read_scene=NULL;
 int result=bb_project_write_stream(a,v,assets,scene,scene_bytes,check,check_context,project_file_write,&f,&bytes);
 if(!result&&(!f.header_written||!ops->seal(io_context,bytes,&read_bytes)))result=BB_PROJECT_IO;
 if(!result&&read_bytes!=bytes)result=BB_PROJECT_FILE_VERIFY;
 if(!result){result=bb_project_read_stream_staged(project_file_read,&f,bytes,v->catalogue,assets,
   check,check_context,stage,sources,nested,capacity,&read_scene,&n);
  if(f.header_mismatch)result=BB_PROJECT_FILE_VERIFY;
 }
 if(!result&&(a->revision!=revision||v->next_revision!=next||v->count!=refs||v->work_count!=works))result=BB_PROJECT_CONFLICT;
 if(!result&&!ops->publish(io_context))result=BB_PROJECT_FILE_PUBLISH;
 if(result){ops->abort(io_context);return result;}
 /* Even a contract-violating publication callback cannot mark a newer edit
  * saved. Published bytes remain the verified captured revision in that case. */
 if(bb_arr_saved(a,revision))return BB_PROJECT_CONFLICT;
 *written=bytes;return BB_PROJECT_OK;
}
