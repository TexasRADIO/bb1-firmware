/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef BB_LIVE_RECORD_ARR_H
#define BB_LIVE_RECORD_ARR_H
#include "bb_live_record.h"
#include "bb_arr_sources.h"
/* Stopped main-owner project edit only. The controller must first grant edit
 * ownership. Scratch must be external. Publish a NEW working part and one
 * section-edit journal entry, so arranger Undo restores the previous part.
 * No live-audio or scene-save claim; shared section rows share this edit. */
int bb_arr_record_commit(bb_arrangement *,bb_arr_sources *,uint32_t section,
                         unsigned target,const bb_live_record *,uint32_t playable,
                         bb_phrase_index *scratch);
#endif
