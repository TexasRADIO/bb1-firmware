/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef BB_SAMPLE_PROFILE_SPLIT_R1_H
#define BB_SAMPLE_PROFILE_SPLIT_R1_H
/* Sample candidate attribution v3, using the existing five retained slots:
 * 0 span preparation plus CPU0 synth A; 1 remaining CPU0 synthesis (B in
 * this two-drum profile); 2 sample decode/group buses; 3 conversion; 4 copy.
 * Scopes0..2 are disjoint subsets of the existing Drums stage except the
 * small span-preparation interval. Never add supplemental and main reports.
 * Observer overhead can affect timing/cache: this is not a speed comparison.
 */
static inline __attribute__((always_inline)) void bb_sample_profile_synth(
    unsigned kit,drum808_t *d,gg_state *groups,float *dry,float *rev,float *dly,int n) {
#if BB_AUDIO_PROFILE
    unsigned scope=kit?BB_ATTR_OTHER_KIT:BB_ATTR_PREPARE;
    (void)bb_attr_begin(&bb_attr,scope,bb_profile_clock());
#else
    (void)kit;
#endif
    bb_proposal_group_span(d,groups,dry,rev,dly,n);
#if BB_AUDIO_PROFILE
    (void)bb_attr_end(&bb_attr,scope,bb_profile_clock(),(uint32_t)n);
#endif
}
static inline __attribute__((always_inline)) int bb_sample_profile_buses(
    bb_sample_slot *slots,unsigned count,gg_state *groups,float gain,
    unsigned echo,unsigned spring,float *dry,float *delay,float *reverb,unsigned frames,int add) {
#if BB_AUDIO_PROFILE
    (void)bb_attr_begin(&bb_attr,BB_ATTR_ADVANCE,bb_profile_clock());
#endif
    int result=bb_sample_buses_mode(slots,count,groups,gain,echo,spring,dry,delay,reverb,frames,add);
#if BB_AUDIO_PROFILE
    (void)bb_attr_end(&bb_attr,BB_ATTR_ADVANCE,bb_profile_clock(),frames);
#endif
    return result;
}
#endif
