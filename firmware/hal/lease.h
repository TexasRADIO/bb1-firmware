/* SPDX-License-Identifier: GPL-3.0-only */
/* HOST-ONLY lease adapter. Test generates C11 atomic fields for the actual
 * retained mailbox algorithms. This is NOT a Pi32 barrier/cache proof.
 * There is no reset/retry API. Timeout forbids private reads and publication;
 * an external confirmed hold permits fault bookkeeping, NOT reuse/commit.
 */
#ifndef BB_PO_LEASE_EXPERIMENT_H
#define BB_PO_LEASE_EXPERIMENT_H
#include "owner-model.h"
#include "bb_c1_completion.h"
#ifndef BB_PL_REQUEST_VALID
#define BB_PL_REQUEST_VALID(r) ((r)->guard==PO_GUARD&&(r)->check==po_request_hash(r))
#endif
#ifndef BB_PL_JOB_GUARD_VALID
#define BB_PL_JOB_GUARD_VALID(seed) ((seed)==PO_GUARD)
#endif
#ifndef BB_PL_RUN
#define BB_PL_RUN(o,r,result) po_run(o,r,result)
#endif
#ifndef BB_PL_FRAMES_VALID
#define BB_PL_FRAMES_VALID(r) ((r)->frames && (r)->frames<=128u)
#endif
#ifndef BB_PL_ACCEPT
#define BB_PL_ACCEPT(o,r,t,k,f,s,out) po_accept(o,r,t,k,f,s,out)
#endif
#ifndef BB_LEASE_PRIVATE_READ
#define BB_LEASE_PRIVATE_READ() ((void)0)
#endif
enum { PL_IDLE=1,PL_INFLIGHT,PL_NEEDS_HOLD,PL_FAULT };
typedef struct {
    po_owner owner;
    /* Preserve the target mailbox's two independent cache lines even when
     * embedded after a DSP object whose size is not a multiple of64. */
    _Alignas(64) bb_c1_mailbox mail;
    po_request wire;po_result reply;
    uint32_t phase,ticket,state,kits,frames;
} po_lease;
_Static_assert(_Alignof(po_lease)>=64,"lease preserves mailbox alignment");
_Static_assert(offsetof(po_lease,mail)%64==0,"CPU0 mailbox line aligned");
_Static_assert((offsetof(po_lease,mail)+offsetof(bb_c1_mailbox,alive))%64==0,
               "CPU1 mailbox line aligned");
/* Cold, no worker exists. Never call to recover a faulted actor. */
static int pl_init(po_lease *l,const drum808_t *kits,const gg_state *groups,unsigned count) {
    if(!l)return 0;
    __builtin_memset(l,0,sizeof *l);
    if(!po_init(&l->owner,kits,groups,count))return 0;
    l->state=l->owner.state;l->kits=count;l->phase=PL_IDLE;
    l->mail.alive=BB_C1_ALIVE;return 1;
}
/* CPU0 owns wire only while idle. No actor/private DSP reads here. */
static int pl_publish(po_lease *l,const po_request *r) {
    if(!l||!r||l->phase!=PL_IDLE||l->ticket==UINT32_MAX||r->ticket!=l->ticket+1u||
       r->previous_state!=l->state||r->kits!=l->kits||!BB_PL_FRAMES_VALID(r)||
       !BB_PL_REQUEST_VALID(r)||
       !bb_rt_separate(l,sizeof *l,r,sizeof *r))return 0;
    if(l->mail.job!=l->ticket||l->mail.done!=l->ticket||l->mail.bad||l->mail.alive!=BB_C1_ALIVE) {
        l->phase=PL_NEEDS_HOLD;return 0;
    }
    bb_c1_drum_copy(&l->wire,r,sizeof *r);l->frames=r->frames;
    l->phase=PL_INFLIGHT;
    if(!bb_c1_publish(&l->mail,r->guard,r->ticket)) {
        l->phase=PL_NEEDS_HOLD;return 0;
    }
    l->ticket=r->ticket;return 1;
}
/* Worker only. Completion is the final release after all private writes.
 * New job kind/value contract would need explicit target negotiation.
 */
static int pl_worker_step(po_lease *l,uint32_t *last_job) {
    uint32_t ticket=l->mail.job;BB_C1_SYNC();
    if(ticket==*last_job)return 0;
    uint32_t seed=l->mail.seed,nonce=l->mail.nonce,check=l->mail.check;
    if(!ticket||*last_job==UINT32_MAX||ticket!=*last_job+1u||!BB_PL_JOB_GUARD_VALID(seed)||nonce!=ticket||
       check!=bb_c1_request_check(ticket,seed,nonce)||seed!=l->wire.guard||
       !BB_PL_RUN(&l->owner,&l->wire,&l->reply)) {
        l->mail.bad_job=ticket;BB_C1_SYNC();l->mail.bad=1;return -1;
    }
    l->mail.result=l->reply.check;l->mail.result_check=bb_c1_result_check(ticket,l->reply.check);
    *last_job=ticket;BB_C1_SYNC();l->mail.done=ticket;BB_C1_SYNC();return 1;
}
/* CPU0: old/missing completion returns waiting, with NO private access.
 * Late completion after timeout is ignored even if every checksum is valid.
 */
static int pl_collect(po_lease *l,float out[2][3][128]) {
    if(!l||l->phase!=PL_INFLIGHT)return 0;
    uint32_t value=0;int status=bb_c1_complete(&l->mail,l->ticket,&value);
    if(!status)return 0;
    if(status<0){l->phase=PL_NEEDS_HOLD;return -1;}
    BB_LEASE_PRIVATE_READ();
    if(value!=l->reply.check||!BB_PL_ACCEPT(&l->owner,&l->reply,l->ticket,l->kits,l->frames,&l->state,out)) {
        l->phase=PL_NEEDS_HOLD;return -1;
    }
    l->phase=PL_IDLE;return 1;
}
/* Caller owns the unchanged real time bound. This emits no hold/reset. */
static int pl_timeout(po_lease *l) {
    if(!l||l->phase!=PL_INFLIGHT)return 0;
    l->phase=PL_NEEDS_HOLD;return 1;
}
/* Only a real independently verified hold can supply exactly1. Even then,
 * actor remains unusable: no private scan, retry, release or commit occurs.
 */
static int pl_confirm_hold(po_lease *l,unsigned verified_hold) {
    if(!l||l->phase!=PL_NEEDS_HOLD||verified_hold!=1)return 0;
    l->phase=PL_FAULT;return 1;
}
#endif
