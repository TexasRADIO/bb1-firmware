/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef BB_FACTORY_TRIO_LAUNCH_H
#define BB_FACTORY_TRIO_LAUNCH_H
#include "bb_audio_trio.h"
#include "bb_factory_trio.h"
enum { BB_FTRIO_REQUEST_BUSY=256,BB_FTRIO_REQUEST_UNAVAILABLE,BB_FTRIO_REQUEST_OFF };
/* Single main-loop producer. inherited is a coherent caller-owned musical
 * snapshot, not a pointer into live audio or its bound staging program. Keep
 * it, choice and catalogue immutable for this call. No live core copy, heap,
 * parsing under the IRQ lock or producer-side audio/part mutation.
 *
 * Validate all eight source parts into the exclusive WRITING lease, then
 * publish one owned command. OK means ACCEPTED, not applied: use the ticket
 * with bb_audio_trio_status. The selected part becomes the bank's initial part.
 * phrase=16/32/48/64 global steps; stopped applies on the next audio consume.
 * Busy/failed requests leave sounding music and older pending banks intact.
 * Failure does not change the output ticket. Workspace/report may be changed
 * by a valid source preflight; they never certify that music was applied.
 *
 * Loads source notes, synth patches/levels and tempo/swing. Kit sounds, other
 * drums and FX inherit the explicit snapshot. Primary must already be enabled;
 * no hidden enabling or kit recall. This is complete bank replacement, not
 * pattern-only browsing, and replaces the old A-H bank on actual application.
 * GreyHound omission requires explicit THREE_ENGINE policy and visible report.
 * All input/output regions are pairwise disjoint, outside audio and its bound
 * program. Reinitialization/restore and other main producers must not overlap.
 */
int bb_factory_request_trio(bb_audio *,const bb_catalogue *,const bb_factory_choice *,
    const bb_core *inherited,unsigned primary,unsigned policy,unsigned phrase,
    bb_factory_trio_workspace *,bb_factory_trio_report *,uint32_t *ticket);
#endif
