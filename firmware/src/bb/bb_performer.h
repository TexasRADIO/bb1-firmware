/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef BB_PERFORMER_H
#define BB_PERFORMER_H
#include "bb_factory_trio_launch.h"
enum {BB_PERFORMER_NO_BANK=512};
typedef struct {
    bb_factory_choice choice;
    uint32_t ticket;
    uint8_t capabilities;
} bb_performer_selection;
/* Main-loop HOME controller for complete ERA banks, not individual patterns.
 * Immutable catalogue/audio lifetimes must span the controller. Reset after
 * full audio initialization or restore; do not transfer between audio owners.
 * CHOOSE=view, REQUEST=request.ticket, QUEUED=status.bank.queued,
 * PLAYING=playing only when its ticket equals status.bank.applied.
 * Source selection records the initial section; current A-H is status.active.
 * Missing GreyHound is never silently waived: policy is explicit per request
 * and accepted capabilities stay with request/playing for visible labelling.
 * No workspace/program allocation, DSP calls or rendering in this controller. */
typedef struct {
    bb_browser view;
    bb_performer_selection request,playing;
    bb_performance_status status;
    uint16_t error;
    uint8_t primary,ready;
} bb_performer;
int bb_performer_init(bb_performer *,const bb_browser *validated_era_view,unsigned primary);
int bb_performer_sync(bb_performer *,bb_audio *);
int bb_performer_bank_key(bb_performer *,bb_audio *,unsigned key,const bb_core *coherent_snapshot,
    unsigned policy,unsigned phrase,bb_factory_trio_workspace *,bb_factory_trio_report *);
int bb_performer_move(bb_performer *,bb_audio *,int delta,const bb_core *coherent_snapshot,
    unsigned policy,unsigned phrase,bb_factory_trio_workspace *,bb_factory_trio_report *);
/* Cursor-only page movement; no audio access or launch. */
int bb_performer_page(bb_performer *,int delta);
/* FIFO acceptance only. Does not reinterpret a section as a source-bank load.
 * Existing queued replacement refuses this action instead of cancelling it. */
int bb_performer_part_key(bb_performer *,bb_audio *,unsigned key);
/* 0=no owned pending bank, 1=accepted/not consumed, 2=queued by audio. */
unsigned bb_performer_pending(const bb_performer *);
#endif
