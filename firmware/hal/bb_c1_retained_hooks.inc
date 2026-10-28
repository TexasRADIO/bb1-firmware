/* SPDX-License-Identifier: GPL-3.0-only */
/* Experimental real mailbox integration, connected to this candidate's audio_span.
 * CPU0 does not inspect private context/payload between physical fragments.
 * bb_c1_live.busy owns the union until FINAL checked completion/validation.
 * No new audio buffers; one original snapshot, <=64-frame logical fragments
 * executed under ONE physical dispatch and ONE final acknowledgement.
 */
static int bb_retained_wait(uint32_t *result) {
    uint32_t t0=fm1_ticks();int status;
    while((status=bb_c1_complete(&bb_c1_mail,bb_c1_live.ticket,result))==0 &&
          fm1_ticks()-t0<24000u)BB_C1_SYNC();
    uint32_t elapsed=fm1_ticks()-t0;
    bb_c1_live.wait_ticks=elapsed;
    bb_c1_live.service_ticks=(t0+elapsed)-bb_c1_live.submitted_ticks;
    if(elapsed>bb_c1_live.wait_max_ticks)bb_c1_live.wait_max_ticks=elapsed;
    if(status!=1)return bb_c1_live_fail(status<0?BB_C1_LIVE_RESULT_BAD:BB_C1_LIVE_TIMEOUT);
    return 1;
}
int bb_retained_begin(const drum808_t *kit,const gg_state *groups,unsigned count,unsigned frames) {
    if(bb_c1_live.failed)return -2;
    if(!bb_c1_live.ready)return -1;
    if(bb_c1_phase!=BB_C1_PHASE_RUNTIME || bb_c1_909_owner_armed || !kit || !groups ||
       !count || count>4u || !frames || frames>128u || bb_c1_live.busy || bb_c1_retained_control.active) {
        bb_c1_live_fail(BB_C1_LIVE_BUSY);return -2;
    }
    BB_C1_SYNC();
    if(bb_c1_mail.bad || bb_c1_mail.job!=bb_c1_mail.done) {
        bb_c1_live_fail(BB_C1_LIVE_RESULT_BAD);return -2;
    }
    int selected=-1;
    for(unsigned k=0;k<count;k++)if(drum808_active(&kit[k])){selected=(int)k;break;}
    if(selected<0)return -1;
    /* Prior lease ended and mailbox is idle, so CPU0 may inspect metadata.
     * Logical fragment tokens and physical mailbox tickets are distinct. */
    uint32_t ticket=bb_c1_mail.job+1u,transaction=bb_c1_retained_state.last_job+1u;
    if(!ticket || !transaction || (frames>64u && transaction==UINT32_MAX)) {
        bb_c1_live_fail(BB_C1_LIVE_TICKET_END);return -2;
    }
    if(!bb_rt_prepare(&bb_c1_retained_state,&bb_c1_drum_private,&kit[selected],&groups[selected],1u,frames,transaction)) {
        bb_c1_live_fail(BB_C1_LIVE_PREPARE_BAD);return -2;
    }
    bb_c1_live.selected=(unsigned)selected;bb_c1_live.frames=frames;
    bb_c1_live.ticket=ticket;bb_c1_live.busy=1;bb_c1_live.active_mask=1u<<(unsigned)selected;
    bb_c1_retained_control.active=1;bb_c1_retained_control.transaction=transaction;bb_c1_retained_control.offset=0;
    bb_c1_retained_request=bb_rt_chain_make(ticket,transaction,1u,frames);
    bb_c1_retained_control.last_fragment=transaction+(frames>64u?1u:0u);
    bb_c1_live.submitted_ticks=fm1_ticks();
    if(!bb_c1_publish(&bb_c1_mail,BB_C1_RETAINED_JOB,ticket)) {
        bb_c1_live_fail(BB_C1_LIVE_PUBLISH_BAD);return -2;
    }
    return selected;
}
int bb_retained_advance(unsigned selected,unsigned frames) {
    if(!bb_c1_live.ready || !bb_c1_live.busy || bb_c1_live.failed || !bb_c1_retained_control.active ||
       selected!=bb_c1_live.selected || frames!=bb_c1_live.frames || frames<=64u || bb_c1_retained_control.offset)
        return bb_c1_live_fail(BB_C1_LIVE_TARGET_BAD);
    /* CPU0 ledger only. Worker may still be executing either fragment.
     * No wait, mailbox/descriptor mutation, or private-state read here.
     * Retain the lease until checked FINAL completion and validation. */
    bb_c1_retained_control.offset=64u;
    return 1;
}
int bb_retained_finish(unsigned selected,drum808_t *live,gg_state *groups,
                               float *dry,float *rev,float *dly,unsigned frames) {
    if(!bb_c1_live.ready || !bb_c1_live.busy || bb_c1_live.failed || !bb_c1_retained_control.active ||
       !live || !groups || !dry || !rev || !dly || selected!=bb_c1_live.selected || frames!=bb_c1_live.frames ||
       bb_c1_retained_control.offset!=(frames>64u?64u:0u))return bb_c1_live_fail(BB_C1_LIVE_TARGET_BAD);
    uint32_t result;
    if(!bb_retained_wait(&result))return 0;
    /* This is the first CPU0 private-state read since the initial publish. */
    uint32_t transaction=bb_c1_retained_control.transaction;
    if(!bb_rt_valid(&bb_c1_retained_state) || bb_c1_retained_state.phase!=BB_RT_DONE ||
       bb_c1_retained_state.transaction!=transaction || bb_c1_retained_state.kits!=1u ||
       bb_c1_retained_state.frames!=frames || bb_c1_retained_state.last_job!=bb_c1_retained_control.last_fragment ||
       bb_c1_retained_state.output_check!=result || !bb_c1_drum_valid(&bb_c1_drum_private,transaction) ||
       bb_c1_drum_private.kits!=1u || bb_c1_drum_private.frames!=frames ||
       !bb_c1_live_payload_ok(&bb_c1_drum_private,result))return bb_c1_live_fail(BB_C1_LIVE_PAYLOAD_BAD);
    /* Existing fixed audio buffers, not mailbox-supplied addresses. All
     * full payload/group/finite checks precede every live/sample write. */
    bb_c1_drum_copy(live,&bb_c1_drum_private.kit[0],sizeof *live);
    bb_c1_drum_copy(groups,&bb_c1_drum_private.groups[0],sizeof *groups);
    for(unsigned i=0;i<frames;i++) {
        dry[i]=bb_c1_drum_private.bus[0][0][i];rev[i]=bb_c1_drum_private.bus[0][1][i];dly[i]=bb_c1_drum_private.bus[0][2][i];
    }
    bb_c1_retained_state.phase=BB_RT_COMMITTED;bb_rt_seal(&bb_c1_retained_state);
    bb_c1_retained_control.active=0;bb_c1_live.busy=0;bb_c1_live.jobs++;return 1;
}
int bb_retained_prepare_others(drum808_t *kit,gg_state *groups,unsigned count,
                                unsigned frames,unsigned offset,unsigned slice,unsigned worker) {
    if(!kit || !groups || !count || count>4u || worker>=count || !frames || frames>128u ||
       offset>=frames || !slice || slice!=(frames-offset>64u?64u:frames-offset) ||
       !bb_c1_live.ready || !bb_c1_live.busy || bb_c1_live.failed || !bb_c1_retained_control.active ||
       worker!=bb_c1_live.selected || frames!=bb_c1_live.frames || offset!=bb_c1_retained_control.offset)
        return bb_c1_live_fail(BB_C1_LIVE_TARGET_BAD);
    if(!offset) {
        for(unsigned k=0;k<count;k++)if(!bb_group_state_valid(&groups[k]))return bb_c1_live_fail(BB_C1_LIVE_TARGET_BAD);
        bb_c1_other.mask=0;bb_c1_other.count=count;bb_c1_other.frames=frames;
        bb_c1_other.worker=worker;bb_c1_other.ticket=bb_c1_retained_control.transaction;
        bb_c1_other.rendered=0;bb_c1_other.retained=1;
        for(unsigned k=0;k<count;k++) {
            bb_c1_other.slot[k]=255;
            if(drum808_active(&kit[k]))bb_c1_other.mask|=1u<<k;
        }
        bb_c1_live.active_mask=bb_c1_other.mask;
        bb_c1_other.pending=bb_c1_other.mask&~(1u<<worker);
        unsigned slot=0;
        for(unsigned k=0;k<count;k++)if(k!=worker && (bb_c1_other.mask&(1u<<k))) {
            bb_c1_other.slot[k]=(uint8_t)slot;
            for(unsigned b=0;b<3;b++)for(unsigned i=0;i<frames;i++)bb_c1_other.bus[slot][b][i]=0;
            slot++;
        }
    }
    if(!bb_c1_other.retained || bb_c1_other.ticket!=bb_c1_retained_control.transaction ||
       bb_c1_other.rendered!=offset || bb_c1_other.frames!=frames || bb_c1_other.worker!=worker || bb_c1_other.count!=count)
        return bb_c1_live_fail(BB_C1_LIVE_TARGET_BAD);
    for(unsigned k=0;k<count;k++)if(k!=worker && (bb_c1_other.mask&(1u<<k))) {
        unsigned slot=bb_c1_other.slot[k];
        if(slot>=3u)return bb_c1_live_fail(BB_C1_LIVE_TARGET_BAD);
        bb_proposal_group_span(&kit[k],&groups[k],bb_c1_other.bus[slot][0]+offset,
            bb_c1_other.bus[slot][1]+offset,bb_c1_other.bus[slot][2]+offset,(int)slice);
    }
    bb_c1_other.rendered+=slice;return 1;
}
