/* SPDX-License-Identifier: GPL-3.0-only */
#include "bb_catalogue_page.h"
int bb_catalogue_page_span(const void *p,size_t n){return p && n<=UINTPTR_MAX-(uintptr_t)p;}
#define span(p,n) bb_catalogue_page_span(p,n)
int bb_catalogue_page_disjoint(const void *a,size_t an,const void *b,size_t bn){
    uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;
    return x<=y?an<=y-x:bn<=x-y;
}
#define separate(a,b,c,d) bb_catalogue_page_disjoint(a,b,c,d)
/* Independently written RFC1951 fixed-Huffman/stored-block subset.
 * Each page is an independent raw stream: output itself is the bounded
 * history window. No heap, global table, recursion or audio-owner state.
 * Dynamic Huffman blocks are deliberately unsupported by BBPC v3.
 */
typedef struct { const uint8_t *at,*end; uint32_t bits;unsigned have; } bb_def_bits;
static int def_take(bb_def_bits *b,unsigned n,unsigned *out){
    while(b->have<n){if(b->at==b->end)return 0;b->bits|=(uint32_t)*b->at++<<b->have;b->have+=8;}
    *out=b->bits&((1u<<n)-1u);b->bits>>=n;b->have-=n;return 1;
}
/* Fixed codes are MSB-first in an LSB-first stream: reverse 5..9 bits with
 * multiplies instead of a per-bit loop (no table, no flash cost). */
static unsigned def_rev8(unsigned x){
    return (((x*0x0802u&0x22110u)|(x*0x8020u&0x88440u))*0x10101u>>16)&255u;
}
static int def_code(bb_def_bits *b,unsigned n,unsigned *out){
    unsigned v;if(!def_take(b,n,&v))return 0;
    *out=def_rev8(v)>>(8u-n);return 1;
}
/* One peek of up to 9 bits decodes any fixed literal/length symbol. The bits
 * past the stream end read as zero, but only bits that exist are consumed. */
static int def_symbol(bb_def_bits *b,unsigned *symbol){
    while(b->have<9u&&b->at!=b->end){b->bits|=(uint32_t)*b->at++<<b->have;b->have+=8;}
    unsigned v=b->bits,c=def_rev8(v&127u)>>1,n;
    if(c<24u){*symbol=256u+c;n=7;}
    else{
        c=(c<<1)|(v>>7&1u);
        if(c>=48u&&c<192u){*symbol=c-48u;n=8;}
        else if(c>=192u&&c<200u){*symbol=280u+c-192u;n=8;}
        else{c=(c<<1)|(v>>8&1u);if(c<400u||c>511u)return 0;*symbol=c-400u+144u;n=9;}
    }
    if(b->have<n)return 0;
    b->bits>>=n;b->have-=n;return 1;
}
static int def_fixed(uint8_t *to,size_t expected,const uint8_t *from,size_t encoded){
    bb_def_bits b={from,from+encoded,0,0};size_t written=0;unsigned final;
    do{
        unsigned kind;if(!def_take(&b,1,&final)||!def_take(&b,2,&kind))return 0;
        if(kind==0){
            unsigned len,inv;b.bits=0;b.have=0;
            if(!def_take(&b,16,&len)||!def_take(&b,16,&inv)||(len^inv)!=65535u||len>expected-written)return 0;
            for(unsigned i=0;i<len;i++){unsigned x;if(!def_take(&b,8,&x))return 0;to[written++]=(uint8_t)x;}
        }else if(kind==1){
            for(;;){
                unsigned symbol;if(!def_symbol(&b,&symbol))return 0;
                if(symbol<256u){if(written==expected)return 0;to[written++]=(uint8_t)symbol;continue;}
                if(symbol==256u)break;
                if(symbol>285u)return 0;
                unsigned len,extra=0,x=0;
                if(symbol<265u)len=symbol-254u;
                else if(symbol==285u)len=258u;
                else {unsigned q=symbol-265u;extra=q/4u+1u;len=3u+((4u+q%4u)<<extra);}
                if(extra&&!def_take(&b,extra,&x))return 0;len+=x;
                unsigned distance;if(!def_code(&b,5,&distance)||distance>29u)return 0;
                if(distance<4u)distance++;
                else {unsigned q=distance;extra=q/2u-1u;if(!def_take(&b,extra,&x))return 0;distance=1u+((2u+(q&1u))<<extra)+x;}
                if(distance>written||len>expected-written)return 0;
                for(unsigned i=0;i<len;i++){to[written]=to[written-distance];written++;}
            }
        }else return 0; /* reserved or dynamic tree: not the v3 contract */
    }while(!final);
    return written==expected&&b.at==b.end&&b.have<8u;   /* read-ahead: no whole unused byte may remain */
}
int bb_catalogue_page_decode(void *output,size_t capacity,const void *input,
    size_t encoded,size_t expected,unsigned codec){
    if(!expected||expected>BB_PAGE_MAX||!encoded||encoded>BB_PAGE_ENCODED_MAX||
       capacity<expected||codec>BB_PAGE_DEFLATE_FIXED||!span(output,capacity)||!span(input,encoded)||
       !separate(output,capacity,input,encoded))return 0;
    uint8_t *to=output;const uint8_t *from=input;
    if(codec==BB_PAGE_RAW){
        if(encoded!=expected)return 0;
        for(size_t i=0;i<expected;i++)to[i]=from[i];
        return 1;
    }
    if(codec==BB_PAGE_DEFLATE_FIXED)return def_fixed(to,expected,from,encoded);
    size_t read=0,written=0;
    while(read<encoded){
        unsigned token=from[read++];size_t literals=token>>4;
        if(literals>expected-written)return 0;
        if(literals==15){
            unsigned extra;
            do{
                if(read==encoded)return 0;
                extra=from[read++];
                if(extra>expected-written-literals)return 0;
                literals+=extra;
            }while(extra==255);
        }
        if(literals>encoded-read)return 0;
        for(size_t i=0;i<literals;i++)to[written++]=from[read++];
        if(read==encoded)return written==expected;
        if(encoded-read<2)return 0;
        size_t distance=(size_t)from[read]|((size_t)from[read+1]<<8);read+=2;
        if(!distance||distance>written)return 0;
        size_t matches=4u+(token&15u);
        if(matches>expected-written)return 0;
        if((token&15u)==15){
            unsigned extra;
            do{
                if(read==encoded)return 0;
                extra=from[read++];
                if(extra>expected-written-matches)return 0;
                matches+=extra;
            }while(extra==255);
        }
        // LZ4 matches may overlap themselves: forward copying is required.
        for(size_t i=0;i<matches;i++){
            to[written]=to[written-distance];written++;
        }
    }
    return 0; /* A final literal sequence is required, not a truncated match. */
}
