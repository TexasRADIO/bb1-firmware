/* SPDX-License-Identifier: GPL-3.0-only */
/* CPU0 callback boundary. Platform binding remains mandatory. No fallback
 * after ownership acceptance/failure: an owned engine is never rendered or
 * mutated through the legacy CPU0 arrays. Each handler returns 1 handled,
 * 0 unowned, -1 failed; block/span lifecycle returns boolean success. */
#ifndef BB_OWNER_AUDIO_BRIDGE_H
#define BB_OWNER_AUDIO_BRIDGE_H
int bb_owner_audio_block_begin(bb_audio *);
int bb_owner_audio_hit(bb_audio *,const bb_hit *);
int bb_owner_audio_sync(bb_audio *,unsigned);
int bb_owner_audio_group(bb_audio *,const bb_command *);
int bb_owner_audio_span_begin(bb_audio *,unsigned);
int bb_owner_audio_span_collect(bb_audio *,unsigned);
int bb_owner_audio_mix(bb_audio *,unsigned,const float *,float,unsigned);
int bb_owner_audio_span_release(bb_audio *);
int bb_owner_audio_block_end(bb_audio *);
int bb_owner_audio_local(bb_audio *,unsigned);
#endif
