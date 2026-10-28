#include "bb_wire.h"
/* SPDX-License-Identifier: GPL-3.0-only */
#include "bb_family_scene.h"
#include "bb_crc32.h"
#include <string.h>
#define overlap bb_wire_overlap
static uint32_t crc(const unsigned char *p,size_t n){
    return ~bb_crc32_add(bb_crc32_add(UINT32_MAX,p,12),p+16,n-16);
}
#define get32 bb_wire_get32
#define put32 bb_wire_put32
static int zero(const void *p,size_t n){const unsigned char *b=p;while(n--)if(*b++)return 0;return 1;}
int bb_family_scene_valid(const bb_family_scene *s){
    if(!s||!bb_session_state_valid(&s->core))return 0;
    for(unsigned k=0;k<4;k++) {
        if(s->family[k]>1)return 0;
        if(!s->family[k]) {
            if(!zero(s->patch[k].value,sizeof s->patch[k].value)||!zero(s->patch[k].touched,sizeof s->patch[k].touched))return 0;
        } else {
            if(!bb909_patch_valid(&s->patch[k]))return 0;
            for(unsigned at=0;at<64;at++) {
                bb_drum_step e=s->core.drums[k].steps[at];
                if((e.hits&~2047u)||(e.accents&~e.hits))return 0;
            }
        }
    }
    return 1;
}
int bb_family_scene_admit(const bb_family_scene *s,unsigned owners){
    if(owners>4u||!bb_family_scene_valid(s))return BB_FAMILY_ADMISSION_INVALID;
    unsigned needed=0;for(unsigned k=0;k<4;k++)needed+=s->family[k]==1;
    /* Disabled families still need truthful audition/tail handling. */
    return needed<=owners?BB_FAMILY_ADMISSION_OK:BB_FAMILY_ADMISSION_CAPACITY;
}
size_t bb_family_scene_encode(const bb_family_scene *s,void *out,size_t capacity){
    if(!s||!out||!bb_family_scene_valid(s))return 0;
    int extended=0;for(unsigned k=0;k<4;k++)extended|=s->family[k];
    size_t bytes=extended?BB_FAMILY_SCENE_BYTES:BB_SESSION_BYTES;
    if(capacity<bytes||overlap(s,sizeof *s,out,bytes))return 0;
    unsigned char *wire=out;
    if(!extended)return bb_session_encode(&s->core,wire,capacity)?bytes:0;
    /* All state and capacity validated before first write; no fallible IO.
     * Stable caller ownership is required throughout these nested encoders. */
    if(!bb_session_encode(&s->core,wire+16,BB_SESSION_BYTES))return 0;
    memcpy(wire+16+BB_SESSION_BYTES,s->family,4);
    for(unsigned k=0;k<4;k++) {
        unsigned char *p=wire+20+BB_SESSION_BYTES+k*BB909_PATCH_BYTES;
        if(s->family[k]){if(!bb909_patch_encode(&s->patch[k],p,BB909_PATCH_BYTES))return 0;}
        else memset(p,0,BB909_PATCH_BYTES);
    }
    memcpy(wire,"BBFS",4);wire[4]=1;wire[5]=0;wire[6]=16;wire[7]=0;
    put32(wire+8,(uint32_t)bytes);put32(wire+12,crc(wire,bytes));return bytes;
}
int bb_family_scene_decode_staged(const void *input,size_t bytes,uint32_t rate,bb_family_scene_workspace *w){
    if(!input||!w||overlap(input,bytes,w,sizeof *w))return 0;
    const unsigned char *wire=input;memset(&w->decoded,0,sizeof w->decoded);
    if(bytes==BB_SESSION_BYTES) {
        if(!bb_session_decode(wire,bytes,rate,&w->decoded.core,&w->core_scratch))return 0;
    } else {
        if(bytes!=BB_FAMILY_SCENE_BYTES||memcmp(wire,"BBFS",4)||wire[4]!=1||wire[5]||wire[6]!=16||wire[7]
           ||get32(wire+8)!=bytes||get32(wire+12)!=crc(wire,bytes))return 0;
        if(!bb_session_decode(wire+16,BB_SESSION_BYTES,rate,&w->decoded.core,&w->core_scratch))return 0;
        memcpy(w->decoded.family,wire+16+BB_SESSION_BYTES,4);
        unsigned active=0;
        for(unsigned k=0;k<4;k++) {
            const unsigned char *p=wire+20+BB_SESSION_BYTES+k*BB909_PATCH_BYTES;
            if(w->decoded.family[k]==1){active++;if(!bb909_patch_decode(p,BB909_PATCH_BYTES,&w->decoded.patch[k]))return 0;}
            else if(w->decoded.family[k]||!zero(p,BB909_PATCH_BYTES))return 0;
        }
        if(!active)return 0; /* canonical all-808 form must remain BBSC */
    }
    return bb_family_scene_valid(&w->decoded);
}
int bb_family_scene_decode(const void *input,size_t bytes,uint32_t rate,bb_family_scene *out,bb_family_scene_workspace *w){
    if(!input||!out||!w||overlap(input,bytes,out,sizeof *out)||overlap(input,bytes,w,sizeof *w)
       ||overlap(out,sizeof *out,w,sizeof *w))return 0;
    if(!bb_family_scene_decode_staged(input,bytes,rate,w))return 0;
    *out=w->decoded;return 1;
}
