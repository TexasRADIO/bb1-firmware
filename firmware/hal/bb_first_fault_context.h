/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef BB_FIRST_FAULT_CONTEXT_H
#define BB_FIRST_FAULT_CONTEXT_H
#include <stdint.h>
/* CPU0-owned diagnostic. Capture only after the worker has been held.
 * Main-loop phase is the interrupted activity, not proof of causation.
 * No clocks, allocation, strings, payload scans, reset command or replay. */
typedef struct {
    volatile uint32_t valid; uint32_t reason, stage, page, home;
    uint32_t frame_low, frame_high, selected, ticket, applied, queued;
} bb_first_fault_context;
static inline void bb_first_fault_capture(bb_first_fault_context *out,
    uint32_t reason,uint32_t stage,uint32_t page,uint32_t home,
    uint64_t frame,uint32_t selected,uint32_t ticket,uint32_t applied,uint32_t queued) {
    if(out->valid)return;
    out->reason=reason;out->stage=stage;out->page=page;out->home=home;
    out->frame_low=(uint32_t)frame;out->frame_high=(uint32_t)(frame>>32);
    out->selected=selected;out->ticket=ticket;out->applied=applied;out->queued=queued;
    __asm__ volatile("" ::: "memory"); /* publish data before valid on CPU0 */
    out->valid=1;
}
#endif
