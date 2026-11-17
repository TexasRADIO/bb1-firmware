/* SPDX-License-Identifier: GPL-3.0-only */
/* Bounded CPU0 producer. Capture never removes queue entries. Drain ONLY the
 * returned entry count through the separately compiled bounded FIFO adapter;
 * producers may append after capture,those new entries belong to next batch.
 * No private worker reads. Full scene/pattern/evolution hooks still require
 * explicit routing;DEFERRED is not an accepted batch or silent fallthrough.
 */
#ifndef BB_PA_PRODUCER_H
#define BB_PA_PRODUCER_H
#include "ordered.h"
#include "bb_control.h"
enum { PA_ROUTE_DEFERRED=2 };
typedef struct {pa_request *wire;const po_routing *route;unsigned failed,closed;} pa_builder;
unsigned bb_apply_pending_live_limit(bb_core *,bb_commands *,bb_live_command_fn,void *,unsigned);
static int pa_builder_fail(pa_builder *b) {
    if(b&&!b->closed){b->failed=1;if(b->wire){b->wire->guard=0;b->wire->check=0;}}
    return PO_ROUTE_REFUSED;
}
static int pa_builder_init(pa_builder *b,pa_request *wire,const po_routing *route,
                            const bb_core *core,uint32_t ticket,uint32_t state,unsigned frames) {
    if(!b||!wire||!core||!po_routing_valid(route)||!ticket||!frames||frames>128||
       !bb_rt_separate(b,sizeof *b,wire,sizeof *wire)||!bb_rt_separate(b,sizeof *b,route,sizeof *route)||
       !bb_rt_separate(wire,sizeof *wire,route,sizeof *route)||!bb_rt_separate(wire,sizeof *wire,core,sizeof *core))return 0;
    for(unsigned m=0;m<BB_MACHINES;m++)if(route->slot[m]!=PO_NO_SLOT&&!po_sound_valid(&core->drums[m].sound))return 0;
    memset(wire,0,sizeof *wire);wire->guard=PA_GUARD;wire->ticket=ticket;wire->previous_state=state;
    wire->kits=route->kits;wire->frames=frames;wire->payload.mask=(1u<<route->kits)-1u;
    for(unsigned m=0;m<BB_MACHINES;m++)if(route->slot[m]!=PO_NO_SLOT)wire->payload.sound[route->slot[m]]=core->drums[m].sound;
    *b=(pa_builder){wire,route,0,0};return 1;
}
static int pa_builder_event(pa_builder *b,unsigned machine,unsigned type,unsigned index,unsigned param,unsigned value) {
    if(!b||b->closed||b->failed)return PO_ROUTE_REFUSED;
    unsigned slot=0;int status=po_route_slot(b->route,machine,&slot);
    if(status!=PO_ROUTE_OK)return status<0?pa_builder_fail(b):status;
    if(!b->wire||b->wire->guard!=PA_GUARD||b->wire->kits!=b->route->kits||
       b->wire->count>=PC_EVENTS||type>7||index>15||param>255||value>255)return pa_builder_fail(b);
    pc_event e={(uint8_t)((type<<5)|(slot<<4)|index),(uint8_t)param,(uint8_t)value};
    /* Semantic bounds are independent of the selected mode;do not mutate
     * baseline during building. Receiver simulates the complete sequence. */
    if(type==PA_EDIT) {
        int max=bb_drum_parameter_max(&b->wire->payload.sound[slot],index,param);
        if(max<0||value>(unsigned)max)return pa_builder_fail(b);
    } else if(type==PA_RECALL) {
        if(index||param||value>=BB_DRUM_KITS)return pa_builder_fail(b);
    } else if(type==PA_GROUP_EDIT) {
        if(index>=GG_COUNT||param>1||value>(param?1u:127u))return pa_builder_fail(b);
    } else if(type==PA_SYNC) {
        if(index||param||value)return pa_builder_fail(b);
    } else {po_command decoded;if(!pc_decode(&e,b->wire->kits,&decoded))return pa_builder_fail(b);}
    b->wire->payload.events[b->wire->count++]=e;b->wire->check=0;return PO_ROUTE_OK;
}
static int pa_builder_command(pa_builder *b,const bb_command *c) {
    if(!b||!c||b->closed||b->failed)return PO_ROUTE_REFUSED;
    switch(c->type) {
    case BB_CMD_DRUM_KIT:return pa_builder_event(b,c->target,PA_RECALL,0,0,c->value);
    case BB_CMD_DRUM_PARAMETER:return pa_builder_event(b,c->target,PA_EDIT,c->voice,c->step,c->value);
    case BB_CMD_DRUM_TRIGGER:return pa_builder_event(b,c->target,PO_HIT,c->voice,0,c->value);
    case BB_CMD_GROUP_LEVEL:case BB_CMD_GROUP_MUTE:
        return pa_builder_event(b,c->target,PA_GROUP_EDIT,c->voice,c->type==BB_CMD_GROUP_MUTE,c->value);
    case BB_CMD_TRIO_BANK:case BB_CMD_PART_LAUNCH:case BB_CMD_DRUM_PATTERN:case BB_CMD_EVOLVE:
        return PA_ROUTE_DEFERRED;
    default:return PO_ROUTE_UNOWNED; /* retained CPU0 core/synth/effects path */
    }
}
static int pa_builder_hit(pa_builder *b,const bb_hit *hit) {
    if(!hit)return pa_builder_fail(b);
    return pa_builder_event(b,hit->machine,PO_HIT,hit->voice,0,hit->velocity);
}
/* Preserve an actual audio_sync_drum boundary before later portable edits.
 * This does not substitute for routing arbitrary scene sound snapshots. */
static int pa_builder_sync(pa_builder *b,unsigned machine) {
    return pa_builder_event(b,machine,PA_SYNC,0,0,0);
}
static int pa_capture_queue(pa_builder *b,bb_commands *q,unsigned *count) {
    if(!b||!q||!count||b->closed||b->failed||!b->wire||(!q->enter!=!q->leave)||
       !bb_rt_separate(q,sizeof *q,b->wire,sizeof *b->wire)||
       !bb_rt_separate(q,sizeof *q,b,sizeof *b)||!bb_rt_separate(q,sizeof *q,count,sizeof *count)||
       !bb_rt_separate(b->wire,sizeof *b->wire,count,sizeof *count))return 0;
    uintptr_t token=q->enter?q->enter(q->lock_context):0;unsigned n=q->count;
    int accepted=n<=BB_COMMAND_CAPACITY&&q->read<BB_COMMAND_CAPACITY;
    for(unsigned i=0;accepted&&i<n;i++) {
        int status=pa_builder_command(b,&q->items[(q->read+i)%BB_COMMAND_CAPACITY]);
        if(status<0||status==PA_ROUTE_DEFERRED)accepted=0;
    }
    if(q->leave)q->leave(q->lock_context,token);
    if(!accepted){pa_builder_fail(b);return 0;}*count=n;return 1;
}
static int pa_builder_seal(pa_builder *b) {
    if(!b||b->closed||b->failed||!b->wire||b->wire->guard!=PA_GUARD)return 0;
    b->wire->check=pa_hash(b->wire);b->closed=1;return 1;
}
#endif
