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
typedef struct {uint32_t ticket,epoch,frames,generation,committed;} bb_rjt_lease;
/* Lane-word transaction integrity (timing candidate, 2026-10-27).
 * Version bump: a job sealed by the previous byte-FNV protocol never validates.
 * Both cores execute the same image; no saved file, asset or wire format uses
 * this private check. */
#define BB_RJT_GUARD UINT32_C(0xD8110A74)
_Static_assert(sizeof(bb_rjt_job)<=sizeof(bb_c1_drum_job),"Review retained pool capacity");
/* Coverage is byte-for-byte the same as the previous protocol: ticket, epoch,
 * next state (including its padding bytes), frames, count, active hits, whole
 * kit (including padding), groups and, for results, every consumed output
 * sample. Every covered field is a 4-byte aligned whole number of words, so
 * the hash reads native aligned words; no byte assembly, no memcpy call. */
typedef uint32_t bb_rjt_word __attribute__((__may_alias__));
#define BB_RJT_ALIGNED(T,f) (offsetof(T,f)%4u==0u)
_Static_assert(__alignof__(bb_rjt_job)>=4u,"word-aligned job");
_Static_assert(BB_RJT_ALIGNED(bb_rjt_job,ticket)&&offsetof(bb_rjt_job,epoch)==offsetof(bb_rjt_job,ticket)+4u,"ticket+epoch words");
_Static_assert(BB_RJT_ALIGNED(bb_rjt_job,next)&&sizeof(bb_rsp_state)%4u==0u,"next state words");
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
static uint32_t bb_rjt_hash(const bb_rjt_job *j,unsigned output){
 uint32_t h=UINT32_C(2166136261);
 h=bb_rjt_segment(h,&j->ticket,2u*sizeof j->ticket);            /* ticket, epoch */
 h=bb_rjt_segment(h,&j->next,sizeof j->next);
 h=bb_rjt_segment(h,&j->render.frames,2u*sizeof j->render.frames); /* frames, count */
 /* Only active hits are read by the renderer. Bound the local length even
  * for a malformed job: callers validate the plan before accepting a hash.
  * Count itself remains covered above; stale unused slots are not payload. */
 unsigned count=j->render.count;
 if(count>BB_RJ_EVENTS)count=BB_RJ_EVENTS;
 h=bb_rjt_segment(h,j->render.hits,count*(uint32_t)sizeof j->render.hits[0]);
 h=bb_rjt_segment(h,&j->render.kit,sizeof j->render.kit);
 h=bb_rjt_segment(h,&j->render.groups,sizeof j->render.groups);
 /* Only frames consumed by this job are output payload. Bound even a
  * malformed local count; valid leases are checked before acceptance. */
 unsigned frames=j->render.frames;
 if(frames>BB_RJ_FRAMES)frames=BB_RJ_FRAMES;
 if(output)for(unsigned b=0;b<3;b++)
  h=bb_rjt_segment(h,j->render.bus[b],frames*(uint32_t)sizeof j->render.bus[b][0]);
 return h;
}
/* Confirmed-complete private payload only. Hash and finite check use the same
 * loaded representation. All consumed samples are checked; unused capacity
 * is neither output payload nor copied into a live destination. */
static int bb_rjt_checked_output_hash(const bb_rjt_job *j,uint32_t *result){
 if(!j||!result||!j->render.frames||j->render.frames>BB_RJ_FRAMES)return 0;
 uint32_t h=bb_rjt_hash(j,0);
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
  j->next.generation==lease->generation&&j->render.frames==lease->frames&&
  bb_rj_plan_valid(&j->render)&&bb_group_state_valid(&j->render.groups);
}
/* Caller has prepared next/render privately; seal only after all preparation
 * succeeds. Lease remains CPU0-owned and is never supplied by the worker. */
static int bb_rjt_seal(bb_rjt_job *j,const bb_rjt_lease *lease){
 if(!j||!lease)return 0;
 j->guard=j->end_guard=BB_RJT_GUARD;j->ticket=lease->ticket;j->epoch=lease->epoch;
 if(!bb_rjt_valid(j,lease))return 0;
 j->input_check=bb_rjt_hash(j,0);return 1;
}
/* Worker receives a request value, never a pointer to CPU0's live lease. */
static int bb_rjt_run(bb_rjt_job *j,bb_rjt_lease request,uint32_t *result){
 if(!result||!bb_rjt_valid(j,&request)||j->input_check!=bb_rjt_hash(j,0))return 0;
 if(!bb_rj_render(&j->render))return 0;
 if((uintptr_t)&j->render.kit==bb_coefficient_trace.kit && bb_coefficient_trace.stage)return 0;
 *result=bb_rjt_hash(j,1);return 1;
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
    !bb_rjt_checked_output_hash(j,&checked)||checked!=result)return 0;
 __builtin_memcpy(state,&j->next,sizeof *state);__builtin_memcpy(kit,&j->render.kit,sizeof *kit);
 __builtin_memcpy(groups,&j->render.groups,sizeof *groups);
 for(unsigned b=0;b<3;b++)__builtin_memcpy(bus[b],j->render.bus[b],j->render.frames*sizeof bus[b][0]);
 lease->committed=1;return 1;
}
#endif
