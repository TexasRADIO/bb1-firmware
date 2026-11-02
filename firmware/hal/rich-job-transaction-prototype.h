/* SPDX-License-Identifier: GPL-3.0-only */
/* OFFLINE contract. The HAL must prove exclusive ownership before ANY call.
 * In particular, a timeout is not permission to inspect/reuse this payload.
 * Completion hash is diagnostic integrity, not authentication. */
#ifndef BB_RICH_JOB_TRANSACTION_PROTOTYPE_H
#define BB_RICH_JOB_TRANSACTION_PROTOTYPE_H
#include "rich-native-plan-prototype.h"
#include "bb_c1_drum_job.h"
#include "coefficient_trace.h"
typedef struct {
 uint32_t guard,ticket,epoch,input_check;
 bb_rsp_state next;
 bb_rj_job render;
 uint32_t end_guard;
} bb_rjt_job;
/* queued (opt 2): the delayed-queue count CPU0 sealed. CPU0 keeps its own copy,
 * so a changed count is refused by bb_rjt_valid before any hash is compared. */
typedef struct {uint32_t ticket,epoch,frames,generation,committed,queued,control;} bb_rjt_lease;
/* Lane-word transaction integrity (timing candidate, 2026-10-27).
 * Version bump: a job sealed by the previous byte-FNV protocol never validates.
 * Both cores execute the same image; no saved file, asset or wire format uses
 * this private check. */
#define BB_RJT_GUARD UINT32_C(0xD8110A74)
_Static_assert(sizeof(bb_rjt_job)<=sizeof(bb_c1_drum_job),"Review retained pool capacity");
/* Coverage: ticket, epoch, next state, frames, count, active hits, whole kit
 * (including padding), groups and, for results, every consumed output sample.
 * Opt 2 (2.0 design and optimisation review, O2): of the next state's delayed
 * queue only the entries in use, [0,count), are hashed, as for hits and output
 * samples. The queue header (floor, generation, count, active) and every other
 * player field stay hashed; count is also bound to the lease; CPU0's verify
 * requires every unused entry to be zero (BB_RJT_QUEUE_TAIL_CHECK). Every
 * covered field is a 4-byte aligned whole number of words, so the hash reads
 * native aligned words; no byte assembly, no memcpy call. */
typedef uint32_t bb_rjt_word __attribute__((__may_alias__));
/* ---- Opt 3 (2.0 design and optimisation review, O3): the kit's control region ----
 * drum808_t is [control A | pan glide | control B | voice state]:
 *   control A [0, pan_lc)  pots, switches, kit pots, track pans, pan targets;
 *   pan glide [pan_lc, potv)  pan_lc/pan_rc/pan_dl/pan_dr, written per sample;
 *   control B [potv, bd)   engineering values, shapers, vol, vel_depth, choke;
 *   voice state [bd, end)  every voice, bank, metal, lanes.
 * INVARIANT: a CPU1 job never writes control A or control B (0 of 477,143
 * oracle jobs, claude/2.0/bench/static-region-probe.patch). Only CPU0 sets
 * them (sound edits, under the P1b fingerprint's sound revision). So:
 *  - CPU0's seal hashes the control regions once and keeps that digest in its
 *    own lease (control); CPU0's verify uses it instead of re-reading them.
 *  - CPU1's result hash still reads them from the job. A CPU1 write there
 *    before its result is refused twice: CPU1's own guard in bb_rjt_run
 *    (BB_RJT_CPU1_CONTROL_GUARD) and CPU0's verify, whose expected result is
 *    built from the sealed digest. A write after the result is never adopted:
 *    CPU0 copies back only the pan glide and the voice state, and the next
 *    job copies the whole live kit in again.
 *  - Host builds can assert that the skipped copy was a no-op
 *    (BB_RJT_HOST_CONTROL_ASSERT). */
#define BB_RJT_KIT_GLIDE offsetof(drum808_t,pan_lc)
#define BB_RJT_KIT_CONTROL_B offsetof(drum808_t,potv)
#define BB_RJT_KIT_STATE offsetof(drum808_t,bd)
_Static_assert(BB_RJT_KIT_GLIDE%4u==0u&&BB_RJT_KIT_CONTROL_B%4u==0u&&BB_RJT_KIT_STATE%4u==0u&&
               0u<BB_RJT_KIT_GLIDE&&BB_RJT_KIT_GLIDE<BB_RJT_KIT_CONTROL_B&&BB_RJT_KIT_CONTROL_B<BB_RJT_KIT_STATE&&
               BB_RJT_KIT_STATE<sizeof(drum808_t)&&
               offsetof(drum808_t,pan_dr)+sizeof(((drum808_t*)0)->pan_dr)==BB_RJT_KIT_CONTROL_B,
               "kit regions: control A, pan glide, control B, voice state");
#ifndef BB_RJT_CPU1_CONTROL_GUARD
#define BB_RJT_CPU1_CONTROL_GUARD 1
#endif
#define BB_RJT_ALIGNED(T,f) (offsetof(T,f)%4u==0u)
_Static_assert(__alignof__(bb_rjt_job)>=4u,"word-aligned job");
_Static_assert(BB_RJT_ALIGNED(bb_rjt_job,ticket)&&offsetof(bb_rjt_job,epoch)==offsetof(bb_rjt_job,ticket)+4u,"ticket+epoch words");
_Static_assert(BB_RJT_ALIGNED(bb_rjt_job,next)&&sizeof(bb_rsp_state)%4u==0u,"next state words");
/* Opt 2 layout: the queue's entries open the state; everything after them
 * (queue header, then the player fields) is one contiguous word-aligned tail. */
#define BB_RJT_NEXT_TAIL offsetof(bb_rsp_state,queue.floor)
_Static_assert(offsetof(bb_rsp_state,queue)==0u&&offsetof(bb_delayed,items)==0u&&
               BB_RJT_NEXT_TAIL==sizeof(((bb_delayed*)0)->items)&&BB_RJT_NEXT_TAIL%4u==0u&&
               sizeof(bb_delayed_event)%4u==0u&&(sizeof(bb_rsp_state)-BB_RJT_NEXT_TAIL)%4u==0u,
               "next state: queue entries, then one word tail");
_Static_assert(BB_RJT_ALIGNED(bb_rjt_job,render)&&BB_RJT_ALIGNED(bb_rj_job,frames)&&
               offsetof(bb_rj_job,count)==offsetof(bb_rj_job,frames)+4u,"frames+count words");
_Static_assert(BB_RJT_ALIGNED(bb_rj_job,hits)&&sizeof(bb_rj_hit)%4u==0u,"hit words");
_Static_assert(BB_RJT_ALIGNED(bb_rj_job,kit)&&sizeof(drum808_t)%4u==0u,"kit words");
_Static_assert(BB_RJT_ALIGNED(bb_rj_job,groups)&&sizeof(gg_state)%4u==0u,"group words");
_Static_assert(BB_RJT_ALIGNED(bb_rj_job,bus)&&sizeof(float)==4u,"bus words");
#undef BB_RJT_ALIGNED
#define BB_RJT_LANE_MUL UINT32_C(0x9E3779B1)
#define BB_RJT_EXP UINT32_C(0x7f800000)
/* One lane step. XOR, rotate and odd multiply are each bijective, so a step is
 * a bijection of the lane for a fixed word AND of the word for a fixed lane:
 * any change confined to one aligned 32-bit word always changes the digest
 * (stronger than byte FNV, which guarantees only single-byte changes). The
 * rotate moves high bits into the multiply so paired top-bit flips do not
 * cancel as they would in plain word FNV. */
static inline __attribute__((always_inline)) uint32_t bb_rjt_lane(uint32_t l,uint32_t w){
 l^=w;l=(l<<13)|(l>>19);return l*BB_RJT_LANE_MUL;
}
/* Four independent lanes break the serial multiply chain of byte FNV.
 * finite!=0 additionally rejects any word whose exponent is all ones, using
 * the same loaded representation that is hashed (output samples only). */
static inline __attribute__((always_inline)) int bb_rjt_digest_body(const bb_rjt_word *w,
 uint32_t n,uint32_t finite,uint32_t *out){
 uint32_t a=UINT32_C(0x243F6A88)^n,b=UINT32_C(0x85A308D3),c=UINT32_C(0x13198A2E),d=UINT32_C(0x03707344);
 uint32_t i=0;
 for(;n-i>=4u;i+=4u){
  const uint32_t w0=w[i],w1=w[i+1u],w2=w[i+2u],w3=w[i+3u];
  if(finite&&((w0&BB_RJT_EXP)==BB_RJT_EXP||(w1&BB_RJT_EXP)==BB_RJT_EXP||
              (w2&BB_RJT_EXP)==BB_RJT_EXP||(w3&BB_RJT_EXP)==BB_RJT_EXP))return 0;
  a=bb_rjt_lane(a,w0);b=bb_rjt_lane(b,w1);c=bb_rjt_lane(c,w2);d=bb_rjt_lane(d,w3);
 }
 if(i<n){const uint32_t w0=w[i];if(finite&&(w0&BB_RJT_EXP)==BB_RJT_EXP)return 0;a=bb_rjt_lane(a,w0);i++;}
 if(i<n){const uint32_t w1=w[i];if(finite&&(w1&BB_RJT_EXP)==BB_RJT_EXP)return 0;b=bb_rjt_lane(b,w1);i++;}
 if(i<n){const uint32_t w2=w[i];if(finite&&(w2&BB_RJT_EXP)==BB_RJT_EXP)return 0;c=bb_rjt_lane(c,w2);}
 /* Bijective in each lane given the others. */
 a=bb_rjt_lane(a,b);a=bb_rjt_lane(a,c);a=bb_rjt_lane(a,d);
 *out=a;return 1;
}
/* As for the previous RAM hash: run from executable RAM on both cores through
 * one checked fixed target; a corrupted local pointer computes the identical
 * result inline rather than calling an unchecked address. */
#if defined(__APPLE__)
#define BB_RJT_LANE_LOCATION __attribute__((noinline))
#else
#define BB_RJT_LANE_LOCATION __attribute__((noinline,section(".ram_text.bb_rjt_lanes")))
#endif
static int BB_RJT_LANE_LOCATION bb_rjt_digest_ram(const bb_rjt_word *w,uint32_t n,
 uint32_t finite,uint32_t *out){
 return bb_rjt_digest_body(w,n,finite,out);
}
#undef BB_RJT_LANE_LOCATION
static int bb_rjt_digest(const void *data,uint32_t bytes,uint32_t finite,uint32_t *out){
 int (*volatile invoke)(const bb_rjt_word *,uint32_t,uint32_t,uint32_t *)=bb_rjt_digest_ram;
 int (*target)(const bb_rjt_word *,uint32_t,uint32_t,uint32_t *)=invoke;
 if(target!=bb_rjt_digest_ram)return bb_rjt_digest_body((const bb_rjt_word *)data,bytes/4u,finite,out);
 __asm__ volatile("" : "+r"(target));
 return target((const bb_rjt_word *)data,bytes/4u,finite,out);
}
/* Fold one segment digest. Bijective in the digest for a fixed chain value. */
static uint32_t bb_rjt_segment(uint32_t h,const void *data,uint32_t bytes){
 uint32_t d;(void)bb_rjt_digest(data,bytes,0u,&d);return bb_rjt_lane(h,d);
}
/* ---- Opt 2: delayed-queue entries in use ----
 * bb_delayed reads items[0,count) only and zeroes every entry it vacates
 * (bb_delayed.c begin, stop, native_step, pop, remove_mask), so a live player
 * state is canonical: entries [count,capacity) are zero. Unused entries are
 * never hashed and never copied into a live destination, so their contents
 * cannot reach audio. With BB_RJT_QUEUE_TAIL_CHECK (default) CPU0 also
 * requires them to be zero in the job before adopting it: any change to any
 * byte of the job's next state is then still refused, as with the whole-state
 * hash (the fault oracle's mode 4 corrupts an unused entry; 60/60 retirements
 * hold). Without it, a corrupted unused entry is ignored, harmlessly. */
#ifndef BB_RJT_QUEUE_TAIL_CHECK
#define BB_RJT_QUEUE_TAIL_CHECK 1
#endif
/* Entries in use, bounded even for a malformed local count; a count above
 * capacity, or one that differs from the lease, is refused by bb_rjt_valid. */
static unsigned bb_rjt_queue_used(const bb_rsp_state *s){
 unsigned n=s->queue.count;return n>BB_DELAYED_CAPACITY?BB_DELAYED_CAPACITY:n;
}
static uint32_t bb_rjt_next_hash(uint32_t h,const bb_rsp_state *s){
 h=bb_rjt_segment(h,(const unsigned char *)s+BB_RJT_NEXT_TAIL,(uint32_t)(sizeof *s-BB_RJT_NEXT_TAIL));
 return bb_rjt_segment(h,s->queue.items,bb_rjt_queue_used(s)*(uint32_t)sizeof s->queue.items[0]);
}
/* Copy a player state: the tail and the entries in use. Entries [used,clear)
 * of the destination are then zeroed (bounded by capacity). Validation
 * precedes every call that writes a live destination. */
static void bb_rjt_next_copy(bb_rsp_state *dst,const bb_rsp_state *src,unsigned clear){
 const unsigned used=bb_rjt_queue_used(src);
 if(clear>BB_DELAYED_CAPACITY)clear=BB_DELAYED_CAPACITY;
 bb_c1_drum_copy((unsigned char *)dst+BB_RJT_NEXT_TAIL,(const unsigned char *)src+BB_RJT_NEXT_TAIL,
                 sizeof *dst-BB_RJT_NEXT_TAIL);
 bb_c1_drum_copy(dst->queue.items,src->queue.items,used*sizeof dst->queue.items[0]);
 if(clear>used){
  bb_rjt_word *w=(bb_rjt_word *)(void *)&dst->queue.items[used];
  for(uint32_t i=0,n=(clear-used)*(uint32_t)(sizeof dst->queue.items[0]/4u);i<n;i++)w[i]=0u;
 }
}
/* Adopt a verified job's next state into a canonical live player: the entries
 * the player used before and the job does not are zeroed, so the result is
 * byte-identical to a whole-state copy of a canonical job state. */
static void bb_rjt_next_adopt(bb_rsp_state *live,const bb_rsp_state *next){
 bb_rjt_next_copy(live,next,bb_rjt_queue_used(live));
}
#if BB_RJT_QUEUE_TAIL_CHECK
static int bb_rjt_queue_tail_zero(const bb_rsp_state *s){
 const bb_rjt_word *w=(const bb_rjt_word *)(const void *)&s->queue.items[bb_rjt_queue_used(s)];
 uint32_t any=0;
 for(uint32_t i=0,n=(BB_DELAYED_CAPACITY-bb_rjt_queue_used(s))*(uint32_t)(sizeof s->queue.items[0]/4u);i<n;i++)any|=w[i];
 return any==0u;
}
#endif
/* Opt 3: one digest for both control regions. */
static uint32_t bb_rjt_kit_control(const drum808_t *k){
 uint32_t a,b;
 (void)bb_rjt_digest(k,(uint32_t)BB_RJT_KIT_GLIDE,0u,&a);
 (void)bb_rjt_digest((const unsigned char *)k+BB_RJT_KIT_CONTROL_B,(uint32_t)(BB_RJT_KIT_STATE-BB_RJT_KIT_CONTROL_B),0u,&b);
 return bb_rjt_lane(a,b);
}
/* control: a sealed control digest to use instead of reading the job's control
 * regions (CPU0's verify), or 0 to read them (seal, CPU1). The digest used is
 * returned in *used when used is not 0. */
static uint32_t bb_rjt_hash_parts(const bb_rjt_job *j,unsigned output,const uint32_t *control,uint32_t *used){
 uint32_t h=UINT32_C(2166136261);
 h=bb_rjt_segment(h,&j->ticket,2u*sizeof j->ticket);            /* ticket, epoch */
 h=bb_rjt_next_hash(h,&j->next);
 h=bb_rjt_segment(h,&j->render.frames,2u*sizeof j->render.frames); /* frames, count */
 /* Only active hits are read by the renderer. Bound the local length even
  * for a malformed job: callers validate the plan before accepting a hash.
  * Count itself remains covered above; stale unused slots are not payload. */
 unsigned count=j->render.count;
 if(count>BB_RJ_EVENTS)count=BB_RJ_EVENTS;
 h=bb_rjt_segment(h,j->render.hits,count*(uint32_t)sizeof j->render.hits[0]);
 {const uint32_t c=control?*control:bb_rjt_kit_control(&j->render.kit);
  if(used)*used=c;
  h=bb_rjt_lane(h,c);}
 h=bb_rjt_segment(h,(const unsigned char *)&j->render.kit+BB_RJT_KIT_GLIDE,(uint32_t)(BB_RJT_KIT_CONTROL_B-BB_RJT_KIT_GLIDE));
 h=bb_rjt_segment(h,(const unsigned char *)&j->render.kit+BB_RJT_KIT_STATE,(uint32_t)(sizeof j->render.kit-BB_RJT_KIT_STATE));
 h=bb_rjt_segment(h,&j->render.groups,sizeof j->render.groups);
 /* Only frames consumed by this job are output payload. Bound even a
  * malformed local count; valid leases are checked before acceptance. */
 unsigned frames=j->render.frames;
 if(frames>BB_RJ_FRAMES)frames=BB_RJ_FRAMES;
 if(output)for(unsigned b=0;b<3;b++)
  h=bb_rjt_segment(h,j->render.bus[b],frames*(uint32_t)sizeof j->render.bus[b][0]);
 return h;
}
/* The whole-memory hash: what CPU1 computes, and what the seal starts from. */
static uint32_t bb_rjt_hash(const bb_rjt_job *j,unsigned output){return bb_rjt_hash_parts(j,output,0,0);}
/* Opt 3 copy-back: the pan glide and the voice state only. */
static void bb_rjt_kit_adopt(drum808_t *live,const drum808_t *job){
#ifdef BB_RJT_HOST_CONTROL_ASSERT
 /* Host proof that skipping control A and B changes nothing. */
 if(__builtin_memcmp(live,job,BB_RJT_KIT_GLIDE)||
    __builtin_memcmp((const unsigned char *)live+BB_RJT_KIT_CONTROL_B,(const unsigned char *)job+BB_RJT_KIT_CONTROL_B,
                     BB_RJT_KIT_STATE-BB_RJT_KIT_CONTROL_B))__builtin_trap();
#endif
 bb_c1_drum_copy((unsigned char *)live+BB_RJT_KIT_GLIDE,(const unsigned char *)job+BB_RJT_KIT_GLIDE,
                 BB_RJT_KIT_CONTROL_B-BB_RJT_KIT_GLIDE);
 bb_c1_drum_copy((unsigned char *)live+BB_RJT_KIT_STATE,(const unsigned char *)job+BB_RJT_KIT_STATE,
                 sizeof *live-BB_RJT_KIT_STATE);
}
/* Confirmed-complete private payload only. Hash and finite check use the same
 * loaded representation. All consumed samples are checked; unused capacity
 * is neither output payload nor copied into a live destination. */
static int bb_rjt_checked_output_hash(const bb_rjt_job *j,const bb_rjt_lease *lease,uint32_t *result){
 if(!j||!lease||!result||!j->render.frames||j->render.frames>BB_RJ_FRAMES)return 0;
#if BB_RJT_QUEUE_TAIL_CHECK
 if(!bb_rjt_queue_tail_zero(&j->next))return 0;
#endif
 uint32_t h=bb_rjt_hash_parts(j,0,&lease->control,0); /* opt 3: sealed control digest */
 for(unsigned b=0;b<3;b++){
  uint32_t d;
  if(!bb_rjt_digest(j->render.bus[b],j->render.frames*(uint32_t)sizeof j->render.bus[b][0],1u,&d))return 0;
  h=bb_rjt_lane(h,d);
 }
 *result=h;return 1;
}
static int bb_rjt_valid(const bb_rjt_job *j,const bb_rjt_lease *lease){
 return j&&lease&&!lease->committed&&lease->ticket&&
  j->guard==BB_RJT_GUARD&&j->end_guard==BB_RJT_GUARD&&
  j->ticket==lease->ticket&&j->epoch==lease->epoch&&
  j->next.generation==lease->generation&&j->next.queue.count==lease->queued&&
  lease->queued<=BB_DELAYED_CAPACITY&&j->render.frames==lease->frames&&
  bb_rj_plan_valid(&j->render)&&bb_group_state_valid(&j->render.groups);
}
/* Caller has prepared next/render privately; seal only after all preparation
 * succeeds. Lease remains CPU0-owned and is never supplied by the worker. */
static int bb_rjt_seal(bb_rjt_job *j,bb_rjt_lease *lease){
 if(!j||!lease)return 0;
 j->guard=j->end_guard=BB_RJT_GUARD;j->ticket=lease->ticket;j->epoch=lease->epoch;
 if(!bb_rjt_valid(j,lease))return 0;
 j->input_check=bb_rjt_hash_parts(j,0,0,&lease->control);return 1;
}
/* Worker receives a request value, never a pointer to CPU0's live lease. */
static int bb_rjt_run(bb_rjt_job *j,bb_rjt_lease request,uint32_t *result){
 if(!result||!bb_rjt_valid(j,&request)||j->input_check!=bb_rjt_hash(j,0))return 0;
 if(!bb_rj_render(&j->render))return 0;
 if((uintptr_t)&j->render.kit==bb_coefficient_trace.kit && bb_coefficient_trace.stage)return 0;
 {uint32_t control;const uint32_t h=bb_rjt_hash_parts(j,1,0,&control);
#if BB_RJT_CPU1_CONTROL_GUARD
  if(control!=request.control)return 0; /* opt 3 invariant: CPU1 never writes control A or B */
#endif
  *result=h;}
 return 1;
}
/* This coefficient is fixed by cowbell initialization; rendering changes
 * delay history, but never hp.a. Compare against the CPU0-owned live kit,
 * not against a worker-supplied checksum of potentially corrupted state. */
static int bb_rjt_fixed_coefficients_match(const drum808_t *before,const drum808_t *after){
 uint32_t a,b;
 if(!before||!after)return 0;
 __builtin_memcpy(&a,&before->cb.hp.a,sizeof a);
 __builtin_memcpy(&b,&after->cb.hp.a,sizeof b);
 return a==b && (a&UINT32_C(0x7f800000))!=UINT32_C(0x7f800000);
}
/* Fixed disjoint owner destinations only, after confirmed worker completion.
 * All validation precedes every live write; duplicate commits are refused. */
static int bb_rjt_commit(const bb_rjt_job *j,bb_rjt_lease *lease,uint32_t result,
 bb_rsp_state *state,drum808_t *kit,gg_state *groups,float bus[3][128]){
 uint32_t checked;
 if(!state||!kit||!groups||!bus||!bb_rjt_valid(j,lease)||
    state->generation!=lease->generation||!bb_rjt_fixed_coefficients_match(kit,&j->render.kit)||
    !bb_rjt_checked_output_hash(j,lease,&checked)||checked!=result)return 0;
 bb_rjt_next_adopt(state,&j->next);bb_rjt_kit_adopt(kit,&j->render.kit);
 __builtin_memcpy(groups,&j->render.groups,sizeof *groups);
 for(unsigned b=0;b<3;b++)__builtin_memcpy(bus[b],j->render.bus[b],j->render.frames*sizeof bus[b][0]);
 lease->committed=1;return 1;
}
#endif
