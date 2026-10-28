/* SPDX-License-Identifier: GPL-3.0-only */
#include "bb_phrase_strike.h"
#include <stddef.h>
static int separate(const void *a,size_t n,const void *b,size_t m) {
    uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;
    return a && b && x<=UINTPTR_MAX-n && y<=UINTPTR_MAX-m && (x+n<=y || y+m<=x);
}
int bb_phrase_hit_resolve(const bb_phrase *p,const bb_phrase_controls *c,uint32_t payload,bb_phrase_hit *out) {
    if(!separate(out,sizeof *out,p,sizeof *p) || !separate(out,sizeof *out,c,sizeof *c) || payload>>25)return 0;
    unsigned ordinal=(payload>>16)&127,kind=(payload>>23)&3;
    bb_phrase_event e;int type=bb_phrase_resolve(p,c,ordinal,&e);
    if(!type || (uint16_t)payload!=p->events[ordinal])return 0;
    if(type==2)return (!kind && !e.modifiers)?2:0;
    if((e.modifiers&BB_FDM_RETRIGGER_LENGTH) && !(e.modifiers&BB_FDM_RETRIGGER_RATE))return 0;
    if(kind==1 && !(e.modifiers&BB_FDM_FLAM))return 0;
    if(kind>=2 && !(e.modifiers&BB_FDM_RETRIGGER_RATE))return 0;
    float velocity=e.intensity==BB_FDI_ACCENTED?1.f:D8_VEL_NORMAL;
    if(e.intensity==BB_FDI_UNACCENTED)velocity*=.62f;
    if(kind)velocity*=kind==1?.72f:.7f;
    *out=(bb_phrase_hit){e.voice,velocity};return 1;
}
int bb_phrase_strike(drum808_t *d,const bb_phrase *p,const bb_phrase_controls *c,uint32_t payload) {
    if(!separate(d,sizeof *d,p,sizeof *p) || !separate(d,sizeof *d,c,sizeof *c))return 0;
    bb_phrase_hit hit;int result=bb_phrase_hit_resolve(p,c,payload,&hit);
    return result==1?bb_drum808_source_trigger(d,hit.voice,hit.velocity):result;
}
