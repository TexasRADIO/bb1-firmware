/* SPDX-License-Identifier: GPL-3.0-only
 * Isolated auxiliary SY/ZAP one-shot owner. Not yet wired to target firmware.
 * The BBSK pack and its ADPCM bytes must remain immutable for this lifetime.
 */
#ifndef BB_AUX_VOICES_R1_H
#define BB_AUX_VOICES_R1_H
#include "bb_aux_buses_r1.h"

/* Eight slots per owner cover the measured four-voice peak in an unchanged
 * factory phrase with room for transition tails. Other workloads can fill
 * the pool; explicit FULL is safer than stealing or writing beyond it. */
enum { BB_AUX_MACHINES=2,BB_AUX_ZONES=2,BB_AUX_SLOTS_PER_MACHINE=8,
       BB_AUX_SY=17,BB_AUX_ZAP=18,BB_AUX_SY_NOTE=76,BB_AUX_ZAP_NOTE=77 };
typedef struct {
    bb_sample_zone zones[BB_AUX_ZONES];
    bb_sample_slot slots[BB_AUX_MACHINES][BB_AUX_SLOTS_PER_MACHINE];
    uint8_t ready;
} bb_aux_voices;

/* Cold-only: no active voices and no previous binding. The enclosing caller
 * validates the whole immutable BBSK pack before extracting these zones. */
static int bb_aux_voices_bind(bb_aux_voices *owner,const bb_sample_zone *zones,unsigned count) {
    if(!owner||owner->ready||!zones||count!=BB_AUX_ZONES)return 0;
    if(zones[0].note!=BB_AUX_SY_NOTE||zones[1].note!=BB_AUX_ZAP_NOTE)return 0;
    for(unsigned i=0;i<count;i++) {
        if(!zones[i].data||!zones[i].bytes||!zones[i].frames||
           zones[i].frames>8192||zones[i].rate!=32768u||zones[i].choke)return 0;
        bb_sample_voice probe; /* bb_sample_start initializes on success. */
        if(!bb_sample_start(&probe,zones[i].data,zones[i].bytes,
                            zones[i].frames,zones[i].rate))return 0;
    }
    for(unsigned m=0;m<BB_AUX_MACHINES;m++)
        for(unsigned s=0;s<BB_AUX_SLOTS_PER_MACHINE;s++)
            if(owner->slots[m][s].voice.active)return 0;
    owner->zones[0]=zones[0];owner->zones[1]=zones[1];owner->ready=1;
    return 1;
}

static int bb_aux_voices_trigger(bb_aux_voices *owner,unsigned machine,
                                  unsigned source_voice,unsigned velocity) {
    if(!owner||!owner->ready||machine>=BB_AUX_MACHINES||velocity>127)return BB_SK_INVALID;
    unsigned note=source_voice==BB_AUX_SY?BB_AUX_SY_NOTE:
                  source_voice==BB_AUX_ZAP?BB_AUX_ZAP_NOTE:128u;
    if(note>127)return BB_SK_INVALID;
    return bb_sample_kit_trigger(owner->slots[machine],BB_AUX_SLOTS_PER_MACHINE,
                                 owner->zones,BB_AUX_ZONES,note,velocity);
}

/* Transport STOP discards tails on both drum owners but retains the cold
 * immutable zone binding for the next START. No decoder runs here. */
static void bb_aux_voices_stop(bb_aux_voices *owner) {
    if(!owner)return;
    for(unsigned m=0;m<BB_AUX_MACHINES;m++)
        for(unsigned s=0;s<BB_AUX_SLOTS_PER_MACHINE;s++)
            bb_sample_stop(&owner->slots[m][s].voice);
}
static void bb_aux_voices_stop_machine(bb_aux_voices *owner,unsigned machine) {
    if(!owner||machine>=BB_AUX_MACHINES)return;
    for(unsigned s=0;s<BB_AUX_SLOTS_PER_MACHINE;s++)
        bb_sample_stop(&owner->slots[machine][s].voice);
}
static int bb_aux_voices_active(const bb_aux_voices *owner,unsigned machine) {
    if(!owner||!owner->ready||machine>=BB_AUX_MACHINES)return 0;
    for(unsigned s=0;s<BB_AUX_SLOTS_PER_MACHINE;s++)
        if(owner->slots[machine][s].voice.active)return 1;
    return 0;
}

/* The main kit renderer must already have advanced its authoritative groups
 * exactly once for this span. Pass its saved pre-span group state here. */
static int bb_aux_voices_render(bb_aux_voices *owner,unsigned machine,
                                 const gg_state *pre_span,float gain,
                                 unsigned echo,unsigned spring,
                                 float *dry,float *delay,float *reverb,unsigned frames) {
    if(!owner||!owner->ready||machine>=BB_AUX_MACHINES)return BB_SK_INVALID;
    return bb_aux_buses_add(owner->slots[machine],BB_AUX_SLOTS_PER_MACHINE,
                            pre_span,gain,echo,spring,dry,delay,reverb,frames);
}
#endif
