#include "bb_wire.h"
/* SPDX-License-Identifier: GPL-3.0-only */
#include "bb909_sequence.h"
#include <string.h>
#define overlap bb_wire_overlap
const char *bb909_sequence_voice_name(unsigned v) {
    static const char *const names[]={"BASS DRUM","SNARE","LOW TOM","MID TOM","HIGH TOM",
        "RIMSHOT","HAND CLAP","CLOSED HAT","OPEN HAT","CRASH","RIDE"};
    return v<BB909_SEQ_VOICES?names[v]:"--";
}
unsigned bb909_sequence_project(const bb_factory_drum_data *d,bb_factory_drum_grid *out) {
    if(!d||!out||overlap(d,sizeof *d,out,sizeof *out)||!d->length||d->length>BB_STEPS
       ||d->count>BB_FACTORY_DRUM_EVENTS||d->lattice>BB_FDL_TRIPLETS||d->bars>4)return BB_FDRUM_ISSUE_INVALID;
    /* Source CY is the general cymbal role; the 909 supplies its crash.
     * RD has its own lane. Cowbell, congas, claves, maracas, synthetic
     * cymbal and zap have no 909 substitute and reject the entire pattern. */
    static const int8_t lane[]={0,1,2,3,4,-1,-1,-1,5,-1,6,-1,-1,9,8,7,10,-1,-1,-1,-1};
    _Static_assert(sizeof lane/sizeof lane[0]==BB_FDV_COUNT,"source roles complete");
    unsigned scale=d->lattice==BB_FDL_EIGHTHS?2u:1u,issues=0;
    if(d->lattice==BB_FDL_TRIPLETS||d->length*scale>BB_STEPS)issues|=BB_FDRUM_ISSUE_TIMING;
    uint32_t seen[BB_STEPS]={0};uint8_t accents[BB_STEPS]={0};
    bb_factory_drum_grid grid;memset(&grid,0,sizeof grid);grid.length=(uint8_t)(d->length*scale);grid.family=1;
    for(unsigned i=0;i<d->count;i++) {
        const bb_factory_drum_event *e=&d->events[i];
        if(e->voice>=BB_FDV_COUNT||!e->step||e->step>d->length
           ||e->intensity>BB_FDI_UNACCENTED||(e->modifiers&~7u))return BB_FDRUM_ISSUE_INVALID;
        unsigned raw=e->step-1u,at=raw*scale;uint32_t bit=UINT32_C(1)<<e->voice;
        if(seen[raw]&bit)issues|=BB_FDRUM_ISSUE_DUPLICATE;seen[raw]|=bit;
        if(e->modifiers)issues|=BB_FDRUM_ISSUE_MODIFIER;
        if(e->voice>=BB_FDV_ACCENT) {if(at<BB_STEPS)accents[at]=1;continue;}
        int v=lane[e->voice];if(v<0){issues|=BB_FDRUM_ISSUE_VOICE;continue;}
        if(at<BB_STEPS) {
            uint16_t hit=(uint16_t)(1u<<(unsigned)v);grid.steps[at].hits|=hit;
            if(e->intensity==BB_FDI_ACCENTED)grid.steps[at].accents|=hit;
        }
    }
    for(unsigned i=0;i<BB_STEPS;i++)if(accents[i])grid.steps[i].accents|=grid.steps[i].hits;
    if(issues)return issues;*out=grid;return 0;
}
int bb909_sequence_begin(bb909_request *r,uint32_t ticket,unsigned frames) {
    if(!r||(uintptr_t)r%_Alignof(bb909_request)||!ticket||!frames||frames>BB909_OWNER_FRAMES)return 0;
    memset(r,0,sizeof *r);r->ticket=ticket;r->frames=frames;r->check=bb909_request_check(r);return 1;
}
int bb909_sequence_hit(bb909_request *r,unsigned layer,uint64_t start,const bb_hit *e) {
    if(!r||(uintptr_t)r%_Alignof(bb909_request)||!e||layer>=BB_MACHINES||e->machine>=BB_MACHINES
       ||overlap(r,sizeof *r,e,sizeof *e)||!r->ticket||!r->frames||r->frames>128
       ||r->count>32||r->check!=bb909_request_check(r))return BB909_SEQ_REJECTED;
    if(e->machine!=layer)return BB909_SEQ_OTHER_LAYER;
    if(e->voice>=BB909_SEQ_VOICES||e->velocity>127||e->frame<start
       ||e->frame-start>=r->frames||r->count==32)return BB909_SEQ_REJECTED;
    unsigned offset=(unsigned)(e->frame-start);
    if(r->count&&offset<r->events[r->count-1u].offset)return BB909_SEQ_REJECTED;
    r->events[r->count++]=(bb909_event){(uint16_t)offset,BB909_EVENT_TRIGGER,e->voice,0,e->velocity};
    r->check=bb909_request_check(r);return BB909_SEQ_APPENDED;
}
