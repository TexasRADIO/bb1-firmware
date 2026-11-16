/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef BB_DRUM_MIX_H
#define BB_DRUM_MIX_H
/* Settled, non-scope path only. Preserve each original product/addition order.
 * No reassociation, reciprocal approximation, voice skip or tail truncation. */
static __attribute__((noinline)) void bb_drum_mix_settled(
    const float *dry,const float *echo,const float *spring,
    float *left,float *right,float *echo_out,float *spring_out,
    unsigned frames,float pan_l,float pan_r,float gain,float bus)
{
    for(unsigned i=0;i<frames;i++) {
        const float value=dry[i];
        left[i]+=value*pan_l*gain*bus;
        right[i]+=value*pan_r*gain*bus;
        if(echo_out)echo_out[i]+=echo[i]*0.2f*gain*bus;
        if(spring_out)spring_out[i]+=spring[i]*0.2f*gain*bus;
    }
}
#endif
