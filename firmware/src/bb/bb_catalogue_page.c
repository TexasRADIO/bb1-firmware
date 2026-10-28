/* SPDX-License-Identifier: GPL-3.0-only */
#include "bb_catalogue_page.h"
static int span(const void *p,size_t n){return p && n<=UINTPTR_MAX-(uintptr_t)p;}
static int separate(const void *a,size_t an,const void *b,size_t bn){
    uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;
    return x<=y?an<=y-x:bn<=x-y;
}
int bb_catalogue_page_decode(void *output,size_t capacity,const void *input,
    size_t encoded,size_t expected,unsigned codec){
    if(!expected||expected>BB_PAGE_MAX||!encoded||encoded>BB_PAGE_ENCODED_MAX||
       capacity<expected||codec>BB_PAGE_LZ4||!span(output,capacity)||!span(input,encoded)||
       !separate(output,capacity,input,encoded))return 0;
    uint8_t *to=output;const uint8_t *from=input;
    if(codec==BB_PAGE_RAW){
        if(encoded!=expected)return 0;
        for(size_t i=0;i<expected;i++)to[i]=from[i];
        return 1;
    }
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
