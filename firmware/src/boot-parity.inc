/* SPDX-License-Identifier: GPL-3.0-only */
/* Pre-audio ONLY: CPU0 kits/groups/sample arrays are scratch. Caller must
 * restore factory state and reseed the idle owner before enabling audio.
 * No transport, output device, allocation, retry or mailbox reset here. */
static int pab_boot_parity(bb_audio *a,unsigned verified_pre_audio) {
    if(verified_pre_audio!=1||!pab_ready(a)||a->core.running||a->core.frame||
       !a->groups||!bb_commands_idle(&a->commands)||!pb_ownership_idle(&a->parts))return 0;
    pa_audio_binding *b=pab_current;
    if(b->block||b->lease->phase!=PL_IDLE||b->route.kits!=1||
       b->route.slot[0]!=0||b->route.slot[1]!=PO_NO_SLOT||
       b->route.slot[2]!=PO_NO_SLOT||b->route.slot[3]!=PO_NO_SLOT)return 0;
    BB_C1_SYNC();
    if(b->lease->mail.job!=b->lease->ticket||b->lease->mail.done!=b->lease->ticket||
       b->lease->mail.bad||b->lease->mail.alive!=BB_C1_ALIVE||
       b->lease->owner.phase!=PO_READY||b->lease->owner.ticket!=b->lease->ticket||
       b->lease->owner.state!=b->lease->state||!po_shape(&b->lease->owner)||
       po_state_hash(&b->lease->owner.payload)!=b->lease->state)return 0;
    /* Validate the independent reference before any test mutation. */
    for(unsigned k=0;k<1;k++)
        if(__builtin_memcmp(a->kit+k,b->lease->owner.payload.kit+k,sizeof a->kit[k])||
           __builtin_memcmp(a->groups+k,b->lease->owner.payload.groups+k,sizeof a->groups[k]))return 0;
    const unsigned sizes[8]={1,7,32,64,65,96,127,128};
    for(unsigned c=0;c<8;c++) {
        unsigned n=sizes[c];a->offset=0;
        __builtin_memset(a->left,0,sizeof a->left);
        __builtin_memset(a->right,0,sizeof a->right);
        if(!bb_owner_audio_block_begin(a))return 0;
        for(unsigned k=0;k<1;k++) {
            po_sound_apply(a->kit+k,&a->core.drums[k].sound);
            for(unsigned v=0;v<D8_NUM;v++) {
                bb_hit hit={0};hit.machine=k;hit.voice=v;
                hit.velocity=(c+k+v)&1u?88:127;
                if(bb_owner_audio_hit(a,&hit)!=1)return pab_fail(b);
                float velocity=hit.velocity==127?1.0f:hit.velocity*(D8_VEL_NORMAL/88.0f);
                drum808_trigger(a->kit+k,v,velocity);
            }
            bb_command group={0};group.type=BB_CMD_GROUP_LEVEL;group.target=k;
            group.voice=c%GG_COUNT;group.value=32+(c*13+k*17)%96;
            if(bb_owner_audio_group(a,&group)!=1||
               !gg_set(a->groups+k,group.voice,group.value,a->groups[k].fader[group.voice].muted))return pab_fail(b);
        }
        if(!bb_owner_audio_span_begin(a,n))return 0;
        for(unsigned k=0;k<1;k++) {
            float *bus[3]={a->drum_dry,a->drum_rev,a->drum_dly};
            for(unsigned j=0;j<3;j++)__builtin_memset(bus[j],0,n*sizeof(float));
            for(unsigned at=0;at<n;) {
                unsigned count=n-at>64?64:n-at;
                bb_proposal_group_span(a->kit+k,a->groups+k,bus[0]+at,bus[1]+at,bus[2]+at,(int)count);
                at+=count;
            }
            /* CPU0 reference work overlaps the worker, as local drum work does live. */
            if(!bb_owner_audio_span_collect(a,n))return 0;
            for(unsigned j=0;j<3;j++)
                if(__builtin_memcmp(bus[j],pl_view_bus(b->lease,k,j),n*sizeof(float)))return pab_fail(b);
            if(__builtin_memcmp(a->kit+k,b->lease->owner.payload.kit+k,sizeof a->kit[k])||
               __builtin_memcmp(a->groups+k,b->lease->owner.payload.groups+k,sizeof a->groups[k]))return pab_fail(b);
            if(bb_owner_audio_mix(a,k,0,1.0f,n)!=1)return pab_fail(b);
        }
        if(!bb_owner_audio_span_release(a)||!bb_owner_audio_block_end(a))return 0;
    }
    return 1; /* Caller still owes restoration and target acceptance. */
}
