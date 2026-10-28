/* SPDX-License-Identifier: GPL-3.0-only */
/* Included after session, asset and kit-switch ownership state is defined.
 * One outer-loop call performs at most one compact-scene job. */
#if BB_WITH_PARTS && BB_WITH_PREPARED_BANK
static volatile int bb_bank_service_result;
static int bb_target_bank_service_allowed(void *context) {
    (void)context;
    if(bb_audio_entered || bb_kit_switch_busy() || !bb_c1_cpu0_available())return 0;
#if BB_WITH_SESSION
    if(bb_session_job || bb_session_hold_request || bb_asset_hold_request ||
       bb_kit_hold_request)return 0;
#if FELUCCA_CDC
    if(bb_asset_locked)return 0;
#endif
#endif
#if FELUCCA_UAC
    if(uac.alt)return 0;
#endif
    return 1;
}
static void bb_target_bank_service(void) {
    bb_bank_service_result=bb_audio_bank_service(&bb_sound,bb_target_bank_service_allowed,0);
}
#else
static void bb_target_bank_service(void) {}
#endif
