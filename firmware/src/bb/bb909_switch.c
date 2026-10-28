#include "bb_wire.h"
/* SPDX-License-Identifier: GPL-3.0-only */
#include "bb909_switch.h"
#include <string.h>
#define overlap bb_wire_overlap
int bb909_switch_project(const bb_machine *src,unsigned from,unsigned to,const bb_drum_sound *recipe,
                         bb_machine *out,uint32_t *missing) {
    if(!src||!out||!missing||from>1||to>1||!src->length||src->length>64||src->next>=src->length
       ||overlap(src,sizeof *src,out,sizeof *out)||overlap(src,sizeof *src,missing,sizeof *missing)
       ||overlap(out,sizeof *out,missing,sizeof *missing)||(to==BB_SWITCH_808&&!recipe))return BB_SWITCH_INVALID;
    static const int8_t to9[]={0,1,2,3,4,5,6,-1,9,8,7};
    static const int8_t to8[]={0,1,2,3,4,5,6,10,9,8,-1};
    bb_machine next=*src;memset(next.steps,0,sizeof next.steps);
    uint32_t absent=0;int mode_bad=0;
    for(unsigned s=0;s<64;s++) {
        uint16_t hits=src->steps[s].hits,acc=src->steps[s].accents;
        if((hits&~2047u)||(acc&~hits))return BB_SWITCH_INVALID;
        for(unsigned v=0;v<11;v++)if(hits&(1u<<v)) {
            int lane=from==to?(int)v:(to==BB_SWITCH_909?to9[v]:to8[v]);
            if(lane<0){absent|=1u<<v;continue;}
            if(from!=to) {
                if(from==BB_SWITCH_808&&v>=2&&v<=6&&src->sound.mode[v]) {absent|=1u<<v;mode_bad=1;continue;}
                if(to==BB_SWITCH_808&&lane>=2&&lane<=6&&recipe->mode[lane]) {absent|=1u<<v;mode_bad=1;continue;}
            }
            next.steps[s].hits|=(uint16_t)(1u<<lane);
            if(acc&(1u<<v))next.steps[s].accents|=(uint16_t)(1u<<lane);
        }
    }
    *missing=absent;
    if(absent)return mode_bad?BB_SWITCH_MODE:BB_SWITCH_MISSING_VOICE;
    if(to==BB_SWITCH_808)next.sound=*recipe;
    *out=next;return BB_SWITCH_OK;
}
