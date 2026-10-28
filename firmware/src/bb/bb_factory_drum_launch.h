#ifndef BB_FACTORY_DRUM_LAUNCH_H
#define BB_FACTORY_DRUM_LAUNCH_H
#include "bb_audio.h"
#if BB_WITH_RICH_DRUM_RENDER
/* Main/control only; explicit source SH -> independent maracas-based shaker.
 * Semantic preparation is separate from engine/timing admission. Not routed
 * from public browsers until rich edit/save/target integration is qualified. */
int bb_factory_prepare_rich_drum(const bb_catalogue *,const bb_factory_choice *,bb_phrase_index *);
int bb_factory_request_rich_drum(bb_audio *,const bb_catalogue *,const bb_factory_choice *,unsigned,unsigned,uint32_t *);
#endif
#include "bb_factory_drum.h"
/* Main-loop source decoder and copied producer. OK means accepted request;
 * inspect bb_audio_drum_pattern_status for actual phrase application and kit
 * compatibility. Never reads or writes the live core from the main loop. */
int bb_factory_request_drum_pattern(bb_audio *,const bb_catalogue *,const bb_factory_choice *,
                                   unsigned machine,unsigned phrase,uint32_t *ticket);
#endif
