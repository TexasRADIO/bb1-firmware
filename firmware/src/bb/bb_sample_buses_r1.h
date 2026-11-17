/* SPDX-License-Identifier: GPL-3.0-only
 * Offline sample backend bus adapter using retained Big Beat group ramps.
 * Default owns output spans. Explicit mode supports adding retained tails to
 * an existing synth span; caller supplies the same pre-span group state.
 * No allocation. Group progression occurs exactly once in either mode.
 */
#ifndef BB_SAMPLE_BUSES_R1_H
#define BB_SAMPLE_BUSES_R1_H
#include "bb_sample_kit_r1.h"
#include "group_gain.h"
static unsigned bb_sample_group(unsigned note) {
    if(note==35||note==36)return GG_KICK;
    if(note==38||note==39||note==40)return GG_SNARE_CLAP;
    if(note==42||note==44||note==46)return GG_HATS;
    return GG_PERCUSSION;
}
static int bb_sample_separate(const void*a,size_t an,const void*b,size_t bn) {
    uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;
    return x<=y?y-x>=an:x-y>=bn;
}
static int bb_sample_buses_mode(bb_sample_slot *slots,unsigned count,gg_state *groups,
                           float gain,unsigned echo,unsigned spring,
                           float *dry,float *delay,float *reverb,unsigned frames,int add) {
    if(!slots||!groups||!dry||!delay||!reverb||!count||count>BB_SK_MAX_SLOTS||
       !frames||frames>128||echo>127||spring>127||!(gain>=0.0f&&gain<=1.0f))return BB_SK_INVALID;
    float *buses[3]={dry,delay,reverb};
    if(!bb_sample_separate(slots,count*sizeof *slots,groups,sizeof *groups))return BB_SK_INVALID;
    for(unsigned i=0;i<3;i++) {
        if(!bb_sample_separate(buses[i],frames*sizeof(float),slots,count*sizeof *slots)||
           !bb_sample_separate(buses[i],frames*sizeof(float),groups,sizeof *groups))return BB_SK_INVALID;
        for(unsigned j=0;j<i;j++)if(!bb_sample_separate(buses[i],frames*sizeof(float),buses[j],frames*sizeof(float)))return BB_SK_INVALID;
    }
    /* The voice pointer, velocity and note->group assignment cannot change
     * inside an owned span. Prepare them once; retain ascending voice order.
     * Fixed local storage only. Native stack impact must be verified. */
    struct bb_sample_context {
        bb_sample_voice *voice;
        uint8_t velocity,group;
    } active_slots[BB_SK_MAX_SLOTS];
    unsigned active=0;
    for(unsigned i=0;i<count;i++){
        if(slots[i].velocity>127||slots[i].note>127)return BB_SK_INVALID;
        if(slots[i].voice.active){
            /* A voice is normally created by bb_sample_start(), but a stale
             * restored/overwritten state must fail before the first table or
             * sample-byte access. This runs once per span, not per sample. */
            const bb_sample_voice *v=&slots[i].voice;
            if(!v->data||!v->frames||v->frames>162816u||v->pos>v->frames||
               !v->rate||v->rate>262144u||v->fraction>=65536u||
               v->index<0||v->index>88||v->predictor< -32768||v->predictor>32767||
               v->previous< -32768||v->previous>32767||
               v->current< -32768||v->current>32767)return BB_SK_INVALID;
            active_slots[active].voice=&slots[i].voice;
            active_slots[active].velocity=slots[i].velocity;
            active_slots[active].group=(uint8_t)bb_sample_group(slots[i].note);
            active++;
        }
    }
    for(unsigned i=0;i<GG_COUNT;i++) {
        const gg_fader *f=&groups->fader[i];
        if(!(f->current>=0.0f&&f->current<=1.0f&&f->target>=0.0f&&f->target<=1.0f&&
             f->increment>=-1.0f&&f->increment<=1.0f)||f->remaining>GG_RAMP_FRAMES)return BB_SK_INVALID;
    }
    float echo_gain=(float)echo/127.0f,spring_gain=(float)spring/127.0f;
    /* An idle bank has no decoder state to advance. Preserve ramp endpoints,
     * float operation order/signed zero and additive synth buffers exactly,
     * without 16 empty voice walks for every output frame. */
    if(!active){
        gg_advance(groups,frames);
        float value=0.0f*gain;
        for(unsigned frame=0;frame<frames;frame++){
            if(add){dry[frame]+=value;delay[frame]+=value*echo_gain;reverb[frame]+=value*spring_gain;}
            else{dry[frame]=value;delay[frame]=value*echo_gain;reverb[frame]=value*spring_gain;}
        }
        return BB_SK_OK;
    }
    /* No control can change a fader inside this owned event span. Settled
     * groups need neither repeated state walks nor cross-object gg_frame
     * calls. A moving group retains every original per-frame update. */
    float gains[GG_COUNT];unsigned moving=0;
    for(unsigned group=0;group<GG_COUNT;group++){
        gains[group]=groups->fader[group].current;
        moving|=groups->fader[group].remaining;
    }
    for(unsigned frame=0;frame<frames;frame++) {
        float sum=0.0f;if(moving)gg_frame(groups,gains);
        /* No voice can start inside an owned event span. Retain ascending
         * slot order so the floating sum and decoder state stay exact. Voices
         * ending mid-span remain listed: their final interpolated frame must
         * still contribute before later ticks naturally return zero. */
        for(unsigned n=0;n<active;n++) {
            const struct bb_sample_context *context=&active_slots[n];
            int32_t tick=bb_sample_tick(context->voice);
            float value=(float)(tick*context->velocity/127)*(1.0f/32768.0f);
            /* Same unity branch and product order as public gg_voice(),
             * without an external function call for every active sample. */
            float group_gain=gains[context->group];
            sum+=group_gain==1.0f?value:value*group_gain;
        }
        float value=sum*gain;
        if(add){dry[frame]+=value;delay[frame]+=value*echo_gain;reverb[frame]+=value*spring_gain;}
        else{dry[frame]=value;delay[frame]=value*echo_gain;reverb[frame]=value*spring_gain;}
    }
    return BB_SK_OK;
}
static int bb_sample_buses(bb_sample_slot *slots,unsigned count,gg_state *groups,
                           float gain,unsigned echo,unsigned spring,
                           float *dry,float *delay,float *reverb,unsigned frames) {
    return bb_sample_buses_mode(slots,count,groups,gain,echo,spring,dry,delay,reverb,frames,0);
}
#endif
