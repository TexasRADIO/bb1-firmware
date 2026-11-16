#include "bb_wire.h"
/* SPDX-License-Identifier: GPL-3.0-only */
#include "bb_audio_trio.h"
#include "bb_audio_family.h"
#if BB_WITH_PREPARED_BANK
#include "prepared_bank_service.h"
#endif
#if BB_WITH_PARTS
#define separated bb_wire_separated
static int outside(const bb_audio *a,const void *p,size_t n) {return bb_audio_groups_outside(a,p,n);}
static int hooks(const bb_audio *a) {return a && (!a->commands.enter==!a->commands.leave);}
static uintptr_t enter(bb_audio *a) {return a->commands.enter?a->commands.enter(a->commands.lock_context):0;}
static void leave(bb_audio *a,uintptr_t t) {if(a->commands.leave)a->commands.leave(a->commands.lock_context,t);}
static int legacy_families(const bb_audio *a) {
    for(unsigned i=0;i<BB_MACHINES;i++)if(bb_audio_families(a)[i])return 0;
    return 1;
}
static int phrase_valid(unsigned n){return n>=16 && n<=64 && !(n%16);}
static bb_trio_program *program(bb_audio *a) {
#if BB_TRIO_EXTERNAL_PROGRAM
    return a->trio.program;
#else
    return &a->trio.program;
#endif
}
static int output_safe(bb_audio *a,const void *out,size_t size) {
    if(!outside(a,out,size))return 0;
#if BB_TRIO_EXTERNAL_PROGRAM
    if(a->trio.program && !separated(a->trio.program,sizeof *a->trio.program,out,size))return 0;
#endif
    return 1;
}
int bb_audio_trio_bind(bb_audio *a,bb_trio_program *p) {
#if BB_TRIO_EXTERNAL_PROGRAM
    if(!hooks(a) || !outside(a,p,sizeof *p))return 0;
    uintptr_t t=enter(a);
    if(a->core.running || !bb_commands_idle(&a->commands) || a->trio.state!=BB_TRIO_FREE || a->trio.program){leave(a,t);return 0;}
    a->trio.program=p;leave(a,t);return 1;
#else
    (void)a;(void)p;return 0;
#endif
}
int bb_audio_trio_begin(bb_audio *a,bb_trio_program **out,uint32_t *ticket) {
    if(!hooks(a) || !output_safe(a,out,sizeof *out) || !output_safe(a,ticket,sizeof *ticket)
       || !separated(out,sizeof *out,ticket,sizeof *ticket))return 0;
    uintptr_t t=enter(a);
    if(!program(a) || a->trio.state!=BB_TRIO_FREE || a->trio.next_ticket==UINT32_MAX){leave(a,t);return 0;}
#if BB_WITH_PREPARED_BANK
    uint32_t id=0;
    if(!pb_begin(&a->parts,program(a),&id)){leave(a,t);return 0;}
    a->trio.next_ticket=id;
#else
    uint32_t id=++a->trio.next_ticket;
#endif
    a->trio.ticket=id;a->trio.state=BB_TRIO_WRITING;leave(a,t);
    *out=program(a);*ticket=id;return 1;
}
int bb_audio_trio_abort(bb_audio *a,uint32_t ticket) {
    if(!hooks(a) || !ticket)return 0;
    uintptr_t t=enter(a);
    int own=a->trio.state==BB_TRIO_WRITING && a->trio.ticket==ticket;
    if(own) {
#if BB_WITH_PREPARED_BANK
        (void)pb_publish(&a->parts,ticket,0);
#endif
        a->trio.state=BB_TRIO_FREE;
    }
    leave(a,t);return own;
}
int bb_audio_trio_publish(bb_audio *a,uint32_t ticket,unsigned phrase) {
    if(!hooks(a) || !ticket)return 0;
    uintptr_t t=enter(a);int own=a->trio.state==BB_TRIO_WRITING && a->trio.ticket==ticket;leave(a,t);
    if(!own)return 0;
    bb_trio_program *p=program(a);
    if(!p || !legacy_families(a) || !phrase_valid(phrase) || p->seed.sample_rate!=a->core.sample_rate ||
       !bb_trio_valid(p)) {bb_audio_trio_abort(a,ticket);return 0;}
#if BB_WITH_PREPARED_BANK
    /* Decode the first playable section on main, outside the IRQ guard. */
    if(!pb_expand_initial(&a->parts,ticket,a->core.sample_rate)) {
        bb_audio_trio_abort(a,ticket);return 0;
    }
#endif
    a->trio.check=bb_trio_check(p);a->trio.phrase=(uint8_t)phrase;
    bb_command c={0};c.type=BB_CMD_TRIO_BANK;c.target=p->initial;
    c.value=(uint16_t)ticket;c.voice=(uint8_t)(ticket>>16);c.note.note=(uint8_t)(ticket>>24);
    /* Publish the ready state and its queue token in ONE short lock. A full
     * FIFO can never expose a queued payload without its owned command. */
    t=enter(a);bb_commands *q=&a->commands;
    if(q->count>=BB_COMMAND_CAPACITY || q->write>=BB_COMMAND_CAPACITY || q->read>=BB_COMMAND_CAPACITY) {
#if BB_WITH_PREPARED_BANK
        (void)pb_publish(&a->parts,ticket,0);
#endif
        a->trio.state=BB_TRIO_FREE;leave(a,t);return 0;
    }
#if BB_WITH_PREPARED_BANK
    if(!pb_publish(&a->parts,ticket,1)) {
        a->trio.state=BB_TRIO_FREE;leave(a,t);return 0;
    }
#endif
    q->items[q->write]=c;q->write=(q->write+1u)%BB_COMMAND_CAPACITY;q->count++;
    a->trio.state=BB_TRIO_QUEUED_SLOT;leave(a,t);
    return 1;
}
int bb_audio_trio_status(bb_audio *a,bb_trio_status *out) {
    if(!hooks(a) || !output_safe(a,out,sizeof *out))return 0;
    uintptr_t t=enter(a);*out=a->trio.status;leave(a,t);return 1;
}
void bb_audio_trio_cancel(bb_audio *a) {
    if(a->trio.state!=BB_TRIO_PINNED)return;
#if BB_WITH_PREPARED_BANK
    /* STOP or an individual part request may cancel an unapplied bank, but a
     * committed bank keeps its source pinned until bounded refill completes. */
    if(a->parts.bank.state!=PB_READY)return;
    pb_cancel_pending(&a->parts);
#endif
    a->trio.status.queued=0;a->trio.status.result=BB_TRIO_CANCELLED;a->trio.state=BB_TRIO_FREE;
}
void bb_audio_trio_transport(bb_audio *a) {bb_audio_trio_cancel(a);a->trio.position=0;}
static int rejected(bb_audio *a) {
#if BB_WITH_PREPARED_BANK
    pb_cancel_pending(&a->parts);
#endif
    a->trio.state=BB_TRIO_FREE;a->trio.status.queued=0;a->trio.status.result=BB_TRIO_REJECTED;return BB_TRIO_REJECTED;
}
static int apply(bb_audio *a,int boundary) {
    bb_trio_program *p=program(a);
#if BB_WITH_PREPARED_BANK
    if(!p || !legacy_families(a) ||
       pb_commit_checked(&a->parts,&a->core,boundary,p,a->trio.check)!=BB_PART_APPLIED)return rejected(a);
#else
    if(!p || !legacy_families(a) || a->trio.check!=bb_trio_check(p) ||
       !bb_parts_load_trio(&a->parts,&a->core,p))return rejected(a);
#endif
    a->trio.status.applied=a->trio.ticket;a->trio.status.queued=0;
    a->trio.status.result=BB_TRIO_APPLIED;a->trio.status.applied_frame=a->core.frame;
    a->trio.status.launches++;
#if !BB_WITH_PREPARED_BANK
    a->trio.state=BB_TRIO_FREE;
#endif
    return BB_TRIO_APPLIED;
}
int bb_audio_trio_command(bb_audio *a,const bb_command *c) {
    uint32_t ticket=(uint32_t)c->value|((uint32_t)c->voice<<16)|((uint32_t)c->note.note<<24);
    bb_trio_program *p=program(a);
    if(a->trio.state!=BB_TRIO_QUEUED_SLOT || !p || !ticket || ticket!=a->trio.ticket
       || c->target!=p->initial || c->step) {
        /* A forged/stale token cannot release another writer's lease. */
        a->trio.status.seen=ticket;a->trio.status.result=BB_TRIO_REJECTED;return BB_TRIO_REJECTED;
    }
    if(!phrase_valid(a->trio.phrase) || p->seed.sample_rate!=a->core.sample_rate ||
       !bb_trio_valid(p) || a->trio.check!=bb_trio_check(p)) {
        a->trio.status.seen=ticket;return rejected(a);
    }
    a->trio.status.seen=ticket;a->trio.status.queued=ticket;a->trio.status.result=BB_TRIO_QUEUED;
    a->trio.state=BB_TRIO_PINNED;
    if(!a->core.running) {
        int result=apply(a,1);
        if(result!=BB_TRIO_APPLIED)return result;
#if BB_WITH_PATTERN
        bb_audio_pattern_cancel(a,0);
#endif
        return result;
    }
    #if BB_WITH_PREPARED_BANK
    if(a->parts.parts.ready && a->parts.parts.queued!=BB_PART_NONE) {
        pp_cancel_pending(&a->parts.parts);a->parts_result=BB_PART_CANCELLED;
    }
    #else
    if(a->parts.ready && a->parts.queued!=BB_PART_NONE) {
        a->parts.queued=BB_PART_NONE;a->parts_result=BB_PART_CANCELLED;
    }
    #endif
#if BB_WITH_PATTERN
    bb_audio_pattern_cancel(a,0);
#endif
    return BB_TRIO_QUEUED;
}
int bb_audio_trio_tick(bb_audio *a) {
    unsigned pos=a->trio.position;a->trio.position=(uint8_t)(pos==191?0:pos+1);
    if(a->trio.state!=BB_TRIO_PINNED)return BB_TRIO_NONE;
    if(!phrase_valid(a->trio.phrase))return rejected(a);
    return pos%a->trio.phrase?BB_TRIO_NONE:apply(a,1);
}
#if BB_WITH_PREPARED_BANK
int bb_audio_bank_service(bb_audio *a,int (*allow)(void *),void *allow_context) {
    if(!hooks(a) || !allow)return PB_SERVICE_INVALID;
    int result=pb_service_one_split(&a->parts,a->core.sample_rate,
        a->commands.enter,a->commands.leave,a->commands.lock_context,allow,allow_context);
    /* The final refill releases the factory source. Only then may another
     * producer borrow the fixed staging payload. Part preparation may follow. */
    if(result==PB_SERVICE_REFILLED) {
        uintptr_t t=enter(a);
        if(a->trio.state==BB_TRIO_PINNED && a->trio.status.applied==a->trio.ticket &&
           a->parts.bank.state==PB_IDLE && !a->parts.bank.source)a->trio.state=BB_TRIO_FREE;
        leave(a,t);
    }
    return result;
}
#endif
#else
int bb_audio_trio_bind(bb_audio *a,bb_trio_program *p){(void)a;(void)p;return 0;}
int bb_audio_trio_begin(bb_audio *a,bb_trio_program **p,uint32_t *t){(void)a;(void)p;(void)t;return 0;}
int bb_audio_trio_abort(bb_audio *a,uint32_t t){(void)a;(void)t;return 0;}
int bb_audio_trio_publish(bb_audio *a,uint32_t t,unsigned p){(void)a;(void)t;(void)p;return 0;}
int bb_audio_trio_status(bb_audio *a,bb_trio_status *s){(void)a;(void)s;return 0;}
int bb_audio_trio_command(bb_audio *a,const bb_command *c){(void)a;(void)c;return BB_TRIO_REJECTED;}
int bb_audio_trio_tick(bb_audio *a){(void)a;return BB_TRIO_NONE;}
void bb_audio_trio_cancel(bb_audio *a){(void)a;}
void bb_audio_trio_transport(bb_audio *a){(void)a;}
#endif
