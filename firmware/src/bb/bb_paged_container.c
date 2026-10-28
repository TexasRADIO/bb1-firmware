/* SPDX-License-Identifier: GPL-3.0-only */
#include "bb_paged_container.h"
#include "bb_catalogue_page.h"
#include "bb_crc32.h"
#include <string.h>
#define STAMP UINT32_C(0x42504331)
static uint16_t u16(const uint8_t*p){return (uint16_t)((unsigned)p[0]|((unsigned)p[1]<<8));}
static uint32_t u32(const uint8_t*p){return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);}
static int span(const void*p,size_t n){return p && n<=UINTPTR_MAX-(uintptr_t)p;}
static int disjoint(const void*a,size_t an,const void*b,size_t bn){
    uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;return x<=y?an<=y-x:bn<=x-y;
}
static int view_ok(const bb_pc_view*v){
    return span(v,sizeof *v) && v->stamp==STAMP && span(v->bytes,v->length) &&
        v->length>=64 && v->length<=BB_PC_MAX_BYTES && span(v->cache,sizeof *v->cache) &&
        v->metadata_bytes>0 && v->metadata_bytes<=BB_PC_MAX_BYTES &&
        v->block_count==(v->metadata_bytes+BB_PC_PAGE-1)/BB_PC_PAGE &&
        v->blocks<=v->length && 8u*v->block_count<=v->length-v->blocks &&
        v->text<=v->length && v->text_bytes<=v->length-v->text && v->payload<=v->length;
}
static int private_output(const bb_pc_view*v,const void*out,size_t n){
    return span(out,n) && disjoint(out,n,v,sizeof *v) &&
        disjoint(out,n,v->bytes,v->length) && disjoint(out,n,v->cache,sizeof *v->cache);
}
static void bind_cache(const bb_pc_view*v){
    bb_pc_cache*c=v->cache;
    if(c->source!=v->bytes || c->length!=v->length || c->crc!=v->crc){
        c->source=v->bytes;c->length=v->length;c->crc=v->crc;
        c->valid[0]=c->valid[1]=0;c->next=0;
    }
}
static const uint8_t *page(const bb_pc_view*v,uint32_t index){
    if(index>=v->block_count)return NULL;
    bb_pc_cache*c=v->cache;
    for(unsigned i=0;i<2;i++)if(c->valid[i] && c->tag[i]==index){
        c->next=(uint8_t)(i^1u);if(c->hits!=UINT32_MAX)c->hits++;return c->page[i];
    }
    const uint8_t*d=v->bytes+v->blocks+8u*index;
    uint32_t offset=u32(d);unsigned field=u16(d+4),encoded=field&32767u,decoded=u16(d+6);
    unsigned expected=v->metadata_bytes-index*BB_PC_PAGE;if(expected>BB_PC_PAGE)expected=BB_PC_PAGE;
    if(!encoded || decoded!=expected || encoded>decoded || offset>v->length-v->payload ||
       encoded>v->length-v->payload-offset)return NULL;
    unsigned raw=field>>15;if((raw && encoded!=decoded)||(!raw && encoded>=decoded))return NULL;
    unsigned slot=c->next&1u;c->valid[slot]=0;
    if(!bb_catalogue_page_decode(c->page[slot],sizeof c->page[slot],v->bytes+v->payload+offset,encoded,decoded,
        raw?BB_PAGE_RAW:BB_PAGE_LZ4))return NULL;
    c->tag[slot]=index;c->valid[slot]=1;c->next=(uint8_t)(slot^1u);
    if(c->misses!=UINT32_MAX)c->misses++;return c->page[slot];
}
int bb_pc_open_container(bb_pc_view*out,const uint8_t*bytes,size_t n,bb_pc_cache*cache){
    if(!span(out,sizeof *out)||!span(cache,sizeof *cache)||!span(bytes,n)||n<64||n>BB_PC_MAX_BYTES ||
       (uintptr_t)out%_Alignof(bb_pc_view)||(uintptr_t)cache%_Alignof(bb_pc_cache)||
       !disjoint(out,sizeof *out,cache,sizeof *cache)||!disjoint(out,sizeof *out,bytes,n)||
       !disjoint(cache,sizeof *cache,bytes,n))return 0;
    if(memcmp(bytes,"BBPC",4)||u16(bytes+4)!=2||u16(bytes+6)!=BB_PC_PAGE||u32(bytes+8)!=n||u16(bytes+30)||u32(bytes+60))return 0;
    bb_pc_view v={.bytes=bytes,.length=n,.cache=cache,.nodes=0,.root=0,.metadata_bytes=0,
        .sparse=0,.dictionary=0,.blocks=0,.block_count=0,.text=0,.text_bytes=0,.payload=0,
        .crc=u32(bytes+12),.stamp=0,.shapes=0};
    uint32_t crc=bb_crc32_add(UINT32_MAX,bytes,12);crc=bb_crc32_add(crc,bytes+16,n-16)^UINT32_MAX;
    if(crc!=v.crc)return 0;
    v.nodes=u32(bytes+16);v.root=u32(bytes+20);v.metadata_bytes=u32(bytes+24);v.shapes=u16(bytes+28);
    if(!v.nodes||v.nodes>65535||v.root>=v.nodes||v.metadata_bytes<v.nodes||v.metadata_bytes>BB_PC_MAX_BYTES)return 0;
    v.sparse=u32(bytes+32);v.dictionary=u32(bytes+36);v.blocks=u32(bytes+40);v.block_count=u32(bytes+44);
    v.text=u32(bytes+48);v.text_bytes=u32(bytes+52);v.payload=u32(bytes+56);
    uint32_t sparse_count=(v.nodes+15u)/16u;
    if(v.sparse!=64||v.dictionary!=64+4u*sparse_count||v.blocks!=v.dictionary+4u*v.shapes||
       v.block_count!=(v.metadata_bytes+BB_PC_PAGE-1)/BB_PC_PAGE||v.text!=v.blocks+8u*v.block_count||
       v.text>n||v.text_bytes>n-v.text||v.payload!=v.text+v.text_bytes)return 0;
    uint32_t previous=0;
    for(uint32_t i=0;i<sparse_count;i++){
        uint32_t at=u32(bytes+v.sparse+4u*i);
        if(at>=v.metadata_bytes||(!i && at)||(i && at<=previous))return 0;previous=at;
    }
    for(unsigned i=0;i<v.shapes;i++)if(u16(bytes+v.dictionary+4u*i)>=v.nodes || u16(bytes+v.dictionary+4u*i+2)>=v.nodes)return 0;
    uint32_t extent=0;
    for(uint32_t i=0;i<v.block_count;i++){
        const uint8_t*d=bytes+v.blocks+8u*i;unsigned field=u16(d+4),encoded=field&32767u,decoded=u16(d+6);
        unsigned expected=v.metadata_bytes-i*BB_PC_PAGE;if(expected>BB_PC_PAGE)expected=BB_PC_PAGE;
        if(u32(d)!=extent||!encoded||decoded!=expected||encoded>decoded||
           ((field>>15)?encoded!=decoded:encoded>=decoded)||extent>n-v.payload||encoded>n-v.payload-extent)return 0;
        extent+=encoded;
    }
    if(extent!=n-v.payload)return 0;
    // Cache is caller-private scratch. Reset on every open, including a new
    // container at a reused address. Old views rebind it on their next read.
    memset(cache,0,sizeof *cache);v.stamp=STAMP;bind_cache(&v);
    for(uint32_t i=0;i<v.block_count;i++)if(!page(&v,i))return 0;
    *out=v;return 1;
}
int bb_pc_read_metadata(const bb_pc_view*v,uint32_t at,void*out,size_t n){
    if(!view_ok(v)||at>v->metadata_bytes||n>v->metadata_bytes-at||!private_output(v,out,n))return 0;
    if(!n)return 1;bind_cache(v);uint8_t*to=out;
    while(n){
        uint32_t block=at/BB_PC_PAGE,within=at%BB_PC_PAGE;const uint8_t*p=page(v,block);if(!p)return 0;
        size_t count=BB_PC_PAGE-within;if(count>n)count=n;
        for(size_t i=0;i<count;i++)to[i]=p[within+i];
        to+=count;at+=(uint32_t)count;n-=count;
    }
    return 1;
}
int bb_pc_text_bytes(const bb_pc_view*v,uint32_t at,size_t n,const uint8_t**out){
    if(!view_ok(v)||at>v->text_bytes||n>v->text_bytes-at||!private_output(v,out,sizeof *out)||
       (uintptr_t)out%_Alignof(const uint8_t*))return 0;
    *out=v->bytes+v->text+at;return 1;
}
