/* SPDX-License-Identifier: GPL-3.0-only */
/* OFFLINE ownership kernel, not a mailbox/HAL integration.
 * Worker owns persistent DSP; CPU0 retains only expected state seal/ticket.
 * Wire and output must be immutable under an externally proven lease.
 * No clocks, waits, reset, retries, allocation or hidden transport here.
 * Full state hashing stays at worker entry/exit; full bus hashing/finite checks
 * stay on both sides. Export validates full state before any live copy.
 */
#ifndef BB_PERSISTENT_OWNER_EXPERIMENT_H
#define BB_PERSISTENT_OWNER_EXPERIMENT_H
#include "retained_transaction.h"
enum { PO_READY=1,PO_DONE,PO_FAILED,PO_HIT=1,PO_SET,PO_GROUP };
enum { PO_COMMANDS=64,PO_GUARD=0x504F5731u };
typedef struct { uint8_t type,kit,index,param;uint32_t value; } po_command;
typedef struct {
    uint32_t guard,ticket,kits,frames,previous_state,count;
    po_command commands[PO_COMMANDS];uint32_t check;
} po_request;
typedef struct {uint32_t ticket,kits,frames,state,buses,check;} po_result;
typedef struct {bb_c1_drum_job payload;uint32_t phase,ticket,state;} po_owner;
_Static_assert(sizeof(po_command)==8,"pointer-free command");
_Static_assert(sizeof(po_request)==540,"bounded request; stack/RAM still target-unproven");
static uint32_t po_request_hash(const po_request *r) {
    return bb_c1_drum_hash(PO_GUARD,r,offsetof(po_request,check));
}
static uint32_t po_result_hash(const po_result *r) {
    return bb_c1_drum_hash(PO_GUARD^0x52534C54u,r,offsetof(po_result,check));
}
static uint32_t po_state_hash(const bb_c1_drum_job *p) {
    uint32_t h=bb_c1_drum_hash(PO_GUARD,&p->kits,sizeof p->kits);
    for(unsigned k=0;k<p->kits;k++) {
        h=bb_c1_drum_hash(h,&p->kit[k],sizeof p->kit[k]);
        h=bb_c1_drum_hash(h,&p->groups[k],sizeof p->groups[k]);
    }
    return h;
}
static uint32_t po_bus_hash(const bb_c1_drum_job *p,uint32_t state) {
    uint32_t h=bb_c1_drum_hash(state,&p->ticket,sizeof p->ticket);
    h=bb_c1_drum_hash(h,&p->frames,sizeof p->frames);
    h=bb_c1_drum_hash(h,&p->input_check,sizeof p->input_check);
    for(unsigned k=0;k<p->kits;k++)for(unsigned bus=0;bus<3;bus++)
        h=bb_c1_drum_hash(h,p->bus[k][bus],p->frames*sizeof(float));
    return h;
}
static int po_fail(po_owner *o){o->phase=PO_FAILED;return 0;}
static int po_shape(const po_owner *o) {
    const bb_c1_drum_job *p=&o->payload;
    return p->guard==BB_C1_DRUM_GUARD && p->end_guard==BB_C1_DRUM_GUARD && p->kits && p->kits<=2;
}
/* Cold, verified-idle only. Not a fault recovery entrypoint. */
static int po_init(po_owner *o,const drum808_t *kits,const gg_state *groups,unsigned count) {
    if(!o||!kits||!groups||!count||count>2)return 0;
    if(!bb_rt_separate(o,sizeof *o,kits,count*sizeof *kits)||!bb_rt_separate(o,sizeof *o,groups,count*sizeof *groups))return 0;
    for(unsigned k=0;k<count;k++)if(!bb_group_state_valid(&groups[k]))return 0;
    __builtin_memset(o,0,sizeof *o);o->payload.kits=count;
    o->payload.guard=o->payload.end_guard=BB_C1_DRUM_GUARD;
    for(unsigned k=0;k<count;k++) {
        bb_c1_drum_copy(&o->payload.kit[k],&kits[k],sizeof *kits);
        bb_c1_drum_copy(&o->payload.groups[k],&groups[k],sizeof *groups);
    }
    o->state=po_state_hash(&o->payload);o->phase=PO_READY;return 1;
}
static int po_command_valid(const po_command *c,unsigned kits) {
    if(c->kit>=kits)return 0;
    if(c->type==PO_HIT) {
        float velocity;__builtin_memcpy(&velocity,&c->value,sizeof velocity);
        return c->index<D8_NUM && !c->param && velocity>=0.0f && velocity<=1.0f;
    }
    if(c->type==PO_SET) {
        if(c->index>D8_KIT || c->param>=drum808_nparams(c->index))return 0;
        const x0x_param_t *p=drum808_param(c->index,c->param);
        return p && c->value<=p->max;
    }
    return c->type==PO_GROUP && c->index<GG_COUNT && !c->param && !(c->value&~0x17fu);
}
static void po_apply(drum808_t *kits,gg_state *groups,const po_command *c) {
    if(c->type==PO_HIT) {
        float velocity;__builtin_memcpy(&velocity,&c->value,sizeof velocity);
        drum808_trigger(&kits[c->kit],c->index,velocity);
    } else if(c->type==PO_SET)drum808_set(&kits[c->kit],c->index,c->param,(int)c->value);
    else (void)gg_set(&groups[c->kit],c->index,c->value&127u,(c->value>>8)&1u);
}
/* Shared post-validation rendering. Call only after the complete request and
 * all controls have been accepted; this helper does not relax admission. */
static int po_render(po_owner *o,uint32_t ticket,unsigned frames,po_result *result) {
    o->payload.ticket=ticket;o->payload.frames=frames;
    /* This field is unused by the persistent-owner input protocol. Reuse it
     * as authenticated START-of-span activity metadata, not end activity:
     * a voice may finish during this span and its final samples still mix.
     * CPU0 must never inspect worker-private voice state to make this choice. */
    o->payload.input_check=0;
    for(unsigned k=0;k<o->payload.kits;k++)
        if(drum808_active(&o->payload.kit[k]))o->payload.input_check|=1u<<k;
    for(unsigned k=0;k<o->payload.kits;k++) {
        for(unsigned bus=0;bus<3;bus++)for(unsigned i=0;i<frames;i++)o->payload.bus[k][bus][i]=0;
        for(unsigned offset=0;offset<frames;) {
            unsigned n=frames-offset>64?64:frames-offset;
            bb_proposal_group_span(&o->payload.kit[k],&o->payload.groups[k],o->payload.bus[k][0]+offset,
                o->payload.bus[k][1]+offset,o->payload.bus[k][2]+offset,(int)n);
            offset+=n;
        }
        if(!bb_group_state_valid(&o->payload.groups[k]))return po_fail(o);
    }
    o->state=po_state_hash(&o->payload);o->ticket=ticket;o->phase=PO_DONE;
    po_result reply={ticket,o->payload.kits,frames,o->state,po_bus_hash(&o->payload,o->state),0};
    reply.check=po_result_hash(&reply);*result=reply;return 1;
}
/* Worker-local snapshot only. Compact expansion already owns a validated,
 * immutable local request; do not copy its540bytes onto a second stack frame.
 * Retain every normal request check before any command or render mutation. */
static __attribute__((noinline)) int po_run_snapshot(po_owner *o,const po_request *r,po_result *result) {
    if(!o)return 0;
    if(!r||!result||!bb_rt_separate(o,sizeof *o,r,sizeof *r)||
       !bb_rt_separate(o,sizeof *o,result,sizeof *result)||!bb_rt_separate(r,sizeof *r,result,sizeof *result))return po_fail(o);
    if(o->phase!=PO_READY||!po_shape(o))return po_fail(o);
    if(r->guard!=PO_GUARD || r->check!=po_request_hash(r) || !r->ticket || o->ticket==UINT32_MAX ||
       r->ticket!=o->ticket+1u || r->kits!=o->payload.kits || !r->frames || r->frames>128 ||
       r->count>PO_COMMANDS || r->previous_state!=o->state || o->state!=po_state_hash(&o->payload))return po_fail(o);
    for(unsigned k=0;k<r->kits;k++)if(!bb_group_state_valid(&o->payload.groups[k]))return po_fail(o);
    for(unsigned c=0;c<PO_COMMANDS;c++) {
        if(c<r->count){if(!po_command_valid(&r->commands[c],r->kits))return po_fail(o);}
        else {const po_command *p=&r->commands[c];if(p->type||p->kit||p->index||p->param||p->value)return po_fail(o);}
    }
    for(unsigned c=0;c<r->count;c++)po_apply(o->payload.kit,o->payload.groups,&r->commands[c]);
    return po_render(o,r->ticket,r->frames,result);
}
/* External wire always retains its bounded private snapshot and alias checks. */
static __attribute__((noinline)) int po_run(po_owner *o,const po_request *wire,po_result *result) {
    if(!o)return 0;
    if(!wire||!result||!bb_rt_separate(o,sizeof *o,wire,sizeof *wire)||
       !bb_rt_separate(o,sizeof *o,result,sizeof *result)||!bb_rt_separate(wire,sizeof *wire,result,sizeof *result))return po_fail(o);
    if(o->phase!=PO_READY||!po_shape(o))return po_fail(o);
    po_request r;bb_c1_drum_copy(&r,wire,sizeof r);
    return po_run_snapshot(o,&r,result);
}
/* CPU0 only AFTER externally checked completion. Do not read worker-owned
 * kit/group state: result and full buses are the only published payload.
 * No live/bus writes on rejection. State export is a separate idle operation.
 */
static int po_validate_output(po_owner *o,const po_result *reply,uint32_t ticket,unsigned kits,unsigned frames) {
    if(!o||!reply)return 0;
    if(!bb_rt_separate(reply,sizeof *reply,o,sizeof *o))return po_fail(o);
    if(o->phase!=PO_DONE||!po_shape(o)||reply->check!=po_result_hash(reply)||
       reply->ticket!=ticket||reply->kits!=kits||reply->frames!=frames||o->ticket!=ticket||
       !kits||kits>2||!frames||frames>128||o->payload.kits!=kits||o->payload.frames!=frames||
       o->payload.ticket!=ticket||o->state!=reply->state||
       (o->payload.input_check&~((1u<<kits)-1u)))return po_fail(o);
    uint32_t h=bb_c1_drum_hash(reply->state,&ticket,sizeof ticket);
    h=bb_c1_drum_hash(h,&frames,sizeof frames);
    h=bb_c1_drum_hash(h,&o->payload.input_check,sizeof o->payload.input_check);
    for(unsigned k=0;k<kits;k++)for(unsigned bus=0;bus<3;bus++)for(unsigned i=0;i<frames;i++) {
        uint32_t bits;__builtin_memcpy(&bits,&o->payload.bus[k][bus][i],sizeof bits);
        if((bits&0x7f800000u)==0x7f800000u)return po_fail(o);
        h=bb_c1_drum_hash_word(h,bits);
    }
    if(h!=reply->buses)return po_fail(o);
    return 1; /* still DONE: a view reader must retain exclusive ownership */
}
static int po_accept(po_owner *o,const po_result *reply,uint32_t ticket,unsigned kits,unsigned frames,
                     uint32_t *expected_state,float out[2][3][128]) {
    if(!o||!reply||!expected_state||!out)return 0;
    if(!bb_rt_separate(out,2u*3u*128u*sizeof(float),o,sizeof *o)||
       !bb_rt_separate(expected_state,sizeof *expected_state,o,sizeof *o)||
       !bb_rt_separate(reply,sizeof *reply,o,sizeof *o)||
       !bb_rt_separate(reply,sizeof *reply,out,2u*3u*128u*sizeof(float))||
       !bb_rt_separate(expected_state,sizeof *expected_state,out,2u*3u*128u*sizeof(float))||
       !bb_rt_separate(expected_state,sizeof *expected_state,reply,sizeof *reply))return po_fail(o);
    if(!po_validate_output(o,reply,ticket,kits,frames))return 0;
    for(unsigned k=0;k<kits;k++)for(unsigned bus=0;bus<3;bus++)
        bb_c1_drum_copy(out[k][bus],o->payload.bus[k][bus],frames*sizeof(float));
    *expected_state=reply->state;o->phase=PO_READY;return 1;
}
/* Verified idle export for saves/inspection. No render-span state copy.
 * HAL must first establish no outstanding job; this function does not wait.
 */
static int po_export(po_owner *o,uint32_t expected,unsigned count,drum808_t *kits,gg_state *groups) {
    if(!o||!kits||!groups)return 0;
    if(!count||count>2||!bb_rt_separate(o,sizeof *o,kits,count*sizeof *kits)||
       !bb_rt_separate(o,sizeof *o,groups,count*sizeof *groups)||
       !bb_rt_separate(kits,count*sizeof *kits,groups,count*sizeof *groups))return po_fail(o);
    if(o->phase!=PO_READY||!po_shape(o)||o->payload.kits!=count||expected!=o->state||
       po_state_hash(&o->payload)!=expected)return po_fail(o);
    for(unsigned k=0;k<count;k++)if(!bb_group_state_valid(&o->payload.groups[k]))return po_fail(o);
    for(unsigned k=0;k<count;k++) {
        bb_c1_drum_copy(&kits[k],&o->payload.kit[k],sizeof *kits);
        bb_c1_drum_copy(&groups[k],&o->payload.groups[k],sizeof *groups);
    }
    return 1;
}
#endif
