/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once
#include "bb_factory_drum.h"
#include "bb909_owner.h"
/* These lanes are 909 engine identities, NOT the 808 portable sound schema.
 * Do not load this grid into an 808 layer or serialize it as an 808 sound.
 * Family identity and revision must travel with a future live inbox. */
enum {BB909_SEQ_BD,BB909_SEQ_SD,BB909_SEQ_LT,BB909_SEQ_MT,BB909_SEQ_HT,
      BB909_SEQ_RS,BB909_SEQ_CP,BB909_SEQ_CH,BB909_SEQ_OH,BB909_SEQ_CR,BB909_SEQ_RD,BB909_SEQ_VOICES};
const char *bb909_sequence_voice_name(unsigned);
unsigned bb909_sequence_project(const bb_factory_drum_data *,bb_factory_drum_grid *);
enum {BB909_SEQ_REJECTED,BB909_SEQ_APPENDED,BB909_SEQ_OTHER_LAYER};
/* Audio-consumer-only request construction, before publication to CPU1.
 * Never append to an in-flight request. Failures preserve the request.
 * 128-frame bound is the complete block, not one independently reset span.
 * Overflow rejects; no silent truncation or delayed note substitution. */
int bb909_sequence_begin(bb909_request *,uint32_t ticket,unsigned frames);
int bb909_sequence_hit(bb909_request *,unsigned selected_layer,uint64_t block_start,const bb_hit *);
