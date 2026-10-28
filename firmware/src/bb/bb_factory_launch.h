#ifndef BB_FACTORY_LAUNCH_H
#define BB_FACTORY_LAUNCH_H
#include "bb_audio.h"
#include "bb_factory_pattern.h"
/* Main-loop producer only. Decode immutable source outside the audio ISR and
 * enqueue a complete copied pattern. OK means accepted request, not playing.
 * Use the returned ticket with bb_audio_pattern_status for actual acknowledgement.
 * Busy/unsupported/invalid leaves core, old pending music and ticket unchanged.
 * This is instrument-pattern recall, not a whole-band scene launch. */
int bb_factory_request_synth_pattern(bb_audio *,const bb_catalogue *,const bb_factory_choice *,
                                     unsigned synth,unsigned phrase,uint32_t *ticket);
#endif
