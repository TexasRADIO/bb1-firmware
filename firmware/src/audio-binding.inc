/* SPDX-License-Identifier: GPL-3.0-only */
/* CPU0 backend. Requires lease-view, stream, bb_audio and mix-view definitions.
 * wait_complete must preserve the platform's original deadline and return
 * only after completion, or fail. Host fixture supplies synchronous worker;
 * this is not the Pi32 platform implementation. */
typedef int (*pab_wait_fn)(po_lease *,void *);
#ifndef PAB_ON_PUBLISH
#define PAB_ON_PUBLISH(l) ((void)(l))
#endif
#ifndef PAB_ON_RELEASE
#define PAB_ON_RELEASE(l) ((void)(l))
#endif
#ifndef PAB_ON_FAILURE
#define PAB_ON_FAILURE(l) ((void)(l))
#endif
typedef struct {
    bb_audio *audio;po_lease *lease;po_routing route;pa_request *wire;
    pa_stream stream;pab_wait_fn wait_complete;void *wait_context;
    uint32_t revision[BB_MACHINES];unsigned failed,block,mixed;
} pa_audio_binding;
static pa_audio_binding *pab_current;
static int pab_fail(pa_audio_binding *b) {
    if(b){b->failed=1;if(b->lease->phase==PL_INFLIGHT)pl_timeout(b->lease);
        PAB_ON_FAILURE(b->lease);}
    return 0;
}
static int pab_ready(bb_audio *a) {
    return pab_current&&pab_current->audio==a&&!pab_current->failed;
}
/* Builder owns exactly the lease wire while idle. Publication transfers that
 * same immutable buffer to the worker: no second wire or540-byte stack copy.
 * This is not the general external-request pl_publish API. */
static int pab_publish(pa_audio_binding *b,const pa_request *r) {
    po_lease *l=b->lease;
    if((const void *)r!=(const void *)&l->wire||l->phase!=PL_IDLE||
       l->ticket==UINT32_MAX||r->ticket!=l->ticket+1u||
       r->previous_state!=l->state||r->kits!=l->kits||
       !BB_PL_FRAMES_VALID(&l->wire)||!plc_valid(&l->wire))return 0;
    if(l->mail.job!=l->ticket||l->mail.done!=l->ticket||l->mail.bad||
       l->mail.alive!=BB_C1_ALIVE){l->phase=PL_NEEDS_HOLD;return 0;}
    l->frames=r->frames;l->phase=PL_INFLIGHT;
    PAB_ON_PUBLISH(l);
    if(!bb_c1_publish(&l->mail,r->guard,r->ticket)){l->phase=PL_NEEDS_HOLD;return 0;}
    l->ticket=r->ticket;return 1;
}
static int pab_control(void *ctx,const pa_request *r,uint32_t *state) {
    pa_audio_binding *b=ctx;
    if(r->frames||!pab_publish(b,r)||
       !b->wait_complete(b->lease,b->wait_context)||pl_collect(b->lease,0)!=1)return pab_fail(b);
    PAB_ON_RELEASE(b->lease);*state=b->lease->state;return 1;
}
/* Cold attachment only; caller proves initial worker state matches its source
 * and no audio callback/worker can run concurrently with attachment. */
static int pab_attach(pa_audio_binding *b,bb_audio *a,po_lease *l,
                      const po_routing *route,pab_wait_fn wait_complete,void *ctx) {
    if(!b||!a||!l||!wait_complete||!po_routing_valid(route)||
       l->phase!=PL_IDLE||route->kits!=l->kits||
       !bb_rt_separate(b,sizeof *b,a,sizeof *a)||
       !bb_rt_separate(b,sizeof *b,l,sizeof *l)||
       !bb_rt_separate(b,sizeof *b,route,sizeof *route))return 0;
    *b=(pa_audio_binding){0};b->audio=a;b->lease=l;b->route=*route;
    b->wire=(pa_request *)(void *)&l->wire;
    b->wait_complete=wait_complete;b->wait_context=ctx;
    for(unsigned k=0;k<BB_MACHINES;k++)b->revision[k]=a->drum_revision[k];
    pab_current=b;return 1;
}
int bb_owner_audio_block_begin(bb_audio *a) {
    if(!pab_ready(a))return 0;pa_audio_binding *b=pab_current;
    if(b->block||b->lease->phase!=PL_IDLE||b->lease->ticket==UINT32_MAX)return pab_fail(b);
    if(!pa_stream_begin(&b->stream,b->wire,&b->route,&a->core,b->lease->ticket+1,
                       b->lease->state,pab_control,b))return pab_fail(b);
    b->block=1;return 1;
}
int bb_owner_audio_hit(bb_audio *a,const bb_hit *hit) {
    if(!pab_ready(a))return -1;pa_audio_binding *b=pab_current;
    if(!b->block){pab_fail(b);return -1;}
    int status=pa_builder_hit(&b->stream.builder,hit);
    if(status<0)pab_fail(b);return status;
}
int bb_owner_audio_sync(bb_audio *a,unsigned k) {
    if(!pab_ready(a))return -1;pa_audio_binding *b=pab_current;unsigned slot;
    int status=po_route_slot(&b->route,k,&slot);
    if(status<=0)return status;
    if(!b->block){pab_fail(b);return -1;}
    if(b->revision[k]!=a->core.drums[k].sound_revision) {
        if(!pa_stream_scene(&b->stream)){pab_fail(b);return -1;}
        for(unsigned m=0;m<BB_MACHINES;m++)if(b->route.slot[m]!=PO_NO_SLOT)
            b->revision[m]=a->core.drums[m].sound_revision;
    }
    return 1;
}
int bb_owner_audio_group(bb_audio *a,const bb_command *c) {
    if(!pab_ready(a))return -1;pa_audio_binding *b=pab_current;
    if(!b->block){pab_fail(b);return -1;}
    int status=pa_builder_command(&b->stream.builder,c);
    if(status<0||status==PA_ROUTE_DEFERRED){pab_fail(b);return -1;}return status;
}
int bb_owner_audio_span_begin(bb_audio *a,unsigned frames) {
    if(!pab_ready(a))return 0;pa_audio_binding *b=pab_current;
    if(!b->block||!frames||frames>128||b->lease->phase!=PL_IDLE||
       !pa_builder_seal(&b->stream.builder))return pab_fail(b);
    b->wire->frames=frames;b->wire->check=pa_hash(b->wire);b->mixed=0;
    if(!pab_publish(b,b->wire))return pab_fail(b);
    return 1;
}
int bb_owner_audio_span_collect(bb_audio *a,unsigned frames) {
    if(!pab_ready(a))return 0;pa_audio_binding *b=pab_current;
    if(frames!=b->lease->frames||!b->wait_complete(b->lease,b->wait_context)||
       pl_collect_view(b->lease)!=1)return pab_fail(b);
    return 1;
}
int bb_owner_audio_mix(bb_audio *a,unsigned k,const float *curve,float fixed,unsigned frames) {
    if(!pab_ready(a))return -1;pa_audio_binding *b=pab_current;unsigned slot;
    int status=po_route_slot(&b->route,k,&slot);if(status<=0)return status;
    if((b->mixed&(1u<<slot))||!bb_owner_mix_view(a,b->lease,slot,k,curve,fixed,frames)) {
        pab_fail(b);return -1;
    }
    b->mixed|=1u<<slot;return 1;
}
int bb_owner_audio_span_release(bb_audio *a) {
    if(!pab_ready(a))return 0;pa_audio_binding *b=pab_current;
    if(b->mixed!=(1u<<b->route.kits)-1u||!pl_release_view(b->lease)||
       b->lease->ticket==UINT32_MAX)return pab_fail(b);
    b->stream.ticket=b->lease->ticket+1;b->stream.state=b->lease->state;
    if(!pa_builder_init(&b->stream.builder,b->wire,&b->route,&a->core,
                       b->stream.ticket,b->stream.state,1))return pab_fail(b);
    PAB_ON_RELEASE(b->lease);
    return 1;
}
int bb_owner_audio_block_end(bb_audio *a) {
    if(!pab_ready(a))return 0;pa_audio_binding *b=pab_current;
    if(!b->block||b->lease->phase!=PL_IDLE||b->stream.builder.failed||
       b->stream.builder.closed||b->wire->count)return pab_fail(b);
    b->block=0;return 1;
}

/* Immutable routing only; never inspect worker-private DSP state. */
int bb_owner_audio_local(bb_audio *a,unsigned k) {
    if(!pab_ready(a))return -1;
    unsigned slot;int status=po_route_slot(&pab_current->route,k,&slot);
    return status<0?-1:status==PO_ROUTE_UNOWNED;
}
