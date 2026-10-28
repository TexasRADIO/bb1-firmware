/* SPDX-License-Identifier: GPL-3.0-only */
/* Fixed globals are written by the target producer and audio consumer.
 * No caller-supplied 'safe' boolean or restart of CPU1 substitutes for ack. */
extern volatile uint32_t bb_kit_hold_request,bb_kit_hold_ack;
static int bb_c1_909_switch_idle(void) {
    BB_C1_SYNC();
    return bb_kit_hold_request==1u&&bb_kit_hold_ack==1u
        &&bb_c1_phase==BB_C1_PHASE_RUNTIME&&bb_c1_live.ready&&!bb_c1_live.failed
        &&!bb_c1_live.busy&&!bb_c1_909_runtime.busy&&!bb_c1_909_runtime.loan
        &&!bb_c1_909_runtime.failed&&!bb_c1_909_group_live&&!bb_c1_mail.bad&&bb_c1_mail.job==bb_c1_mail.done
        &&bb_c1_released(BB_C1_CON);
}
static int bb_c1_909_bind_suspended(unsigned layer) {
    if(bb_c1_private_pool.persistent.phase)return 0; /* live owner is not scratch */
    if(layer>=4u||bb_c1_909_generation==UINT32_MAX||!bb_c1_909_switch_idle()||bb_c1_909_runtime.bound||bb_c1_909_owner_armed)return 0;
    /* Confirmed suspension prevents any new publisher; CPU1 already has
     * acknowledged completion. Only discarded 808 job scratch is reused. */
    if(!bb909_owner_init(&bb_c1_private_pool.owner,bb_c1_mail.job,
                        (const void *)(uintptr_t)BB909_PACK_XIP_ADDR,BB909_PACK_BYTES))return 0;
    __builtin_memset(&bb_c1_909_pending,0,sizeof bb_c1_909_pending);
    bb_c1_909_runtime.layer=layer;bb_c1_909_runtime.bound=1;
    bb_c1_909_owner_armed=1;bb_c1_909_generation++;
    if(!bb909_audio_control_reset())return bb_c1_909_route_fail(BB_C1_LIVE_PREPARE_BAD);
    BB_C1_SYNC();return 1;
}
static int bb_c1_909_unbind_suspended(unsigned layer) {
    if(!bb_c1_909_switch_idle()||!bb_c1_909_runtime.bound||!bb_c1_909_owner_armed
       ||layer!=bb_c1_909_runtime.layer||bb_c1_909_pending.count)return 0;
    /* The target commits its validated 808 grid/sound before releasing the
     * audio pause. No implicit conversion of any other layer is performed. */
    bb_c1_909_owner_armed=0;BB_C1_SYNC();
    __builtin_memset(&bb_c1_909_runtime,0,sizeof bb_c1_909_runtime);
    __builtin_memset(&bb_c1_909_pending,0,sizeof bb_c1_909_pending);return 1;
}
