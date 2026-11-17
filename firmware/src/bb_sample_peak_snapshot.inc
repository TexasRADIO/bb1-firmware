/* SPDX-License-Identifier: GPL-3.0-only */
/* Included after private_render_startup.inc defines the retained peak record.
 * Startup-only, AFTER the measured timestamp. No clocks, allocation, DSP
 * mutation or new retained RAM. Copying may perturb subsequent cache state;
 * use this for attribution, not an uninstrumented speed comparison. */
static void bb_sample_peak_snapshot(unsigned block,uint32_t elapsed) {
#if BB_AUDIO_PROFILE
    bb_private_peak.valid=0;
    bb_private_peak.version=1;
    bb_private_peak.block=block;
    bb_private_peak.frame=(uint32_t)bb_sound.core.frame;
    bb_private_peak.elapsed=elapsed;
    uint64_t sum=0;
    unsigned valid=!bb_profile.errors && bb_attr_complete(&bb_attr);
    for(unsigned s=0;s<BB_PROF_COUNT;s++) {
        bb_private_peak.ticks[s]=bb_profile.stage[s].ticks;
        bb_private_peak.calls[s]=bb_profile.stage[s].calls;
        bb_private_peak.frames[s]=bb_profile.stage[s].frames;
        sum+=bb_profile.stage[s].ticks;
        if(bb_profile.stage[s].frames>128u)valid=0;
    }
    if(sum>elapsed)valid=0;
    bb_private_peak.stage_sum=(uint32_t)sum;
    bb_private_peak.hits_a=bb_sound.hits[0];
    bb_private_peak.hits_b=bb_sound.hits[1];
    bb_private_peak.next_a=bb_sound.core.drums[0].next;
    bb_private_peak.next_b=bb_sound.core.drums[1].next;
    bb_private_peak.next_dillo=bb_sound.core.synths[0].next;
    bb_private_peak.next_telegram=bb_sound.core.synths[1].next;
    /* CPU0-only candidate admits no worker jobs. Do not present stale worker
     * measurements from another startup diagnostic as part of this interval. */
    bb_private_peak.worker_service=bb_private_peak.worker_wait=0;
    bb_private_peak_attribution=bb_attr;
    bb_private_peak.valid=valid;
#else
    (void)block;(void)elapsed;
    bb_private_peak.version=0;bb_private_peak.valid=0;
#endif
}
