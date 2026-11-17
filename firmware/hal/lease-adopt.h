/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef BB_PO_LEASE_ADOPT_H
#define BB_PO_LEASE_ADOPT_H
/* One pre-audio lifetime transition after completed boot qualification.
 * The platform must exclude other publishers and boot/private DSP users.
 * CPU1 may remain in its idle mailbox loop. Do not reset its last-job value
 * or write its mailbox fields. This is NOT fault recovery or live restore. */
static int pl_adopt_boot_idle(po_lease *l,const drum808_t *kits,
                              const gg_state *groups,unsigned count,
                              unsigned verified_pre_audio) {
    if(!l||verified_pre_audio!=1||l->phase!=0||!kits||!groups||
       !count||count>2||!bb_rt_separate(l,sizeof *l,kits,count*sizeof *kits)||
       !bb_rt_separate(l,sizeof *l,groups,count*sizeof *groups))return 0;
    BB_C1_SYNC();
    uint32_t ticket=l->mail.job;
    if(ticket==UINT32_MAX||l->mail.done!=ticket||l->mail.bad||
       l->mail.alive!=BB_C1_ALIVE)return 0;
    /* po_init validates all source groups before modifying the private DSP.
     * No mailbox writes, including its retained diagnostic result fields. */
    if(!po_init(&l->owner,kits,groups,count))return 0;
    l->owner.ticket=ticket;
    __builtin_memset(&l->wire,0,sizeof l->wire);
    __builtin_memset(&l->reply,0,sizeof l->reply);
    l->ticket=ticket;l->state=l->owner.state;l->kits=count;l->frames=0;
    BB_C1_SYNC();l->phase=PL_IDLE;return 1;
}
#endif
