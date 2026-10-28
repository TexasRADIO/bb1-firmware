/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once
#include "bb_performer.h"
#include "bb_panel.h"
/* Main-loop-only HOME service. Fixed owned scratch, no heap or extra bank.
 * Caller serializes with other producers/restores; snapshot must be coherent
 * and outside live audio/program/scratch. Reset on every audio lifetime change.
 * Successful reset opens a cursor only: never loads or starts music.
 * These functions are retained for link-cost evaluation; not yet UI-wired. */
int bb_home_reset(const bb_browser *,unsigned primary);
int bb_home_sync(bb_audio *);
const bb_performer *bb_home_view(void);
int bb_home_bank_key(bb_audio *,unsigned,const bb_core *,unsigned policy,unsigned phrase);
int bb_home_move(bb_audio *,int,const bb_core *,unsigned policy,unsigned phrase);
int bb_home_part_key(bb_audio *,unsigned);
int bb_home_page(int delta);
/* Coherent full musical mirror after bank/part application, only with empty
 * FIFO. Preserve UI selection/page/edit and clamp step-page bounds. One
 * bounded copy under the target queue guard; no DSP or source parsing. */
int bb_home_sync_panel(bb_panel *,bb_audio *);

/* Main loop only, immediately after a bank request returns CONTENT.
 * Summarizes existing report bits; does not reparse, accept, or launch data.
 * Never call for other results, whose report may be unchanged. */
const char *bb_home_content_reason(void);
