/* SPDX-License-Identifier: GPL-3.0-only */
/* PRE-AUDIO ONLY. Independent native CPU0 whole-span references versus
 * retained CPU1 fragments. Existing bb_sound kit/group/sample arrays are
 * temporary scratch; target initialization restores them before any audio.
 * No allocation, physical output, transport command, retry or deadline edit.
 * Two independent CPU0 references provide useful work during each physical
 * fragment; this is correctness qualification, NOT live timing acceptance.
 */
#if !BB_WITH_DILLO || !BB_WITH_TELEGRAM || !BB_WITH_EFFECTS
#error "Private retained parity requires the complete retained scratch layout"
#endif
enum {BB_RP_NOT_RUN,BB_RP_RUNNING,BB_RP_PASS,BB_RP_WORKER,BB_RP_MISMATCH,BB_RP_RESTORE};
static struct {uint32_t version,status,cases,bytes,jobs,failed_case,worker_reason;} bb_retained_parity;
static int bb_private_restore_owner(void){
    bb_group_runtime *runtime=&bb_c1_phase_pool.runtime;
    bb_target_init();
    if(!bb_audio_trio_bind(&bb_sound,&runtime->program)||
       !bb_audio_groups_bind(&bb_sound,runtime->groups,BB_MACHINES)){
        bb_c1_trio_bound=0;(void)bb_c1_live_fail(BB_C1_LIVE_BOOT_BAD);return 0;
    }
    return 1;
}
static int bb_retained_parity_startup(void){
    if(!bb_private_render_admitted())return 0;
    if(bb_retained_parity.status)return bb_retained_parity.status==BB_RP_PASS;
    bb_retained_parity.version=2;bb_retained_parity.status=BB_RP_RUNNING;
    /* Runtime binding writes fader fields, not padding. Preserve EVERY
     * original group byte in the unused tail of the existing phase pool.
     * Reference lifetime is over; no producer/audio/909 owner runs here. */
    _Static_assert(sizeof(bb_c1_phase_pool)>=sizeof(bb_group_runtime)+BB_MACHINES*sizeof(gg_state),
        "existing pre-audio pool tail must contain group backup");
    unsigned char *saved_groups=(unsigned char *)&bb_c1_phase_pool+sizeof(bb_group_runtime);
    bb_c1_drum_copy(saved_groups,bb_c1_phase_pool.runtime.groups,BB_MACHINES*sizeof(gg_state));
    uint32_t jobs=bb_c1_live.jobs;
    const unsigned sizes[8]={1,7,32,64,65,96,127,128};
    for(unsigned c=0;c<8;c++){
        unsigned n=sizes[c];drum808_t *d=&bb_sound.kit[0];drum808_init(d);gg_init(&bb_sound.groups[0]);
        for(int v=0;v<D8_NUM;v++){
            for(int p=0;p<drum808_nparams(v);p++){
                const char *name=drum808_param(v,p)->name;
                if(bb_private_title_is(name,"Rev"))drum808_set(d,v,p,48);
                if(bb_private_title_is(name,"Dly"))drum808_set(d,v,p,64);
                if(bb_private_title_is(name,"Drive"))drum808_set(d,v,p,c&2u?96:0);
                if(bb_private_title_is(name,"Sound"))drum808_set(d,v,p,c&1u);
                if(bb_private_title_is(name,"Tune"))drum808_set(d,v,p,(53u+7u*c+(unsigned)v)%128u);
            }
            drum808_trigger(d,v,c&1u?D8_VEL_NORMAL:1.0f);
        }
        for(unsigned g=0;g<GG_COUNT;g++)
            if(!gg_set(&bb_sound.groups[0],g,32u+(c*13u+g*17u)%96u,(c+g)%5u==0u))goto mismatch;
        bb_c1_drum_copy(&bb_sound.kit[1],d,sizeof *d);
        bb_c1_drum_copy(&bb_sound.kit[2],d,sizeof *d);
        bb_c1_drum_copy(&bb_sound.groups[1],&bb_sound.groups[0],sizeof(gg_state));
        bb_c1_drum_copy(&bb_sound.groups[2],&bb_sound.groups[0],sizeof(gg_state));
        for(unsigned i=0;i<n;i++){
            bb_sound.drum_dry[i]=bb_sound.drum_rev[i]=bb_sound.drum_dly[i]=0;
            bb_sound.lead[i]=bb_sound.echo_send[i]=bb_sound.spring_send[i]=0;
        }
        if(bb_retained_begin(bb_sound.kit,bb_sound.groups,1u,n)!=0)goto worker;
        /* Reference1 renders the WHOLE original span, not matching fragments. */
        bb_proposal_group_span(&bb_sound.kit[1],&bb_sound.groups[1],bb_sound.drum_dry,
            bb_sound.drum_rev,bb_sound.drum_dly,(int)n);
        if(n>64u && !bb_retained_advance(0,n))goto worker;
        bb_proposal_group_span(&bb_sound.kit[2],&bb_sound.groups[2],bb_sound.lead,
            bb_sound.echo_send,bb_sound.spring_send,(int)n);
        if(!bb_retained_finish(0,&bb_sound.kit[0],&bb_sound.groups[0],bb_sound.left,
            bb_sound.right,bb_sound.bass,n))goto worker;
        if(!bb_c1_dsp_bytes_equal(&bb_sound.kit[0],&bb_sound.kit[1],sizeof *d)||
           !bb_c1_dsp_bytes_equal(&bb_sound.kit[0],&bb_sound.kit[2],sizeof *d)||
           !bb_c1_dsp_bytes_equal(&bb_sound.groups[0],&bb_sound.groups[1],sizeof(gg_state))||
           !bb_c1_dsp_bytes_equal(&bb_sound.groups[0],&bb_sound.groups[2],sizeof(gg_state))||
           !bb_c1_dsp_bytes_equal(bb_sound.left,bb_sound.drum_dry,n*sizeof(float))||
           !bb_c1_dsp_bytes_equal(bb_sound.right,bb_sound.drum_rev,n*sizeof(float))||
           !bb_c1_dsp_bytes_equal(bb_sound.bass,bb_sound.drum_dly,n*sizeof(float))||
           !bb_c1_dsp_bytes_equal(bb_sound.left,bb_sound.lead,n*sizeof(float))||
           !bb_c1_dsp_bytes_equal(bb_sound.right,bb_sound.echo_send,n*sizeof(float))||
           !bb_c1_dsp_bytes_equal(bb_sound.bass,bb_sound.spring_send,n*sizeof(float)))goto mismatch;
        bb_retained_parity.bytes+=2u*(sizeof *d+sizeof(gg_state)+3u*n*sizeof(float));
        bb_retained_parity.cases++;
        continue;
worker:
        if(!bb_c1_live.failed)(void)bb_c1_live_fail(BB_C1_LIVE_TARGET_BAD);
        bb_retained_parity.status=BB_RP_WORKER;bb_retained_parity.failed_case=c+1u;break;
mismatch:
        bb_retained_parity.status=BB_RP_MISMATCH;bb_retained_parity.failed_case=c+1u;
        (void)bb_c1_live_fail(BB_C1_LIVE_RESULT_BAD);break;
    }
    bb_retained_parity.jobs=bb_c1_live.jobs-jobs;
    if(bb_retained_parity.cases==8u&&bb_retained_parity.status==BB_RP_RUNNING)bb_retained_parity.status=BB_RP_PASS;
    if(!bb_private_restore_owner())bb_retained_parity.status=BB_RP_RESTORE;
    bb_c1_drum_copy(bb_c1_phase_pool.runtime.groups,saved_groups,BB_MACHINES*sizeof(gg_state));
    bb_retained_parity.worker_reason=bb_c1_live.failed;
    return bb_retained_parity.status==BB_RP_PASS;
}
