/* SPDX-License-Identifier: GPL-3.0-only */
#include "bb_delayed.h"
#include <stddef.h>
#include <string.h>
static int separate(const void *a,size_t n,const void *b,size_t m) {
    uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;
    return a && b && x<=UINTPTR_MAX-n && y<=UINTPTR_MAX-m && (x+n<=y || y+m<=x);
}
static int valid(const bb_delayed *q) {
    return q && q->count<=BB_DELAYED_CAPACITY && q->active<=1;
}
int bb_delayed_begin(bb_delayed *q,uint32_t generation,uint64_t frame) {
    if(!valid(q))return BB_DELAYED_INVALID;
    if(!generation || generation<=q->generation)return BB_DELAYED_STALE;
    memset(q->items,0,sizeof q->items);q->generation=generation;q->floor=frame;
    q->count=0;q->active=1;return BB_DELAYED_OK;
}
void bb_delayed_stop(bb_delayed *q) {
    if(!q)return;
    memset(q->items,0,sizeof q->items);q->count=0;q->active=0;
}
int bb_delayed_offer(bb_delayed *q,uint64_t now,const bb_delayed_event *events,unsigned n) {
    if(!valid(q) || !n || n>BB_DELAYED_CAPACITY ||
       !separate(q,sizeof *q,events,n*sizeof *events))return BB_DELAYED_INVALID;
    if(!q->active)return BB_DELAYED_STALE;
    if(now<q->floor || (q->count && q->items[0].frame<now))return BB_DELAYED_LATE;
    if(n>BB_DELAYED_CAPACITY-q->count)return BB_DELAYED_FULL;
    for(unsigned i=0;i<n;i++) {
        if(events[i].generation!=q->generation)return BB_DELAYED_STALE;
        if(events[i].frame<now)return BB_DELAYED_LATE;
    }
    for(unsigned i=0;i<n;i++) {
        unsigned at=q->count;
        while(at && q->items[at-1].frame>events[i].frame) {
            q->items[at]=q->items[at-1];at--;
        }
        q->items[at]=events[i];q->count++;
    }
    q->floor=now;return BB_DELAYED_OK;
}
int bb_delayed_peek(const bb_delayed *q,bb_delayed_event *out) {
    if(!valid(q) || !separate(q,sizeof *q,out,sizeof *out))return BB_DELAYED_INVALID;
    if(!q->active || !q->count)return BB_DELAYED_WAIT;
    *out=q->items[0];return BB_DELAYED_OK;
}
int bb_delayed_pop(bb_delayed *q,uint64_t frame,bb_delayed_event *out) {
    if(!valid(q) || !separate(q,sizeof *q,out,sizeof *out))return BB_DELAYED_INVALID;
    if(!q->active || !q->count)return BB_DELAYED_WAIT;
    if(frame>q->items[0].frame || frame<q->floor)return BB_DELAYED_LATE;
    if(frame<q->items[0].frame)return BB_DELAYED_WAIT;
    *out=q->items[0];q->count--;
    /* Source is one event above destination, so forward assignment preserves
     * overlapping queue order without a runtime memmove dependency. At most
     * BB_DELAYED_CAPACITY-1 events move; no allocation or unbounded loop. */
    for(unsigned i=0;i<q->count;i++)q->items[i]=q->items[i+1];
    memset(&q->items[q->count],0,sizeof q->items[0]);q->floor=frame;
    return BB_DELAYED_OK;
}
