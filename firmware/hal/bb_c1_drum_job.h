/* SPDX-License-Identifier: GPL-3.0-only */
/* Experimental private drum transaction. Caller owns live engines; a worker
 * owns only this snapshot until checked completion. No hardware, function
 * pointers, allocation, output routing or access to the live audio object.
 * Publication/wait/hold barriers are the transport's responsibility. These
 * functions must NEVER race with an in-flight worker. Hashing is a diagnostic
 * corruption check, not authentication or proof against all memory faults. */
#ifndef BB_C1_DRUM_JOB_H
#define BB_C1_DRUM_JOB_H
#include <stdint.h>
#include <stddef.h>
#include "drum808.h"
#include "group_gain.h"
/* Optional diagnostic observer. Production default compiles to no operation;
 * an explicitly selected target diagnostic may map this to the existing
 * worker trace word. It must not publish completion or touch the payload. */
#ifndef BB_C1_DRUM_PHASE
#define BB_C1_DRUM_PHASE(stage) ((void)(stage))
#endif
enum { BB_C1_DPH_INPUT=0x20,BB_C1_DPH_GROUPS,BB_C1_DPH_CLEAR,
       BB_C1_DPH_RENDER,BB_C1_DPH_OUTPUT,BB_C1_DPH_RETURN };
#if !defined(BB_GROUP_JOB_HOST_TEST) && !defined(BB_GROUP_TARGET_CANDIDATE)
#error "Group job extension is host-only pending HAL and pool integration"
#endif
extern void bb_proposal_group_span(drum808_t *,gg_state *,float *,float *,float *,int);
static int bb_group_state_valid(const gg_state *s) {
    if(!s)return 0;
    for(unsigned g=0;g<GG_COUNT;g++){
        const gg_fader *f=&s->fader[g];
        /* Positive range predicates also reject NaN/infinity. */
        if(f->level>127 || f->muted>1 || f->remaining>GG_RAMP_FRAMES
           || !(f->current>=0.0f && f->current<=1.0f)
           || !(f->increment>=-1.0f/GG_RAMP_FRAMES && f->increment<=1.0f/GG_RAMP_FRAMES)
           || f->target!=(f->muted?0.0f:(float)f->level/127.0f)
           || (!f->remaining && f->current!=f->target))return 0;
    }
    return 1;
}
#define BB_C1_DRUM_KITS 2u
#define BB_C1_DRUM_FRAMES 128u
#define BB_C1_DRUM_GUARD UINT32_C(0xD8C10A71)
extern void bb_drum808_render_span(drum808_t *,float *,float *,float *,int);
typedef struct {
    uint32_t guard,ticket,kits,frames,input_check;
    drum808_t kit[BB_C1_DRUM_KITS];
    float bus[BB_C1_DRUM_KITS][3][BB_C1_DRUM_FRAMES];
    gg_state groups[BB_C1_DRUM_KITS];
    uint32_t end_guard;
} bb_c1_drum_job;
/* Placement experiment only: preserve byte FNV1a order and full coverage.
 * Target copies .ram_text through the existing startup path. Host sanitizer
 * tests exercise the same fixed-call wrapper without a Mach-O section alias.
 * No additional buffers, reduced validation, arithmetic or deadline changes. */
#if defined(__APPLE__)
#define BB_C1_HASH_LOCATION __attribute__((noinline))
#else
#define BB_C1_HASH_LOCATION __attribute__((noinline,section(".ram_text.bb_c1_hash")))
#endif
static uint32_t BB_C1_HASH_LOCATION bb_c1_drum_hash_ram(uint32_t h,const void *data,size_t n) {
    const unsigned char *p=data;
    while(n>=16u) {
        h=(h^p[0])*UINT32_C(16777619);
        h=(h^p[1])*UINT32_C(16777619);
        h=(h^p[2])*UINT32_C(16777619);
        h=(h^p[3])*UINT32_C(16777619);
        h=(h^p[4])*UINT32_C(16777619);
        h=(h^p[5])*UINT32_C(16777619);
        h=(h^p[6])*UINT32_C(16777619);
        h=(h^p[7])*UINT32_C(16777619);
        h=(h^p[8])*UINT32_C(16777619);
        h=(h^p[9])*UINT32_C(16777619);
        h=(h^p[10])*UINT32_C(16777619);
        h=(h^p[11])*UINT32_C(16777619);
        h=(h^p[12])*UINT32_C(16777619);
        h=(h^p[13])*UINT32_C(16777619);
        h=(h^p[14])*UINT32_C(16777619);
        h=(h^p[15])*UINT32_C(16777619);
        p+=16;n-=16;
    }
    while(n>=4u) {
        h=(h^p[0])*UINT32_C(16777619);h=(h^p[1])*UINT32_C(16777619);
        h=(h^p[2])*UINT32_C(16777619);h=(h^p[3])*UINT32_C(16777619);
        p+=4;n-=4;
    }
    while(n--)h=(h^*p++)*UINT32_C(16777619);
    return h;
}
/* RAM/XIP exceeds the direct-call range. As in the existing worker entry,
 * use exactly one checked, fixed target; no mailbox/user-controlled address.
 * If its stack copy is corrupt, compute the original byte hash locally rather
 * than executing an unchecked target or returning a pretend checksum. */
static uint32_t bb_c1_drum_hash(uint32_t h,const void *data,size_t n) {
    uint32_t (*volatile invoke)(uint32_t,const void *,size_t)=bb_c1_drum_hash_ram;
    uint32_t (*target)(uint32_t,const void *,size_t)=invoke;
    if(target!=bb_c1_drum_hash_ram) {
        const unsigned char *p=data;
        while(n--)h=(h^*p++)*UINT32_C(16777619);
        return h;
    }
    __asm__ volatile("" : "+r"(target));
    return target(h,data,n);
}
#undef BB_C1_HASH_LOCATION
/* One already-loaded binary32 representation, same in-memory byte order.
 * Big endian remains exact, rather than guessing a fixed host byte order. */
static inline uint32_t bb_c1_drum_hash_word(uint32_t h,uint32_t word) {
#if !defined(__BYTE_ORDER__) || !defined(__ORDER_LITTLE_ENDIAN__) || !defined(__ORDER_BIG_ENDIAN__)
#error "Compiler must declare explicit memory byte order"
#endif
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    h=(h^(word&255u))*UINT32_C(16777619);
    h=(h^((word>>8)&255u))*UINT32_C(16777619);
    h=(h^((word>>16)&255u))*UINT32_C(16777619);
    h=(h^(word>>24))*UINT32_C(16777619);
#elif __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
    h=(h^(word>>24))*UINT32_C(16777619);
    h=(h^((word>>16)&255u))*UINT32_C(16777619);
    h=(h^((word>>8)&255u))*UINT32_C(16777619);
    h=(h^(word&255u))*UINT32_C(16777619);
#else
#error "Explicit memory byte order required for exact sample hash"
#endif
    return h;
}
/* Raw object representations, never FP values. Alias-qualified words require
 * both pointers aligned; otherwise use the original byte path. No overread,
 * padding rewrite, speculative access or overlap assumption beyond memcpy's
 * original contract. Exact self-copy is supported for the boot reference. */
typedef uint32_t bb_c1_copy_word __attribute__((__may_alias__));
static void bb_c1_drum_copy(void *destination,const void *source,size_t n) {
    if(!n || destination==source)return;
    unsigned char *d=destination;const unsigned char *s=source;
    if((((uintptr_t)d|(uintptr_t)s)&3u)==0u) {
        bb_c1_copy_word *dw=(bb_c1_copy_word *)d;
        const bb_c1_copy_word *sw=(const bb_c1_copy_word *)s;
        while(n>=16u) {dw[0]=sw[0];dw[1]=sw[1];dw[2]=sw[2];dw[3]=sw[3];dw+=4;sw+=4;n-=16;}
        while(n>=4u){*dw++=*sw++;n-=4;}
        d=(unsigned char *)dw;s=(const unsigned char *)sw;
    }
    while(n--)*d++=*s++;
}
static int bb_c1_drum_valid(const bb_c1_drum_job *j,uint32_t ticket) {
    return j && ticket && j->ticket==ticket && j->guard==BB_C1_DRUM_GUARD
        && j->end_guard==BB_C1_DRUM_GUARD && j->kits && j->kits<=BB_C1_DRUM_KITS
        && j->frames && j->frames<=BB_C1_DRUM_FRAMES;
}
static uint32_t bb_c1_drum_state_check(const bb_c1_drum_job *j) {
    uint32_t h=UINT32_C(2166136261);
    h=bb_c1_drum_hash(h,&j->ticket,sizeof j->ticket);
    h=bb_c1_drum_hash(h,&j->kits,sizeof j->kits);
    h=bb_c1_drum_hash(h,&j->frames,sizeof j->frames);
    for(unsigned k=0;k<j->kits;k++){
        h=bb_c1_drum_hash(h,&j->kit[k],sizeof j->kit[k]);
        h=bb_c1_drum_hash(h,&j->groups[k],sizeof j->groups[k]);
    }
    return h;
}
static uint32_t bb_c1_drum_output_check(const bb_c1_drum_job *j) {
    uint32_t h=bb_c1_drum_state_check(j);
    for(unsigned k=0;k<j->kits;k++)for(unsigned b=0;b<3;b++)
        h=bb_c1_drum_hash(h,j->bus[k][b],j->frames*sizeof(float));
    return h;
}
/* CPU0, only when transport proves no outstanding job. */
static int bb_c1_drum_prepare(bb_c1_drum_job *j,const drum808_t *live,const gg_state *groups,
                              unsigned kits,unsigned frames,uint32_t ticket) {
    if(!j || !live || !groups || !ticket || !kits || kits>BB_C1_DRUM_KITS
       || !frames || frames>BB_C1_DRUM_FRAMES)return 0;
    for(unsigned k=0;k<kits;k++)if(!bb_group_state_valid(&groups[k]))return 0;
    j->guard=j->end_guard=BB_C1_DRUM_GUARD;j->ticket=ticket;j->kits=kits;j->frames=frames;
    for(unsigned k=0;k<kits;k++){
        bb_c1_drum_copy(&j->kit[k],&live[k],sizeof j->kit[k]);
        bb_c1_drum_copy(&j->groups[k],&groups[k],sizeof j->groups[k]);
    }
    j->input_check=bb_c1_drum_state_check(j);return 1;
}
/* Worker, only on its fixed private object, once per accepted ticket.
 * Even partial execution changes no live state. Completion goes in a separate
 * single-writer mailbox, not this payload. */
static int bb_c1_drum_run(bb_c1_drum_job *j,uint32_t ticket,uint32_t *result) {
    BB_C1_DRUM_PHASE(BB_C1_DPH_INPUT);
    if(!result || !bb_c1_drum_valid(j,ticket)
       || j->input_check!=bb_c1_drum_state_check(j))return 0;
    BB_C1_DRUM_PHASE(BB_C1_DPH_GROUPS);
    for(unsigned k=0;k<j->kits;k++)if(!bb_group_state_valid(&j->groups[k]))return 0;
    for(unsigned k=0;k<j->kits;k++) {
        BB_C1_DRUM_PHASE(BB_C1_DPH_CLEAR|(k<<8));
        for(unsigned b=0;b<3;b++)for(unsigned i=0;i<j->frames;i++)j->bus[k][b][i]=0;
        BB_C1_DRUM_PHASE(BB_C1_DPH_RENDER|(k<<8));
        bb_proposal_group_span(&j->kit[k],&j->groups[k],j->bus[k][0],j->bus[k][1],j->bus[k][2],(int)j->frames);
    }
    BB_C1_DRUM_PHASE(BB_C1_DPH_OUTPUT);
    *result=bb_c1_drum_output_check(j);
    BB_C1_DRUM_PHASE(BB_C1_DPH_RETURN);
    return 1;
}
/* CPU0 after mailbox completion OR confirmed worker hold. Validate everything
 * before copying any state. On failure there are no live engine/output writes.
 * A timeout does NOT authorize calling this while the worker may still run. */
static int bb_c1_drum_commit(const bb_c1_drum_job *j,uint32_t ticket,
                             unsigned kits,unsigned frames,uint32_t result,
                             drum808_t *live,gg_state *groups,float out[BB_C1_DRUM_KITS][3][BB_C1_DRUM_FRAMES]) {
    if(!live || !groups || !out || !bb_c1_drum_valid(j,ticket) || j->kits!=kits || j->frames!=frames
       || bb_c1_drum_output_check(j)!=result)return 0;
    /* All validation precedes every live engine, fader and bus write. */
    for(unsigned k=0;k<kits;k++){
        if(!bb_group_state_valid(&j->groups[k]))return 0;
        for(unsigned b=0;b<3;b++)for(unsigned i=0;i<frames;i++){
            uint32_t bits;__builtin_memcpy(&bits,&j->bus[k][b][i],sizeof bits);
            if((bits&UINT32_C(0x7f800000))==UINT32_C(0x7f800000))return 0;
        }
    }
    for(unsigned k=0;k<kits;k++) {
        bb_c1_drum_copy(&groups[k],&j->groups[k],sizeof groups[k]);
        bb_c1_drum_copy(&live[k],&j->kit[k],sizeof live[k]);
        for(unsigned b=0;b<3;b++)for(unsigned i=0;i<frames;i++)out[k][b][i]=j->bus[k][b][i];
    }
    return 1;
}
/* A bounded float calculation for a future hardware job. All operands depend
 * on the request; compare returned float bits with the CPU0 evaluation using
 * identical flags. No FMA or fast-math is authorized by this experiment. */
static uint32_t bb_c1_float_calculate(uint32_t nonce) {
    float x=(float)(nonce&1023u)*(1.0f/1024.0f)-0.5f;
    float y=0.125f;
    for(unsigned i=0;i<32;i++){x=x*0.984375f+y*0.0625f;y=y*0.96875f-x*0.03125f;}
    union {float f;uint32_t u;} bits;bits.f=x+y;return bits.u;
}
#endif
