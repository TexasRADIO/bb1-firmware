/* SPDX-License-Identifier: GPL-3.0-only */
/* Main-loop only under independently verified audio/producer suspension.
 * Idle worker remains alive; mailbox and sequence are never reset. */
static int pab_session_owned_idle(bb_audio *a,unsigned suspended) {
    if(suspended!=1||!pab_ready(a)||!a->groups||a->core.running)return 0;
    pa_audio_binding *b=pab_current;po_lease *l=b->lease;
    BB_C1_SYNC();
    if(b->block||l->phase!=PL_IDLE||l->owner.phase!=PO_READY||
       l->mail.job!=l->ticket||l->mail.done!=l->ticket||l->mail.bad||
       l->mail.alive!=BB_C1_ALIVE)return 0;
    if(!bb_rt_separate(l,sizeof *l,a->groups,BB_MACHINES*sizeof(gg_state))||
       !po_routing_valid(&b->route)||b->route.kits!=l->kits||!po_shape(&l->owner)||
       l->owner.ticket!=l->ticket||l->owner.state!=l->state||
       po_state_hash(&l->owner.payload)!=l->state)return pab_fail(b);
    for(unsigned s=0;s<l->kits;s++)
        if(!bb_group_state_valid(l->owner.payload.groups+s))return pab_fail(b);
    return 1;
}
/* Refresh mirrors used by the existing family-scene encoder. No DSP export
 * is needed to save its portable sounds/patterns, only current group faders. */
static int pab_session_snapshot_groups(bb_audio *a,unsigned suspended) {
    if(!pab_session_owned_idle(a,suspended))return 0;
    pa_audio_binding *b=pab_current;
    for(unsigned k=0;k<BB_MACHINES;k++)if(b->route.slot[k]!=PO_NO_SLOT)
        a->groups[k]=b->lease->owner.payload.groups[b->route.slot[k]];
    return 1;
}
/* Existing validated family restore has rebuilt CPU0 audio and group mix.
 * Transfer its fresh drum states into the same idle owner; no old tails and
 * no mailbox reset. Failure after CPU0 restore must retain the platform hold. */
static int pab_session_reseed(bb_audio *a,unsigned suspended) {
    if(!pab_session_owned_idle(a,suspended))return 0;
    pa_audio_binding *b=pab_current;po_lease *l=b->lease;
    for(unsigned k=0;k<BB_MACHINES;k++)if(b->route.slot[k]!=PO_NO_SLOT)
        if(!bb_group_state_valid(a->groups+k))return pab_fail(b);
    for(unsigned k=0;k<BB_MACHINES;k++)if(b->route.slot[k]!=PO_NO_SLOT) {
        unsigned s=b->route.slot[k];
        l->owner.payload.kit[s]=a->kit[k];l->owner.payload.groups[s]=a->groups[k];
        b->revision[k]=0;a->drum_revision[k]=0;
    }
    l->state=l->owner.state=po_state_hash(&l->owner.payload);return 1;
}
