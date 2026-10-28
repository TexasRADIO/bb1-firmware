/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef BB_PROPOSAL_GROUP_VALIDATE_H
#define BB_PROPOSAL_GROUP_VALIDATE_H
#include "group_gain.h"
/* Same semantic bounds as the checked 808 proposal. Owner memory must be
 * aligned, exclusively held and completely received before calling. */
static inline int gg_valid(const gg_state *s){
    if(!s)return 0;
    for(unsigned g=0;g<GG_COUNT;g++){
        const gg_fader *f=&s->fader[g];
        if(f->level>127||f->muted>1||f->remaining>GG_RAMP_FRAMES
           ||!(f->current>=0.0f&&f->current<=1.0f)
           ||!(f->increment>=-1.0f/GG_RAMP_FRAMES&&f->increment<=1.0f/GG_RAMP_FRAMES)
           ||f->target!=(f->muted?0.0f:(float)f->level/127.0f)
           ||(!f->remaining&&f->current!=f->target))return 0;
    }
    return 1;
}
#endif
