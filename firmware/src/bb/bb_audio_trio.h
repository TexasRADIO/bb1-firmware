/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef BB_AUDIO_TRIO_H
#define BB_AUDIO_TRIO_H
#include "bb_audio.h"
#if BB_WITH_PREPARED_BANK
#include "prepared_bank_service.h"
#endif
/* One main-loop producer. begin grants WRITING-only access to the fixed owned
 * program so the target needs no second 6KB staging bank. Audio cannot read a
 * WRITING lease. Fill/decode/validate outside IRQ masking; stop writing before
 * publish. Abort only your matching WRITING lease. Rejected publish releases
 * that lease, not earlier music. No concurrent main producers or allocation.
 * out/ticket/status must be outside the audio owner AND its bound program,
 * and out/ticket must not overlap each other.
 * Publication acceptance is not application: read seen/queued/applied status. */
int bb_audio_trio_begin(bb_audio *,bb_trio_program **out,uint32_t *ticket);
/* Optional target profile: bind once after initialization, while stopped with
 * an empty FIFO and FREE slot. Buffer must be outside the audio owner. Its
 * earlier user (e.g. CPU0 boot reference) must have permanently released it.
 * No allocation, fallback or automatic lifetime proof. Session restore keeps
 * this binding; full initialization clears it. Internal/disabled builds reject.
 * Caller must supply IRQ hooks on target and suspend other main producers. */
int bb_audio_trio_bind(bb_audio *,bb_trio_program *);
int bb_audio_trio_abort(bb_audio *,uint32_t ticket);
int bb_audio_trio_publish(bb_audio *,uint32_t ticket,unsigned launch_phrase);
int bb_audio_trio_status(bb_audio *,bb_trio_status *);
/* Coherent HOME/performance readout, copied under one short queue guard.
 * Cursor choices and publication acceptance are not applied-bank labels.
 * bank.applied identifies the last applied source bank in this audio lifetime;
 * ready does not certify eight distinct authored variations. No catalogue,
 * DSP, waits or allocation occur here. Reset label mappings on audio restore.
 * Same caller-owned/disjoint output and target IRQ-hook rules as trio_status.
 * Disabled builds return0 without changing out. */
typedef struct {
    bb_trio_status bank;
    uint64_t frame;
    uint32_t part_launches;
    uint16_t bpm;
    uint8_t active,queued,position,phrase_steps,ready,result,error,running;
    uint8_t level[BB_MIX_CHANNELS];
} bb_performance_status;
int bb_audio_performance_status(bb_audio *,bb_performance_status *out);
/* Request A-H in the existing ready bank. Returns FIFO acceptance only;
 * use the coherent status for actual/queued position. Never creates eight
 * copies, starts transport or cancels a pending bank replacement. Refuses
 * missing bank, occupied bank lease, invalid part or invalid/full FIFO.
 * Main-loop single producer; consumer still owns phrase-boundary application. */
int bb_audio_part_request(bb_audio *,unsigned part);
#if BB_WITH_PREPARED_BANK
/* Main-loop only. Performs at most one refill or inactive-section expansion.
 * Admission is rechecked inside the same IRQ guard as the metadata claim.
 * Audio publication remains on the audio owner. */
int bb_audio_bank_service(bb_audio *,int (*allow)(void *),void *allow_context);
#endif
/* Audio owner only, outside the queue's short locks. */
int bb_audio_trio_command(bb_audio *,const bb_command *);
int bb_audio_trio_tick(bb_audio *);
void bb_audio_trio_cancel(bb_audio *);
void bb_audio_trio_transport(bb_audio *);
#endif
