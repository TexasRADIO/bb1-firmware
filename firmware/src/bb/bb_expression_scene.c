/* SPDX-License-Identifier: GPL-3.0-only */
#include "bb_expression_scene.h"
#include "bb_crc32.h"
#include "bb_wire.h"
#include <string.h>
#include <stdint.h>
_Static_assert(BB_SYNTHS==2 && BB_STEPS==64,"version1 fixed metadata shape");
static int known_scene(const uint8_t *p,size_t bytes){
    return bytes>=4 && (!memcmp(p,"BBSC",4)||!memcmp(p,"BBFS",4)||
                        !memcmp(p,"BBGM",4)||!memcmp(p,"BBKS",4));
}
static int playable_word(bb_note_expression word){
    bb_note_expression_view v;
    return bb_note_expression_read(word,&v) &&
           (!v.has_velocity||v.velocity) && (!v.has_gate||v.gate_percent);
}
static uint32_t checksum(const uint8_t *p,size_t bytes){
    return ~bb_crc32_add(bb_crc32_add(UINT32_MAX,p,12),p+16,bytes-16);
}
size_t bb_expression_scene_finish(void *output,size_t capacity,size_t nested,
                                  const bb_note_expression e[BB_SYNTHS][BB_STEPS]){
    const size_t overhead=BB_EXPRESSION_SCENE_OVERHEAD;
    if(!output||!e||nested>UINT32_MAX-overhead||capacity<overhead||
       nested>capacity-overhead||
       !bb_wire_separated(output,overhead+nested,e,sizeof(bb_note_expression)*BB_SYNTHS*BB_STEPS))return 0;
    uint8_t *p=output;unsigned any=0;
    if(!known_scene(p+overhead,nested))return 0;
    for(unsigned s=0;s<BB_SYNTHS;s++)for(unsigned n=0;n<BB_STEPS;n++){
        if(!playable_word(e[s][n]))return 0;
        any|=e[s][n];
    }
    if(!any){
        /* Destination precedes source, so a forward copy is overlap-safe.
         * Volatile byte access also avoids synthesizing a memmove call: the
         * freestanding FM1 runtime deliberately supplies no such helper. */
        volatile uint8_t *dst=p;const volatile uint8_t *src=p+overhead;
        for(size_t i=0;i<nested;i++)dst[i]=src[i];
        return nested;
    }
    memset(p,0,16);memcpy(p,"BBEX",4);p[4]=1;p[5]=BB_SYNTHS;p[6]=BB_STEPS;
    bb_wire_put32(p+8,(uint32_t)(overhead+nested));
    for(unsigned s=0;s<BB_SYNTHS;s++)for(unsigned n=0;n<BB_STEPS;n++){
        size_t at=16+2*(s*BB_STEPS+n);p[at]=(uint8_t)e[s][n];p[at+1]=(uint8_t)(e[s][n]>>8);
    }
    bb_wire_put32(p+12,checksum(p,overhead+nested));return overhead+nested;
}
int bb_expression_scene_open(const void *input,size_t bytes,
                             bb_note_expression e[BB_SYNTHS][BB_STEPS],
                             const void **nested,size_t *length){
    const size_t ebytes=sizeof(bb_note_expression)*BB_SYNTHS*BB_STEPS;
    if(!input||!e||!nested||!length||bytes<4||
       !bb_wire_separated(input,bytes,e,ebytes)||
       !bb_wire_separated(input,bytes,nested,sizeof *nested)||
       !bb_wire_separated(input,bytes,length,sizeof *length)||
       !bb_wire_separated(e,ebytes,nested,sizeof *nested)||
       !bb_wire_separated(e,ebytes,length,sizeof *length)||
       !bb_wire_separated(nested,sizeof *nested,length,sizeof *length))return 0;
    const uint8_t *p=input;size_t offset=0;
    if(!memcmp(p,"BBEX",4)){
        offset=BB_EXPRESSION_SCENE_OVERHEAD;
        if(bytes<offset+4||bytes>UINT32_MAX||p[4]!=1||p[5]!=BB_SYNTHS||p[6]!=BB_STEPS||p[7]||
           bb_wire_get32(p+8)!=bytes||bb_wire_get32(p+12)!=checksum(p,bytes)||
           !known_scene(p+offset,bytes-offset))return 0;
        unsigned any=0;
        for(unsigned s=0;s<BB_SYNTHS;s++)for(unsigned n=0;n<BB_STEPS;n++){
            size_t at=16+2*(s*BB_STEPS+n);
            bb_note_expression word=(bb_note_expression)(p[at]|((unsigned)p[at+1]<<8));
            if(!playable_word(word))return 0;
            any|=word;
        }
        if(!any)return 0; /* canonical: legacy bytes when no expression */
    }else if(!known_scene(p,bytes))return 0;
    /* Commit outputs only after all framing/metadata checks pass. */
    for(unsigned s=0;s<BB_SYNTHS;s++)for(unsigned n=0;n<BB_STEPS;n++){
        size_t at=16+2*(s*BB_STEPS+n);
        e[s][n]=offset?(bb_note_expression)(p[at]|((unsigned)p[at+1]<<8)):0;
    }
    *nested=p+offset;*length=bytes-offset;return 1;
}
