/* SPDX-License-Identifier: GPL-3.0-only */
/* External outputs must not overwrite ANY shared lease object, including
 * mailbox/request/control fields outside the private DSP owner. Underlying
 * primitives remain preserved experiments; port only these checked entrances.
 */
#include "lease-export.h"
static int pl_collect_checked(po_lease *l,float out[2][3][128]) {
    if(!l)return -1;
    if(!out||!bb_rt_separate(l,sizeof *l,out,2u*3u*128u*sizeof(float))) {
        if(l->phase==PL_INFLIGHT)l->phase=PL_NEEDS_HOLD;
        return -1;
    }
    return pl_collect(l,out);
}
static int pl_export_checked(po_lease *l,drum808_t *kits,gg_state *groups) {
    if(!l||l->phase!=PL_IDLE)return 0;
    if(!kits||!groups||!l->kits||l->kits>2u||
       !bb_rt_separate(l,sizeof *l,kits,l->kits*sizeof *kits)||
       !bb_rt_separate(l,sizeof *l,groups,l->kits*sizeof *groups)) {
        l->phase=PL_NEEDS_HOLD;return 0;
    }
    return pl_export(l,kits,groups);
}
