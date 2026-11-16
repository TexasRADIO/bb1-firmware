/* SPDX-License-Identifier: GPL-3.0-only */
/* Zero-copy read lease: caller must finish all bus reads before release.
 * Const C pointers are not a hardware protection mechanism. Host model only. */
#include "lease-ordered.h"
enum {PL_VIEW=5};
static int pl_collect_view(po_lease *l) {
    if(!l||l->phase!=PL_INFLIGHT)return 0;
    if(!l->frames){l->phase=PL_NEEDS_HOLD;return -1;}
    uint32_t value=0;int status=bb_c1_complete(&l->mail,l->ticket,&value);
    if(!status)return 0;
    if(status<0){l->phase=PL_NEEDS_HOLD;return -1;}
    BB_LEASE_PRIVATE_READ();
    if(value!=l->reply.check||!po_validate_output(&l->owner,&l->reply,l->ticket,l->kits,l->frames)) {
        l->phase=PL_NEEDS_HOLD;return -1;
    }
    l->phase=PL_VIEW;return 1;
}
static const float *pl_view_bus(const po_lease *l,unsigned kit,unsigned bus) {
    if(!l||l->phase!=PL_VIEW||kit>=l->kits||bus>=3)return 0;
    return l->owner.payload.bus[kit][bus];
}
static int pl_view_active(const po_lease *l,unsigned kit) {
    if(!l||l->phase!=PL_VIEW||kit>=l->kits)return -1;
    return (l->owner.payload.input_check>>kit)&1u;
}
static int pl_release_view(po_lease *l) {
    if(!l||l->phase!=PL_VIEW)return 0;
    l->state=l->reply.state;l->owner.phase=PO_READY;l->phase=PL_IDLE;return 1;
}
