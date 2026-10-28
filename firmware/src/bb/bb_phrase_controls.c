/* SPDX-License-Identifier: GPL-3.0-only */
#include "bb_phrase_controls.h"
#include <stddef.h>
static int separate(const void *a,size_t n,const void *b,size_t m) {
    uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;
    return a && b && x<=UINTPTR_MAX-n && y<=UINTPTR_MAX-m && (x+n<=y || y+m<=x);
}
static int control(unsigned v){return v==BB_FDV_ACCENT || v==BB_FDV_ACC;}
int bb_phrase_controls_prepare(const bb_phrase *p,bb_phrase_controls *out) {
    if(!separate(p,sizeof *p,out,sizeof *out) || !p->length || p->length>BB_STEPS ||
       p->count>BB_FACTORY_DRUM_EVENTS || p->lattice>BB_FDL_TRIPLETS || p->bars>4)return 0;
    unsigned per=p->lattice==BB_FDL_SIXTEENTHS?16:p->lattice==BB_FDL_EIGHTHS?8:24;
    if(p->bars && p->length!=p->bars*per)return 0;
    uint64_t mask=0;
    for(unsigned i=0;i<p->count;i++) {
        bb_phrase_event e;if(!bb_phrase_read(p,i,&e))return 0;
        if(control(e.voice)) {if(e.modifiers)return 0;mask|=UINT64_C(1)<<(e.step-1);}
    }
    out->accents=mask;return 1;
}
int bb_phrase_resolve(const bb_phrase *p,const bb_phrase_controls *controls,unsigned i,bb_phrase_event *out) {
    if(!separate(p,sizeof *p,out,sizeof *out) || !separate(controls,sizeof *controls,out,sizeof *out) ||
       !separate(p,sizeof *p,controls,sizeof *controls))return 0;
    bb_phrase_event e;if(!bb_phrase_read(p,i,&e))return 0;
    int kind=control(e.voice)?2:1;
    if(kind==1 && (controls->accents&(UINT64_C(1)<<(e.step-1))))e.intensity=BB_FDI_ACCENTED;
    *out=e;return kind;
}
