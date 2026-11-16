/* SPDX-License-Identifier: GPL-3.0-only
 * Big Beat offline sample-kit adapter. Audio-thread-owned voice slots.
 * No fixed hardware polyphony claim: caller budgets/provides slot count.
 * No stealing. Capacity failure and unmapped notes are explicit results.
 */
#ifndef BB_SAMPLE_KIT_R1_H
#define BB_SAMPLE_KIT_R1_H
#include "bb_sample_voice_r1.h"
enum {BB_SK_OK,BB_SK_INVALID,BB_SK_UNMAPPED,BB_SK_FULL};
#define BB_SK_MAX_SLOTS 32u
typedef struct {
    const uint8_t *data;
    size_t bytes;
    uint32_t frames,rate;
    uint8_t note,choke;
} bb_sample_zone;
typedef struct {bb_sample_voice voice;uint8_t velocity,choke,note;} bb_sample_slot;
/* Zones/data must already be validated and remain immutable for active hits.
 * A nonzero choke group is an explicit kit choice, not inferred by names.
 * Closed/open hats can share a group; zero means independent tails. */
static int bb_sample_kit_trigger(bb_sample_slot *slots,unsigned count,
                                const bb_sample_zone *zones,unsigned nz,
                                unsigned note,unsigned velocity) {
    if(!slots||!count||count>BB_SK_MAX_SLOTS||!zones||!nz||nz>16||note>127||velocity>127)
        return BB_SK_INVALID;
    if(!velocity)return BB_SK_OK;
    const bb_sample_zone *z=0;
    for(unsigned i=0;i<nz;i++)if(zones[i].note==note){if(z)return BB_SK_INVALID;z=&zones[i];}
    if(!z)return BB_SK_UNMAPPED;
    bb_sample_voice next;
    if(!bb_sample_start(&next,z->data,z->bytes,z->frames,z->rate))return BB_SK_INVALID;
    unsigned target=count;
    for(unsigned i=0;i<count;i++)if(!slots[i].voice.active ||
        (z->choke && slots[i].choke==z->choke)){target=i;break;}
    if(target==count)return BB_SK_FULL;
    /* Admission is complete before existing voices can be stopped. */
    if(z->choke)for(unsigned i=0;i<count;i++)
        if(slots[i].choke==z->choke)bb_sample_stop(&slots[i].voice);
    slots[target]=(bb_sample_slot){next,(uint8_t)velocity,z->choke,z->note};
    return BB_SK_OK;
}
/* Bounded raw sum, not normalized or limited: caller owns instrument gain
 * and final output guard. Max magnitude <= 32*32768, within int32 range. */
static int bb_sample_kit_tick(bb_sample_slot *slots,unsigned count,int32_t *out) {
    if(!slots||!out||!count||count>BB_SK_MAX_SLOTS)return BB_SK_INVALID;
    int32_t sum=0;
    for(unsigned i=0;i<count;i++){
        if(slots[i].velocity>127)return BB_SK_INVALID;
    }
    for(unsigned i=0;i<count;i++)sum+=bb_sample_tick(&slots[i].voice)*slots[i].velocity/127;
    *out=sum;return BB_SK_OK;
}
#endif
