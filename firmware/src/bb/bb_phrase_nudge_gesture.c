/* SPDX-License-Identifier: GPL-3.0-only */
#include "bb_phrase_nudge_gesture.h"
#ifndef BB_PHRASE_NUDGE_GESTURE_LIBC_PROVIDED
#include <string.h>
#endif

static int disjoint(const void *a,size_t n,const void *b,size_t m)
{
    uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;
    return a&&b&&x<=UINTPTR_MAX-n&&y<=UINTPTR_MAX-m&&(x+n<=y||y+m<=x);
}

int bb_phrase_nudge_gesture_begin(bb_phrase_nudge_gesture *g,const bb_phrase *p,
                                  const bb_phrase_nudges *n,uint32_t ticket,
                                  uint32_t epoch,unsigned machine,
                                  unsigned voice,unsigned step)
{
    if(!g||!p||!n||machine>1u||voice>=BB_PHRASE_VOICES||step>=BB_STEPS||
       !ticket||!bb_phrase_nudges_valid(p,n))return 0;
    int value;
    if(!bb_phrase_nudge_get(p,n,voice,step,&value))return 0;
    bb_phrase_nudge_gesture next;
    memset(&next,0,sizeof next);
    next.transaction.before=next.transaction.after=*n;
    next.transaction.source_ticket=ticket;
    next.transaction.source_epoch=epoch;
    next.transaction.machine=(uint8_t)machine;
    next.transaction.voice=(uint8_t)voice;
    next.transaction.step=(uint8_t)step;
    next.value=(int8_t)value;next.active=1;
    *g=next;return 1;
}

int bb_phrase_nudge_gesture_turn(bb_phrase_nudge_gesture *g,const bb_phrase *p,int delta)
{
    if(!g||!p||!g->active||!delta||
       !bb_phrase_nudges_valid(p,&g->transaction.after))return 0;
    int next=g->value;
    if(delta>0){int room=7-next;next+=delta<room?delta:room;}
    else {int room=next+8;next-=delta> -room? -delta:room;}
    if(next==g->value)return 1;
    bb_phrase_nudges edited=g->transaction.after;
    if(!bb_phrase_nudge_set(p,&edited,g->transaction.voice,g->transaction.step,next))return 0;
    bb_phrase_nudge_queue_profile profile;
    if(!bb_phrase_nudge_queue_measure(p,&edited,&profile)||profile.combined>BB_DELAYED_CAPACITY)return 0;
    g->transaction.after=edited;g->value=(int8_t)next;
    g->changed=(uint8_t)!!memcmp(&g->transaction.before,&edited,sizeof edited);
    return 1;
}

int bb_phrase_nudge_gesture_finish(bb_phrase_nudge_gesture *g,uint32_t ticket,
                                   uint32_t epoch,bb_phrase_nudge_transaction *out)
{
    if(!g||!g->active)return BB_NUDGE_GESTURE_INVALID;
    if(ticket!=g->transaction.source_ticket||epoch!=g->transaction.source_epoch){
        memset(g,0,sizeof *g);return BB_NUDGE_GESTURE_STALE;
    }
    if(!g->changed){memset(g,0,sizeof *g);return BB_NUDGE_GESTURE_UNCHANGED;}
    if(!out||!disjoint(g,sizeof *g,out,sizeof *out))return BB_NUDGE_GESTURE_INVALID;
    *out=g->transaction;memset(g,0,sizeof *g);return BB_NUDGE_GESTURE_CHANGED;
}

void bb_phrase_nudge_gesture_cancel(bb_phrase_nudge_gesture *g)
{
    if(g)memset(g,0,sizeof *g);
}
