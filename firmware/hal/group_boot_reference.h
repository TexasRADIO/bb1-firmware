/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef BB_GROUP_BOOT_REFERENCE_H
#define BB_GROUP_BOOT_REFERENCE_H
#include "bb_c1_drum_job.h"
/* Original-size CPU0 reference. Faders belong to the bounded boot call frame,
 * never to this pool object or the second core's live payload. */
typedef struct {
    uint32_t guard,ticket,kits,frames,input_check;
    drum808_t kit[BB_C1_DRUM_KITS];
    float bus[BB_C1_DRUM_KITS][3][BB_C1_DRUM_FRAMES];
    uint32_t end_guard;
} bb_group_boot_reference;
_Static_assert(sizeof(bb_group_boot_reference)==10352,"boot reference must retain original allocation");
static uint32_t bb_group_reference_check(const bb_group_boot_reference *j,const gg_state *groups){
    uint32_t h=UINT32_C(2166136261);
    h=bb_c1_drum_hash(h,&j->ticket,sizeof j->ticket);
    h=bb_c1_drum_hash(h,&j->kits,sizeof j->kits);
    h=bb_c1_drum_hash(h,&j->frames,sizeof j->frames);
    for(unsigned k=0;k<j->kits;k++){
        h=bb_c1_drum_hash(h,&j->kit[k],sizeof j->kit[k]);
        h=bb_c1_drum_hash(h,&groups[k],sizeof groups[k]);
    }
    return h;
}
static int bb_group_reference_prepare(bb_group_boot_reference *j,const gg_state *groups,
    unsigned kits,unsigned frames,uint32_t ticket){
    if(!j || !groups || !ticket || !kits || kits>BB_C1_DRUM_KITS || !frames || frames>BB_C1_DRUM_FRAMES)return 0;
    for(unsigned k=0;k<kits;k++)if(!bb_group_state_valid(&groups[k]))return 0;
    j->guard=j->end_guard=BB_C1_DRUM_GUARD;j->ticket=ticket;j->kits=kits;j->frames=frames;
    j->input_check=bb_group_reference_check(j,groups);return 1;
}
static int bb_group_reference_run(bb_group_boot_reference *j,gg_state *groups,uint32_t ticket,uint32_t *result){
    if(!j || !groups || !result || !ticket || j->ticket!=ticket ||
       j->guard!=BB_C1_DRUM_GUARD || j->end_guard!=BB_C1_DRUM_GUARD ||
       !j->kits || j->kits>BB_C1_DRUM_KITS || !j->frames || j->frames>BB_C1_DRUM_FRAMES)return 0;
    for(unsigned k=0;k<j->kits;k++)if(!bb_group_state_valid(&groups[k]))return 0;
    if(j->input_check!=bb_group_reference_check(j,groups))return 0;
    for(unsigned k=0;k<j->kits;k++){
        for(unsigned b=0;b<3;b++)for(unsigned f=0;f<j->frames;f++)j->bus[k][b][f]=0;
        bb_proposal_group_span(&j->kit[k],&groups[k],j->bus[k][0],j->bus[k][1],j->bus[k][2],(int)j->frames);
    }
    uint32_t h=bb_group_reference_check(j,groups);
    for(unsigned k=0;k<j->kits;k++)for(unsigned b=0;b<3;b++)h=bb_c1_drum_hash(h,j->bus[k][b],j->frames*sizeof(float));
    *result=h;return 1;
}
#endif
