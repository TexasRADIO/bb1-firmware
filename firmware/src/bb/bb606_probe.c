/* SPDX-License-Identifier: GPL-3.0-only */
#include "bb606_probe.h"
#include <string.h>
static uint32_t hash(uint32_t h,const void *data,unsigned bytes) {
    const unsigned char *p=data;
    while(bytes--)h=(h^*p++)*UINT32_C(16777619);
    return h;
}
static int valid(const bb606_probe_job *j,uint32_t ticket) {
    return j && ticket && j->ticket==ticket && j->guard==BB606_PROBE_GUARD
        && j->end_guard==BB606_PROBE_GUARD && j->voices && j->voices<128u
        && j->frames && j->frames<=BB606_PROBE_FRAMES && j->velocity<=127u;
}
static uint32_t input(const bb606_probe_job *j) {
    uint32_t h=hash(UINT32_C(2166136261),&j->ticket,4);
    h=hash(h,&j->voices,4);h=hash(h,&j->frames,4);h=hash(h,&j->velocity,4);
    return hash(h,j->state,BB606_PROBE_STATE_BYTES);
}
static uint32_t output(const bb606_probe_job *j) {
    uint32_t h=input(j);
    for(unsigned b=0;b<3;b++)h=hash(h,j->bus[b],j->frames*sizeof(float));
    return h;
}
static int finite(const bb606_probe_job *j) {
    for(unsigned b=0;b<3;b++)for(unsigned i=0;i<j->frames;i++) {
        uint32_t bits;memcpy(&bits,&j->bus[b][i],sizeof bits);
        if((bits&UINT32_C(0x7f800000))==UINT32_C(0x7f800000))return 0;
    }
    return 1;
}
int bb606_probe_prepare(bb606_probe_job *j,uint32_t ticket,unsigned voices,
                       unsigned frames,unsigned velocity) {
    if(!j || !ticket || !voices || voices>=128u || !frames || frames>128u
       || velocity>127u || BB606_API(_size)()!=BB606_PROBE_STATE_BYTES
       || BB606_API(_alignment)()>4u || (uintptr_t)j%4u)return 0;
    memset(j,0,sizeof *j);
    j->guard=j->end_guard=BB606_PROBE_GUARD;j->ticket=ticket;
    j->voices=voices;j->frames=frames;j->velocity=velocity;
    if(!BB606_API(_init)(j->state,sizeof j->state,44100.f))return 0;
    for(unsigned v=0;v<BB606_VOICES;v++) {
        bb606_config c;
        if(!BB606_API(_default)(v,&c))return 0;
        c.echo=.25f;c.spring=.5f;
        if(!BB606_API(_configure)(j->state,v,&c))return 0;
    }
    j->input_check=input(j);return 1;
}
int bb606_probe_run(bb606_probe_job *j,uint32_t ticket,uint32_t *result) {
    if(!result || !valid(j,ticket) || j->finished || input(j)!=j->input_check)return 0;
    for(unsigned v=0;v<BB606_VOICES;v++)if(j->voices&(1u<<v))
        if(!BB606_API(_trigger)(j->state,v,j->velocity))return 0;
    if(!BB606_API(_render)(j->state,j->bus[0],j->bus[1],j->bus[2],j->frames)
       || !finite(j))return 0;
    *result=output(j);j->finished=1;return 1;
}
int bb606_probe_accept(const bb606_probe_job *j,uint32_t ticket,uint32_t result) {
    return valid(j,ticket) && j->finished==1 && finite(j) && output(j)==result;
}

/* Same operations as run(), with four non-overlapping timing intervals.
 * This is boot-only instrumentation; it is never called by live audio. */
int bb606_probe_run_timed(bb606_probe_job *j,uint32_t ticket,uint32_t *result,
                         bb606_probe_timing *timing) {
    uint32_t t0,t1,t2,t3,t4;
    if(!timing)return 0;
    memset(timing,0,sizeof *timing);
    t0=bb606_probe_clock();
    if(!result || !valid(j,ticket) || j->finished || input(j)!=j->input_check)return 0;
    t1=bb606_probe_clock();
    for(unsigned v=0;v<BB606_VOICES;v++)if(j->voices&(1u<<v))
        if(!BB606_API(_trigger)(j->state,v,j->velocity))return 0;
    t2=bb606_probe_clock();
    if(!BB606_API(_render)(j->state,j->bus[0],j->bus[1],j->bus[2],j->frames))return 0;
    t3=bb606_probe_clock();
    if(!finite(j))return 0;
    *result=output(j);j->finished=1;
    t4=bb606_probe_clock();
    timing->input_ticks=t1-t0;timing->trigger_ticks=t2-t1;
    timing->render_ticks=t3-t2;timing->output_ticks=t4-t3;
    timing->valid=1;return 1;
}
