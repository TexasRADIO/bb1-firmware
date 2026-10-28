/* SPDX-License-Identifier: GPL-3.0-only */
/* Offline prototype. NOT wired to the FM-1 mailbox. Caller must supply a
 * private kit/group snapshot and CPU0-resolved native808 hits. No live audio
 * pointer, shared sequencer state, allocation or function-pointer payload. */
#ifndef BB_RICH_JOB_PROTOTYPE_H
#define BB_RICH_JOB_PROTOTYPE_H
#include <stdint.h>
#if !defined(BB_GROUP_TARGET_CANDIDATE)
#include <string.h>
#endif
#include "bb_source_trigger.h"
#include "bb_phrase.h"
#include "group_gain.h"
enum {BB_RJ_FRAMES=128,BB_RJ_EVENTS=192};
typedef struct {uint16_t offset;uint8_t voice,reserved;float velocity;} bb_rj_hit;
typedef struct {
    uint32_t frames,count;
    bb_rj_hit hits[BB_RJ_EVENTS];
    drum808_t kit;
    gg_state groups;
    float bus[3][BB_RJ_FRAMES];
} bb_rj_job;
extern void bb_proposal_group_span(drum808_t *,gg_state *,float *,float *,float *,int);
static int bb_rj_plan_valid(const bb_rj_job *job) {
    if(!job||!job->frames||job->frames>BB_RJ_FRAMES||job->count>BB_RJ_EVENTS)return 0;
    for(unsigned i=0;i<job->count;i++) {
        const bb_rj_hit *hit=&job->hits[i];
        if(hit->offset>=job->frames || (i&&hit->offset<job->hits[i-1].offset) || hit->reserved ||
           !(hit->voice<16||hit->voice==BB_PHRASE_SHAKER) ||
           !(hit->velocity>0.0f&&hit->velocity<=1.0f))return 0;
    }
    return 1;
}
static int bb_rj_render(bb_rj_job *job) {
    /* Whole plan checked BEFORE touching private DSP/output. Structural
     * context/checksum validation belongs to the future mailbox contract. */
    if(!bb_rj_plan_valid(job))return 0;
    /* Only the validated active span is accumulated, hashed and committed.
     * Leave unused capacity untouched; short event spans must not clear a
     * full half-block three times. Every active sample still starts at zero. */
    for(unsigned b=0;b<3;b++)
        __builtin_memset(job->bus[b],0,job->frames*sizeof job->bus[b][0]);
    unsigned frame=0,next=0;
    while(frame<job->frames) {
        while(next<job->count && job->hits[next].offset==frame) {
            const bb_rj_hit *hit=&job->hits[next++];
            if(!bb_drum808_source_trigger(&job->kit,hit->voice,hit->velocity))return 0;
        }
        unsigned end=next<job->count?job->hits[next].offset:job->frames;
        bb_proposal_group_span(&job->kit,&job->groups,job->bus[0]+frame,
                              job->bus[1]+frame,job->bus[2]+frame,(int)(end-frame));
        frame=end;
    }
    return 1;
}
#endif
