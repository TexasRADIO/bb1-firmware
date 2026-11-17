#include "bb_wire.h"
/* SPDX-License-Identifier: GPL-3.0-only */
#include "bb909_patch.h"
#include "bb_crc32.h"
#include "drum909.h"
#include <string.h>
#define overlap bb_wire_overlap
static uint32_t crc(const unsigned char *b,size_t n){
    return ~bb_crc32_add(UINT32_MAX,b,n);
}
#define put32 bb_wire_put32
#define get32 bb_wire_get32
int bb909_patch_valid(const bb909_controls *s){
    if(!s)return 0;
    for(unsigned v=0;v<12;v++) {
        unsigned count=bb909_control_count(v);if(!count||count>11||(s->touched[v]&~((1u<<count)-1u)))return 0;
        for(unsigned p=0;p<11;p++) {
            if(p>=count){if(s->value[v][p])return 0;continue;}
            if(!bb909_control_valid(v,p,s->value[v][p]))return 0;
            if(!(s->touched[v]&(1u<<p))&&s->value[v][p]!=drum909_param((int)v,(int)p)->def)return 0;
        }
    }
    return 1;
}
int bb909_patch_encode(const bb909_controls *s,void *out,size_t capacity){
    if(!s||!out||capacity<168||overlap(s,sizeof *s,out,168)||!bb909_patch_valid(s))return 0;
    unsigned char wire[168]={0};memcpy(wire,"B9P1",4);wire[4]=1;wire[5]=12;wire[6]=11;
    memcpy(wire+8,s->value,132);
    for(unsigned v=0;v<12;v++){wire[140+2*v]=(unsigned char)s->touched[v];wire[141+2*v]=(unsigned char)(s->touched[v]>>8);}
    put32(wire+164,crc(wire,164));memcpy(out,wire,168);return 1;
}
int bb909_patch_decode(const void *input,size_t bytes,bb909_controls *out){
    if(!input||!out||bytes!=168||overlap(input,bytes,out,sizeof *out))return 0;
    const unsigned char *wire=input;
    if(memcmp(wire,"B9P1",4)||wire[4]!=1||wire[5]!=12||wire[6]!=11||wire[7]||get32(wire+164)!=crc(wire,164))return 0;
    bb909_controls patch;memset(&patch,0,sizeof patch);memcpy(patch.value,wire+8,132);
    for(unsigned v=0;v<12;v++)patch.touched[v]=(uint16_t)(wire[140+2*v]|((unsigned)wire[141+2*v]<<8));
    if(!bb909_patch_valid(&patch))return 0;*out=patch;return 1;
}
int bb909_patch_request(const bb909_controls *s,unsigned cursor,uint32_t ticket,bb909_request *out,unsigned *next){
    if(!s||!out||!next||cursor>132||!ticket||overlap(s,sizeof *s,out,sizeof *out)
       ||overlap(s,sizeof *s,next,sizeof *next)||overlap(out,sizeof *out,next,sizeof *next)||!bb909_patch_valid(s))return BB909_RESTORE_INVALID;
    bb909_request request;memset(&request,0,sizeof request);request.ticket=ticket;request.frames=1;
    unsigned at=cursor;
    while(at<132&&request.count<32) {
        unsigned v=at/11,p=at%11;at++;
        if(s->touched[v]&(1u<<p))request.events[request.count++]=(bb909_event){0,BB909_EVENT_SET,v,p,s->value[v][p]};
    }
    *next=at;if(!request.count)return BB909_RESTORE_DONE;
    request.check=bb909_request_check(&request);*out=request;return BB909_RESTORE_READY;
}
