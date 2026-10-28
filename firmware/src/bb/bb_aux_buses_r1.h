/* SPDX-License-Identifier: GPL-3.0-only
 * Isolated offline auxiliary one-shot bus candidate. Not wired to target.
 * Main kit/synth owns the authoritative group ramps; this adds auxiliary
 * tails using the same pre-span values without advancing those ramps twice.
 */
#ifndef BB_AUX_BUSES_R1_H
#define BB_AUX_BUSES_R1_H
#include "bb_sample_buses_r1.h"
static int bb_aux_buses_add(bb_sample_slot *slots,unsigned count,
                            const gg_state *pre_span,float gain,
                            unsigned echo,unsigned spring,
                            float *dry,float *delay,float *reverb,unsigned frames) {
    if(!pre_span)return BB_SK_INVALID;
    gg_state local=*pre_span;
    return bb_sample_buses_mode(slots,count,&local,gain,echo,spring,
                                dry,delay,reverb,frames,1);
}
#endif
