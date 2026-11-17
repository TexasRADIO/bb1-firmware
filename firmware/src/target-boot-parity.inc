/* SPDX-License-Identifier: GPL-3.0-only */
/* After private_render_startup.inc definitions; pre-audio, pre-USB only. */
#include "boot-parity.inc"
static void pab_target_boot_parity(void) {
    bb_private_render_result.status=BB_PR_NOT_ADMITTED;
    if(!bb_private_render_admitted()||!pab_target_startup()) {
        (void)bb_c1_live_fail(BB_C1_LIVE_BOOT_BAD);return;
    }
    _Static_assert(sizeof(bb_c1_phase_pool)>=sizeof(bb_group_runtime)+BB_MACHINES*sizeof(gg_state),
        "pre-audio group backup fits existing pool tail");
    unsigned char *saved=(unsigned char *)&bb_c1_phase_pool+sizeof(bb_group_runtime);
    bb_c1_drum_copy(saved,bb_sound.groups,BB_MACHINES*sizeof(gg_state));
    uint32_t core=bb_c1_drum_hash(PO_GUARD,&bb_sound.core,sizeof bb_sound.core);
    uint32_t ticket=bb_c1_private_pool.persistent.ticket,jobs=bb_c1_live.jobs;
    memset(&bb_private_render_result,0,sizeof bb_private_render_result);
    bb_private_render_result.version=4;bb_private_render_result.kind=6; /* startup parity, NOT live timing */
    if(ticket>UINT32_MAX-8u) {
        bb_private_render_result.status=BB_PR_NOT_ADMITTED;
        (void)bb_c1_live_fail(BB_C1_LIVE_BOOT_BAD);return;
    }
    uint32_t begin=fm1_ticks();
    if(!pab_boot_parity(&bb_sound,1)) {
        bb_private_render_result.status=BB_PR_PARITY;
        if(!bb_c1_live.failed)(void)bb_c1_live_fail(BB_C1_LIVE_RESULT_BAD);
        /* Do not retry, read late worker state or erase the failure. */
        bb_private_render_result.worker_reason=bb_c1_live.failed;return;
    }
    bb_private_render_result.total_ticks=fm1_ticks()-begin;
    bb_private_render_result.physical_jobs=bb_c1_live.jobs-jobs;
    if(bb_private_render_result.physical_jobs!=8u||
       bb_c1_private_pool.persistent.ticket!=ticket+8u||bb_c1_live.busy||bb_c1_live.failed)
        goto restore_failed;
    /* Existing reset rebuilds CPU0 kit/synth/effect/queue storage and rebinds
     * the existing trio/group allocations. Mailbox/tickets are untouched. */
    if(!bb_private_restore_owner())goto restore_failed;
    bb_c1_drum_copy(bb_sound.groups,saved,BB_MACHINES*sizeof(gg_state));
    if(core!=bb_c1_drum_hash(PO_GUARD,&bb_sound.core,sizeof bb_sound.core)||
       !pab_session_reseed(&bb_sound,1)||bb_sound.core.running||bb_sound.core.frame||
       !bb_commands_idle(&bb_sound.commands)||!pb_ownership_idle(&bb_sound.parts))goto restore_failed;
    for(unsigned k=0;k<1;k++) {
        if(memcmp(bb_sound.kit+k,bb_c1_private_pool.persistent.owner.payload.kit+k,sizeof bb_sound.kit[k])||
           memcmp(bb_sound.groups+k,bb_c1_private_pool.persistent.owner.payload.groups+k,sizeof(gg_state)))goto restore_failed;
    }
    bb_private_render_result.blocks=bb_private_render_result.completed_blocks=8;
    bb_private_render_result.status=BB_PR_PASS;return;
restore_failed:
    bb_private_render_result.status=BB_PR_RESTORE;
    if(!bb_c1_live.failed)(void)bb_c1_live_fail(BB_C1_LIVE_BOOT_BAD);
    bb_private_render_result.worker_reason=bb_c1_live.failed;
}
