#include "bb_echo.h"
#include <string.h>
static float magnitude(float x) { return x<0?-x:x; }
static uint32_t motion_random(bb_echo *e) {
    uint32_t x=e->motion_rng;
    x^=x<<13;x^=x>>17;x^=x<<5;e->motion_rng=x;return x;
}
static void motion_targets(bb_echo *e) {
    for(unsigned h=0;h<3;h++) {
        float unit=(float)(motion_random(e)&65535u)*(1.0f/65535.0f);
        /* Disjoint ranges keep all three heads ordered and inside the tape:
         * thirds +/-0.1, and longest0.85..1.0 of the selected echo time. */
        e->head_target[h]=h==2?unit*0.15f-0.15f:unit*0.2f-0.1f;
        unit=(float)(motion_random(e)&65535u)*(1.0f/65535.0f);
        e->pan_target[h]=unit*0.3f-0.15f;
    }
    e->motion_remaining=e->sample_rate*(20u+motion_random(e)%41u);
    if(e->motion_changes!=UINT32_MAX)e->motion_changes++;
}
int bb_echo_motion_set(bb_echo *e,unsigned depth) {
    if(!e || !e->sample_rate || depth>100)return 0;
    e->motion_target=(float)depth*0.01f;return 1;
}
static float read_head(const bb_echo *e,float delay) {
    unsigned age=(unsigned)delay;
    float fraction=delay-(float)age;
    unsigned near=(e->write-age)&(BB_ECHO_RING-1u);
    unsigned far=(near-1u)&(BB_ECHO_RING-1u);
    return e->tape[near]+(e->tape[far]-e->tape[near])*fraction;
}
int bb_echo_set(bb_echo *e,unsigned ms,unsigned feedback,unsigned tone,
                unsigned drive,unsigned wear,unsigned wet,unsigned heads) {
    float delay,cutoff;
    if(!e || !e->sample_rate || ms<10 || ms>700 || feedback>90 ||
       tone>100 || drive>100 || wear>100 || wet>100 || heads>7)return 0;
    delay=(float)(e->sample_rate*ms)/1000.0f;
    /* Leave room for the maximum wow excursion and interpolation tap. */
    if(delay>(float)(BB_ECHO_RING-128u))delay=(float)(BB_ECHO_RING-128u);
    e->target_delay=delay;
    e->feedback=(float)feedback*0.01f;
    e->drive=(float)drive*0.04f;
    e->wear=(float)wear*0.00002f;
    e->wet=(float)wet*0.01f;e->heads=(uint8_t)heads;
    cutoff=700.0f+(float)tone*73.0f;
    e->low_coeff=1.0f/(1.0f+(float)e->sample_rate/(6.283185307f*cutoff));
    return 1;
}
int bb_echo_init(bb_echo *e,unsigned sample_rate) {
    if(!e || sample_rate<8000 || sample_rate>96000)return 0;
    memset(e,0,sizeof *e);e->sample_rate=sample_rate;
    e->hp_pole=1.0f/(1.0f+6.283185307f*180.0f/(float)sample_rate);
    e->motion_slew=1.0f/(8.0f*(float)sample_rate);e->motion_rng=0x42424543u;
    if(!bb_echo_set(e,360,55,50,25,0,50,7))return 0;
    e->delay=e->target_delay;
    return 1;
}
int bb_echo_process(bb_echo *restrict e,const float *restrict send,
                    float *restrict left,float *restrict right,unsigned frames) {
    unsigned i;
    if(!e || !e->sample_rate || !send || !left || !right || send==left ||
       send==right || left==right || !frames || frames>128)return 0;
    /* Controls are applied between calls. Hoist only invariant expressions,
     * with the original operation order/rounding; do not batch recurrences. */
    const float phase_step=0.63f/(float)e->sample_rate;
    const float hp_pole=e->hp_pole,low_coeff=e->low_coeff;
    const float feedback=e->feedback,drive=e->drive,wear=e->wear;
    const float wet_gain=e->wet*(2.0f/3.0f);
    for(i=0;i<frames;i++) {
        float in=send[i],tap[3],tri,record,l=0,r=0;
        unsigned h;
        if(!(in>=-64.0f && in<=64.0f)) {
            in=0;if(e->invalid_frames!=UINT32_MAX)e->invalid_frames++;
        }
        e->delay+=(e->target_delay-e->delay)*0.001f;
        e->phase+=phase_step;
        if(e->phase>=1.0f)e->phase-=1.0f;
        tri=e->phase<0.5f?4.0f*e->phase-1.0f:3.0f-4.0f*e->phase;
        e->wow+=(tri-e->wow)*0.002f;
        if(e->motion_target || e->motion) {
            e->motion+=(e->motion_target-e->motion)*0.0005f;
            if(!e->motion_target && e->motion<1e-7f)e->motion=0;
            if(e->motion) {
                if(!e->motion_remaining)motion_targets(e);
                e->motion_remaining--;
                for(h=0;h<3;h++) {
                    e->head_offset[h]+=(e->head_target[h]-e->head_offset[h])*e->motion_slew;
                    e->pan_offset[h]+=(e->pan_target[h]-e->pan_offset[h])*e->motion_slew;
                }
            }
        }
        /* Init owns zeroing the tape. Before its first excitation there
         * are no samples or filter tails to read. Keep all motion/delay
         * recurrences and tape position exact; never bypass an excited tape. */
        if(!e->excited) {
            if(in==0 && e->hp_x==0 && e->hp_y==0 && e->low==0) {
                e->write=(e->write+1u)&(BB_ECHO_RING-1u);
                left[i]=right[i]=0;
                continue;
            }
            e->excited=1;
        }
        for(h=0;h<3;h++) {
            float delay=e->delay*(float)(h+1u)/3.0f;
            if(e->motion)delay+=e->delay*e->motion*e->head_offset[h];
            tap[h]=read_head(e,delay*(1.0f+wear*e->wow));
        }
        record=in+tap[0]*feedback;
        e->hp_y=hp_pole*(e->hp_y+record-e->hp_x);e->hp_x=record;
        e->low+=(e->hp_y-e->low)*low_coeff;
        /* Bounded soft recording curve. At zero drive preserve coloration
         * without pretending that the low/high-pass path is bypassed. */
        record=e->low/(1.0f+drive*magnitude(e->low));
        e->tape[e->write]=record;
        e->write=(e->write+1u)&(BB_ECHO_RING-1u);
        for(h=0;h<3;h++)if(e->heads&(1u<<h)) {
            static const float pan_l[3]={0.75f,0.5f,0.25f};
            float pan=pan_l[h];
            if(e->motion)pan+=e->motion*e->pan_offset[h];
            l+=tap[h]*pan;r+=tap[h]*(1.0f-pan);
        }
        left[i]=l*wet_gain;right[i]=r*wet_gain;
    }
    return 1;
}
