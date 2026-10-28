/* SPDX-License-Identifier: GPL-3.0-only */
/* Save/inspection admission belongs to the CPU0 lease, not the raw owner.
 * No private access is permitted during an in-flight/timed-out/faulted lease.
 * This still uses the host-only mailbox model, not a physical idle proof.
 */
#include "lease.h"
static int pl_export(po_lease *l,drum808_t *kits,gg_state *groups) {
    if(!l||l->phase!=PL_IDLE)return 0;
    BB_C1_SYNC();
    if(l->mail.alive!=BB_C1_ALIVE||l->mail.bad||l->mail.job!=l->ticket||l->mail.done!=l->ticket) {
        l->phase=PL_NEEDS_HOLD;return 0;
    }
    BB_LEASE_PRIVATE_READ();
    if(!po_export(&l->owner,l->state,l->kits,kits,groups)) {
        l->phase=PL_NEEDS_HOLD;return 0;
    }
    return 1;
}
