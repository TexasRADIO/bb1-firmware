/* SPDX-License-Identifier: GPL-3.0-only */
/* PRE-AUDIO diagnostic. CPU0 single-track attack cost, not whole-arrangement
 * acceptance. Shared metal-bank cost is included in each metal voice and
 * must not be added across those voices. No DSP approximation or live PLAY. */
enum {BB_VC_NOT_RUN,BB_VC_RUNNING,BB_VC_PASS,BB_VC_REFUSED,BB_VC_INVALID,BB_VC_CAP,BB_VC_RESTORE};
static struct {
    uint32_t version,status,cases,failed_case,elapsed,calibration;
    uint32_t ticks[D8S_NUM][4];
} bb_voice_cost;
static int bb_private_voice_cost_startup(void) {
    if(!bb_private_render_admitted())return 0;
    if(bb_voice_cost.status)return bb_voice_cost.status==BB_VC_PASS;
    bb_voice_cost.version=1;bb_voice_cost.status=BB_VC_RUNNING;
    const unsigned sizes[4]={26,34,64,128};
    const unsigned tracks[D8S_NUM]={D8_BD,D8_SD,D8_LT,D8_MT,D8_HT,D8_LT,D8_MT,D8_HT,
        D8_RS,D8_RS,D8_CP,D8_CP,D8_CB,D8_CH,D8_OH,D8_CY};
    unsigned char *backup=(unsigned char *)&bb_c1_phase_pool+sizeof(bb_group_runtime);
    _Static_assert(sizeof(bb_c1_phase_pool)>=sizeof(bb_group_runtime)+BB_MACHINES*sizeof(gg_state),"existing pre-audio backup capacity");
    bb_c1_drum_copy(backup,bb_sound.groups,BB_MACHINES*sizeof(gg_state));
    uint32_t begin=fm1_ticks(),t=fm1_ticks();
    bb_voice_cost.calibration=fm1_ticks()-t;
    for(unsigned sound=0;sound<D8S_NUM;sound++)for(unsigned size=0;size<4;size++) {
        drum808_t *d=&bb_sound.kit[0];unsigned track=tracks[sound],n=sizes[size];
        drum808_init(d);gg_init(&bb_sound.groups[0]);
        unsigned variant=(sound>=D8S_LC&&sound<=D8S_HC)||sound==D8S_CL||sound==D8S_MA;
        for(int p=0;p<drum808_nparams((int)track);p++) {
            const char *name=drum808_param((int)track,p)->name;
            if(bb_private_title_is(name,"Sound"))drum808_set(d,(int)track,p,(int)variant);
            if(bb_private_title_is(name,"Rev"))drum808_set(d,(int)track,p,48);
            if(bb_private_title_is(name,"Dly"))drum808_set(d,(int)track,p,64);
            if(bb_private_title_is(name,"Drive"))drum808_set(d,(int)track,p,0);
        }
        drum808_trigger(d,(int)track,1.0f);
        for(unsigned i=0;i<n;i++)bb_sound.drum_dry[i]=bb_sound.drum_rev[i]=bb_sound.drum_dly[i]=0;
        t=fm1_ticks();
        bb_proposal_group_span(d,&bb_sound.groups[0],bb_sound.drum_dry,bb_sound.drum_rev,bb_sound.drum_dly,(int)n);
        bb_voice_cost.ticks[sound][size]=fm1_ticks()-t;
        for(unsigned i=0;i<n;i++) {
            float v[3]={bb_sound.drum_dry[i],bb_sound.drum_rev[i],bb_sound.drum_dly[i]};
            for(unsigned b=0;b<3;b++)if(!(v[b]>=-64.0f&&v[b]<=64.0f)) {
                bb_voice_cost.status=BB_VC_INVALID;bb_voice_cost.failed_case=sound*4u+size+1u;goto restore;
            }
        }
        bb_voice_cost.cases++;
        /* Separate startup experiment cap. Never changes an audio/worker
         * deadline or feeds/reconfigures the watchdog. */
        if(fm1_ticks()-begin>FM1_TICKS_PER_US*1000000u){bb_voice_cost.status=BB_VC_CAP;goto restore;}
    }
    bb_voice_cost.status=BB_VC_PASS;
restore:
    bb_voice_cost.elapsed=fm1_ticks()-begin;
    if(!bb_private_restore_owner())bb_voice_cost.status=BB_VC_RESTORE;
    bb_c1_drum_copy(bb_c1_phase_pool.runtime.groups,backup,BB_MACHINES*sizeof(gg_state));
    return bb_voice_cost.status==BB_VC_PASS;
}
