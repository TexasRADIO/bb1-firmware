/* SPDX-License-Identifier: GPL-3.0-only */
/* CPU0 ordered scene bridge. transact must complete/accept or fail; never
 * retry after failure. Hardware transaction bounds remain an integration gate.
 * Caller owns portable core and fixed routing; no DSP pointers are exposed. */
#ifndef BB_PA_STREAM_H
#define BB_PA_STREAM_H
#include "producer.h"
typedef int (*pa_transact_fn)(void *,const pa_request *,uint32_t *);
typedef struct {
    pa_builder builder;const bb_core *core;pa_transact_fn transact;void *context;
    uint32_t ticket,state;unsigned failed,transactions;
} pa_stream;
static int pa_stream_fail(pa_stream *s) {
    s->failed=1;pa_builder_fail(&s->builder);return 0;
}
static int pa_stream_begin(pa_stream *s,pa_request *wire,const po_routing *route,
                           const bb_core *core,uint32_t ticket,uint32_t state,
                           pa_transact_fn transact,void *context) {
    if(!s||!transact||!wire||!bb_rt_separate(s,sizeof *s,wire,sizeof *wire)||
       !core||!bb_rt_separate(s,sizeof *s,core,sizeof *core))return 0;
    *s=(pa_stream){0};s->core=core;s->transact=transact;s->context=context;s->ticket=ticket;s->state=state;
    if(!pa_builder_init(&s->builder,wire,route,core,ticket,state,1))return pa_stream_fail(s);
    return 1;
}
static int pa_stream_send(pa_stream *s,unsigned frames) {
    if(!s||s->failed||frames>128||s->builder.failed||s->builder.closed)return 0;
    pa_request *r=s->builder.wire;
    if(!pa_builder_seal(&s->builder))return pa_stream_fail(s);
    r->frames=frames;r->guard=frames?PA_GUARD:PA_CONTROL_GUARD;
    r->check=frames?pa_hash(r):pa_control_hash(r);
    uint32_t next_state=s->state;
    if(!s->transact(s->context,r,&next_state))return pa_stream_fail(s);
    s->state=next_state;s->transactions++;
    if(s->ticket==UINT32_MAX)return pa_stream_fail(s);
    s->ticket++;
    return pa_builder_init(&s->builder,r,s->builder.route,s->core,s->ticket,s->state,1)?1:pa_stream_fail(s);
}
/* Call AFTER original core scene commit,at its original all-drum sync hook.
 * The old wire still contains the pre-scene portable baseline and ordered
 * events. Flush it without implicit sync,then capture the new core sounds.
 * Empty pending batches need no physical transaction. */
static int pa_stream_scene(pa_stream *s) {
    if(!s||s->failed||s->builder.failed||s->builder.closed)return 0;
    if(s->builder.wire->count) {
        if(!pa_stream_send(s,0))return 0;
    } else if(!pa_builder_init(&s->builder,s->builder.wire,s->builder.route,s->core,s->ticket,s->state,1))return pa_stream_fail(s);
    for(unsigned m=0;m<BB_MACHINES;m++) {
        int status=pa_builder_sync(&s->builder,m);
        if(status<0)return pa_stream_fail(s);
    }
    return 1;
}
#endif
