/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef BB_PROPOSAL_PREPARED_BANK_SERVICE_H
#define BB_PROPOSAL_PREPARED_BANK_SERVICE_H
#include "prepared_bank.h"
#include "bb_control.h"
enum {
    PB_SERVICE_INVALID=-1, PB_SERVICE_NONE=0, PB_SERVICE_REFILLED=1,
    PB_SERVICE_PART_READY=2, PB_SERVICE_DISCARDED=3, PB_SERVICE_FAILED=4,
    PB_SERVICE_SEEDED=5
};
/* Offline candidate for a single main-loop producer, not target integration.
 * All callbacks are required. allow runs INSIDE the short metadata guard and
 * must only inspect admission flags: session/asset/kit holds, reset, etc.
 * The callback context must outlive this call and not alias proposal storage.
 * Never call from the audio ISR, re-enter, or run concurrently with another
 * main producer. Reset and save/load still need the independent ownership gate.
 *
 * One call prepares at most ONE inactive section, refills ONE bank slot, or
 * copies ONE compact seed into an inactive first-use slot.
 * No loop, allocation, live-core write, transport start, wait, or DSP work.
 * All decoding/checksum/packing runs outside the guard. A claimed operation
 * ALWAYS finishes its lease, even if an intervening STOP/hold cancels it.
 * Bounded work count is not a measured FM-1 latency/deadline guarantee.
 * Result describes the finish at its guard, not the state after interrupts
 * resume. Read coherent status separately; PART_READY never means applied.
 */
int pb_service_one(prepared_bank *,unsigned sample_rate,
    bb_enter_fn enter,bb_leave_fn leave,int (*allow)(void *),void *context);
/* Native adapters commonly keep the IRQ-lock context separate from the
 * admission flags inspected by allow. The original single-context entry
 * remains as a compatibility wrapper for the verified proposal tests. */
int pb_service_one_split(prepared_bank *,unsigned sample_rate,
    bb_enter_fn enter,bb_leave_fn leave,void *lock_context,
    int (*allow)(void *),void *allow_context);
#endif
