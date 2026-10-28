/* SPDX-License-Identifier: GPL-3.0-only */
#include "aux_navigation.h"

_Static_assert(sizeof(an_return)==8,"Return chain must stay within eight bytes");
_Static_assert(sizeof(an_fx_gesture)==8,"FX gesture must stay within eight bytes");
unsigned an_kind(uint16_t value){return value>>AN_KIND_SHIFT;}
void an_reset(an_return *s){if(s)*s=(an_return){0};}
int an_toggle(an_return *s,uint16_t current,uint16_t requested,
              uint16_t fallback,uint16_t *out){
    if(!s||!out||s->depth>AN_DEPTH||!an_kind(requested)||an_kind(fallback))return 0;
    /* The result cannot alias the return chain: publication is all-or-nothing. */
    uintptr_t a=(uintptr_t)s,b=(uintptr_t)out;
    if((a<=b&&b-a<sizeof *s)||(b<a&&a-b<sizeof *out))return 0;
    unsigned seen=1u<<an_kind(current);
    if(s->depth&&!an_kind(current))return 0;
    for(unsigned i=0;i<s->depth;i++){
        unsigned kind=an_kind(s->frames[i]);
        if((!kind&&i)||(seen&(1u<<kind)))return 0;
        seen|=1u<<kind;
    }
    an_return next=*s;uint16_t page=requested;
    unsigned wanted=an_kind(requested),have=an_kind(current);
    if(wanted==have){
        page=next.depth?next.frames[--next.depth]:fallback;
    }else{
        unsigned at=next.depth;
        for(unsigned i=0;i<next.depth;i++)if(an_kind(next.frames[i])==wanted){at=i;break;}
        if(at<next.depth){page=next.frames[at];next.depth=(uint8_t)at;}
        else{
            if(next.depth==AN_DEPTH)return 0;
            next.frames[next.depth++]=current;
        }
    }
    for(unsigned i=next.depth;i<AN_DEPTH;i++)next.frames[i]=0;
    *s=next;*out=page;return 1;
}

unsigned an_fx_step(an_fx_gesture *s,uint32_t now,int held,int pressed,int rec,int cancel){
    if(!s)return AN_FX_NONE;
    if(held<0||held>1||pressed<0||pressed>1||rec<0||rec>1||cancel<0||cancel>1||s->state>3){
        s->state=held?3:0;return AN_FX_NONE;
    }
    if(cancel||rec){s->state=held?3:0;return AN_FX_NONE;}
    if(s->state==3){if(!held)s->state=0;return AN_FX_NONE;}
    if(s->state==2){if(!held)s->state=0;return AN_FX_NONE;}
    if(s->state==0){
        if(!pressed){if(held)s->state=3;return AN_FX_NONE;}
        if(!held)return AN_FX_TAP; /* Debounced tap already released. */
        s->since=now;s->state=1;return AN_FX_NONE;
    }
    if((uint32_t)(now-s->since)>=AN_FX_HOLD_MS){
        s->state=held?2:0;return AN_FX_HOLD;
    }
    if(!held){s->state=0;return AN_FX_TAP;}
    return AN_FX_NONE;
}
