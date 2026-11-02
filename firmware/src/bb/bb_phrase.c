/* SPDX-License-Identifier: GPL-3.0-only */
#include "bb_phrase.h"
#include <string.h>
_Static_assert(BB_STEPS<=64,"Step packing needs revision");
_Static_assert(BB_PHRASE_VOICES<=32,"Voice packing needs revision");
_Static_assert(sizeof(bb_phrase)==292,"Packed ratchet metadata memory cost");
static int disjoint(const void *a,size_t n,const void *b,size_t m) {
    uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;
    return a && b && x<=UINTPTR_MAX-n && y<=UINTPTR_MAX-m &&
        (x+n<=y || y+m<=x);
}
static int header(unsigned length,unsigned count,unsigned lattice,unsigned bars) {
    if(!length || length>BB_STEPS || count>BB_FACTORY_DRUM_EVENTS ||
       lattice>BB_FDL_TRIPLETS || bars>4)return 0;
    unsigned per=lattice==BB_FDL_SIXTEENTHS?16:lattice==BB_FDL_EIGHTHS?8:24;
    return !bars || length==bars*per;
}
int bb_phrase_pack(const bb_factory_drum_data *data,bb_phrase *out) {
    if(!disjoint(data,sizeof *data,out,sizeof *out) ||
       !header(data->length,data->count,data->lattice,data->bars))return 0;
    /* Validate before any destination write; no large stack staging copy. */
    for(unsigned i=0;i<data->count;i++) {
        const bb_factory_drum_event *e=&data->events[i];
        if(e->voice>=BB_PHRASE_VOICES || !e->step || e->step>data->length ||
           e->intensity>BB_FDI_UNACCENTED || e->modifiers>7)return 0;
    }
    memset(out,0,sizeof *out);
    out->length=data->length;out->count=data->count;
    out->lattice=data->lattice;out->bars=data->bars;
    for(unsigned i=0;i<data->count;i++) {
        const bb_factory_drum_event *e=&data->events[i];
        out->events[i]=(uint16_t)(e->voice|((e->step-1u)<<5)|
            (e->intensity<<11)|(e->modifiers<<13));
    }
    return 1;
}
int bb_phrase_read(const bb_phrase *p,unsigned i,bb_phrase_event *out) {
    if(!disjoint(p,sizeof *p,out,sizeof *out) ||
       !header(p->length,p->count,p->lattice,p->bars) || i>=p->count)return 0;
    unsigned word=p->events[i];
    bb_phrase_event e={(uint8_t)(word&31),(uint8_t)(((word>>5)&63)+1),
        (uint8_t)((word>>11)&3),(uint8_t)(word>>13)};
    if(e.voice>=BB_PHRASE_VOICES || e.step>p->length || e.intensity>BB_FDI_UNACCENTED)return 0;
    *out=e;return 1;
}

unsigned bb_phrase_ratchet_count(const bb_phrase *p,unsigned ordinal){
    return p&&ordinal<p->count&&ordinal<BB_FACTORY_DRUM_EVENTS?
        1u+((p->ratchets[ordinal>>2]>>(2u*(ordinal&3u)))&3u):0u;
}
int bb_phrase_ratchets_valid(const bb_phrase *p){
    if(!p||!header(p->length,p->count,p->lattice,p->bars))return 0;
    for(unsigned i=0;i<BB_FACTORY_DRUM_EVENTS;i++){
        unsigned packed=(p->ratchets[i>>2]>>(2u*(i&3u)))&3u;
        if(i>=p->count){if(packed)return 0;continue;}
        unsigned voice=p->events[i]&31u;
        if((voice==BB_FDV_ACCENT||voice==BB_FDV_ACC)&&packed)return 0;
    }
    return 1;
}
int bb_phrase_ratchet_set(bb_phrase *p,unsigned voice,unsigned step,unsigned count){
    if(!bb_phrase_ratchets_valid(p)||voice>=BB_PHRASE_VOICES||
       voice==BB_FDV_ACCENT||voice==BB_FDV_ACC||step>=p->length||count<1u||count>4u)return 0;
    unsigned matches=0;
    for(unsigned i=0;i<p->count;i++){
        bb_phrase_event e;if(!bb_phrase_read(p,i,&e))return 0;
        matches+=e.voice==voice&&e.step==step+1u;
    }
    if(!matches)return 0;
    for(unsigned i=0;i<p->count;i++){
        unsigned word=p->events[i];
        if((word&31u)==voice&&((word>>5)&63u)==step){
            unsigned shift=2u*(i&3u);uint8_t mask=(uint8_t)(3u<<shift);
            p->ratchets[i>>2]=(uint8_t)((p->ratchets[i>>2]&~mask)|((count-1u)<<shift));
        }
    }
    return 1;
}
