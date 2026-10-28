/* SPDX-License-Identifier: GPL-3.0-only */
/* Real flags, not a synthesized "safe" claim. Their producer guards still
 * require target-level tests before this compile-only stage can be installed. */
#include "single-drum-platform-r1.inc"
#if defined(BB_SAMPLE_DUAL_TRIAL)
#include "dual-sample-platform-r1.inc"
#endif
static bb_single_pause bb_single_target_pause(void) {
    bb_single_pause p={bb_kit_hold_request,bb_kit_hold_ack,
        bb_session_hold_request==1&&bb_session_hold_ack==1,
        bb_audio_entered,uac.alt};return p;
}
static int pab_target_session_ready(bb_audio *a) {
    bb_single_pause p=bb_single_target_pause();return bb_single_paused(a,&p);
}
static int pab_session_snapshot_groups(bb_audio *a,unsigned verified) {
    return verified==1&&pab_target_session_ready(a); /* groups already on CPU0 */
}
