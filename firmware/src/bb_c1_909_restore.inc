/* SPDX-License-Identifier: GPL-3.0-only */
#include "bb909_patch.h"
#include "bb_wire.h"
enum {BB_C1_RESTORE_REJECTED,BB_C1_RESTORE_OK,BB_C1_RESTORE_HELD_FAILURE};
/* Main-loop only, after actual audio suspension ack and fresh owner bind.
 * Uses existing fixed request, checked finish, finite scan and release path.
 * Partial application is NOT rollback: any transport failure holds CPU1 and
 * caller must leave audio suspended. No scene is published by this function.
 * Never use on an existing sounding owner: untouched defaults require init. */
static __attribute__((noinline)) int bb_c1_909_restore_suspended(const bb909_controls *s,gg_state *groups) {
    if(!groups||(uintptr_t)groups%_Alignof(gg_state)
       ||!bb909_group_memory_ok(groups,sizeof *groups)||!gg_valid(groups)
       ||!s||bb_wire_overlap(s,sizeof *s,groups,sizeof *groups)||!bb909_patch_valid(s)||!bb_c1_909_switch_idle()||
       !bb_c1_909_runtime.bound||!bb_c1_909_owner_armed||bb_c1_909_runtime.jobs||
       bb_c1_909_pending.count||bb_c1_909_control_epoch!=bb_c1_909_generation||
       bb_wire_overlap(s,sizeof *s,&bb_c1_909_controls,sizeof bb_c1_909_controls)||
       bb_wire_overlap(s,sizeof *s,&bb_c1_909_pending,sizeof bb_c1_909_pending))return BB_C1_RESTORE_REJECTED;
    for(unsigned v=0;v<12;v++)if(bb_c1_909_controls.touched[v])return BB_C1_RESTORE_REJECTED;
    /* Restore jobs render one frame each. Keep those frames off the live
     * fader timeline; no musical state is published during suspension. */
    gg_state working_groups=*groups;
    unsigned cursor=0,next=0;
    for(;;){
        uint32_t ticket=bb_c1_mail.job+1u;
        int result=bb909_patch_request(s,cursor,ticket,&bb_c1_909_pending,&next);
        if(result==BB909_RESTORE_DONE)break;
        if(result!=BB909_RESTORE_READY||!bb_c1_909_switch_idle()||
           !bb909_audio_begin(1,&working_groups))goto failed;
        bb909_audio_view view;
        if(!bb909_audio_finish(1,&view)||!bb909_audio_release(&working_groups))goto failed;
        cursor=next;
    }
    if(__builtin_memcmp(s->value,bb_c1_909_controls.value,sizeof s->value)||
       __builtin_memcmp(s->touched,bb_c1_909_controls.touched,sizeof s->touched))goto failed;
    if(!bb_c1_909_switch_idle())goto failed;
    /* Sole suspended publisher, acknowledged idle CPU1: restore the original
     * timeline before any audio can run. The caller's group state is untouched. */
    bb_c1_private_pool.owner.groups=*groups;
    return BB_C1_RESTORE_OK;
failed:
    if(!bb_c1_909_runtime.failed)(void)bb_c1_909_route_fail(BB_C1_LIVE_PREPARE_BAD);
    return BB_C1_RESTORE_HELD_FAILURE;
}
