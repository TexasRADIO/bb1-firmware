/* SPDX-License-Identifier: GPL-3.0-only */
/* Offline protocol experiment, NOT installed HAL code.
 * One physical mailbox dispatch owns one immutable descriptor containing
 * one or two <=64-frame logical fragments. Only FINAL return permits the
 * RAM dispatcher to acknowledge the physical ticket. No intermediate ACK,
 * CPU0 private-state read, live write, or second publication is permitted.
 *
 * The legacy transaction kernel calls its fragment token physical_job.
 * Here that token is explicitly LOGICAL, distinct from mailbox_job. A HAL
 * port needs a NEW job kind and counters; it must not report two dispatches
 * when only one occurred. The final commit uses the last logical token.
 */
#ifndef BB_RETAINED_CHAIN_H
#define BB_RETAINED_CHAIN_H
#include "retained_transaction.h"
typedef struct {
    uint32_t mailbox_job,count;
    bb_rt_request fragment[2];
    uint32_t check;
} bb_rt_chain;
_Static_assert(sizeof(bb_rt_chain)==68,"fixed pointer-free chain descriptor");
static uint32_t bb_rt_chain_check(const bb_rt_chain *c){
    return bb_c1_drum_hash(UINT32_C(0x43484e31),c,offsetof(bb_rt_chain,check));
}
/* CPU0 while the worker is idle, before publication. Invalid inputs are
 * refused by run; construction itself never dereferences a DSP object. */
static bb_rt_chain bb_rt_chain_make(uint32_t mailbox_job,uint32_t transaction,
                                   unsigned kits,unsigned frames){
    bb_rt_chain c={0};c.mailbox_job=mailbox_job;c.count=frames>64u?2u:1u;
    c.fragment[0]=bb_rt_request_make(transaction,transaction,kits,frames,0,frames>64u?64u:frames);
    if(c.count==2u)c.fragment[1]=bb_rt_request_make(transaction,transaction+1u,kits,frames,64u,frames-64u);
    c.check=bb_rt_chain_check(&c);return c;
}
static int bb_rt_chain_request_equal(const bb_rt_request *a,const bb_rt_request *b){
    return a->transaction==b->transaction && a->job==b->job && a->kits==b->kits &&
        a->frames==b->frames && a->offset==b->offset && a->count==b->count && a->check==b->check;
}
/* Worker-only. The transport has already validated its exact next physical
 * ticket and fixed entry. Copy and validate BOTH descriptors before any DSP
 * mutation. Caller must never publish/overwrite wire before final ACK/hold.
 * No timer, clock, mailbox mutation or retry is hidden inside this function.
 */
static int bb_rt_chain_run(bb_rt_state *s,bb_c1_drum_job *j,const bb_rt_chain *wire,
                           uint32_t mailbox_job,uint32_t *result){
    if(!s || !j || !bb_rt_separate(s,sizeof *s,j,sizeof *j))return 0;
    if(!wire || !result)return bb_rt_fail(s);
    if(!bb_rt_separate(wire,sizeof *wire,s,sizeof *s) || !bb_rt_separate(wire,sizeof *wire,j,sizeof *j) ||
       !bb_rt_separate(result,sizeof *result,s,sizeof *s) || !bb_rt_separate(result,sizeof *result,j,sizeof *j) ||
       !bb_rt_separate(result,sizeof *result,wire,sizeof *wire))return bb_rt_fail(s);
    bb_rt_chain c;bb_c1_drum_copy(&c,wire,sizeof c);
    if(!bb_rt_valid(s) || s->phase!=BB_RT_PREPARED || !mailbox_job || c.mailbox_job!=mailbox_job ||
       c.check!=bb_rt_chain_check(&c) || c.count!=(s->frames>64u?2u:1u))return bb_rt_fail(s);
    bb_rt_chain expected=bb_rt_chain_make(mailbox_job,s->transaction,s->kits,s->frames);
    for(unsigned n=0;n<2u;n++)if(!bb_rt_chain_request_equal(&c.fragment[n],&expected.fragment[n]))return bb_rt_fail(s);
    uint32_t value=0;
    for(unsigned n=0;n<c.count;n++){
        if(!bb_rt_run(s,j,&c.fragment[n],c.fragment[n].job,&value))return 0;
        if(n+1u<c.count && value!=bb_rt_progress(&c.fragment[n]))return bb_rt_fail(s);
    }
    if(!bb_rt_valid(s) || s->phase!=BB_RT_DONE || s->output_check!=value)return bb_rt_fail(s);
    *result=value;return 1;
}
#endif
