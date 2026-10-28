/* SPDX-License-Identifier: GPL-3.0-only */
/* Called only on CPU0 after verified core1 hold; never on successful audio. */
static void bb_capture_first_fault(uint32_t reason) {
    if(bb_first_fault.valid)return;
    unsigned selected=bb_c1_live.selected;
    uint32_t applied=0,queued=0;
    if(selected<BB_MACHINES) {
        applied=bb_sound.drum_patterns.status[selected].state.applied;
        queued=bb_sound.drum_patterns.status[selected].state.queued;
    }
    bb_first_fault_capture(&bb_first_fault,reason,felucca_dbg.stage,
        felucca_dbg.page,felucca_dbg.home,bb_sound.core.frame,
        selected,bb_c1_live.ticket,applied,queued);
}
