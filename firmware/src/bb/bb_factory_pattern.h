#ifndef BB_FACTORY_PATTERN_H
#define BB_FACTORY_PATTERN_H
#include "bb_browser.h"
#include "bb_core.h"
#include "bb_pattern.h"
enum { BB_FACTORY_PATTERN_OK, BB_FACTORY_PATTERN_INVALID, BB_FACTORY_PATTERN_LENGTH,
       BB_FACTORY_PATTERN_EXPRESSION, BB_FACTORY_PATTERN_FIELD,
       BB_FACTORY_PATTERN_BUSY, BB_FACTORY_PATTERN_UNAVAILABLE };
/* Preferred producer path: decode directly to caller-owned pattern data,
 * without copying or reading a live core. Failure leaves out unchanged. */
int bb_factory_prepare_synth_data(const bb_catalogue *,const bb_factory_choice *,
                                 unsigned synth,bb_pattern_data *out);
/* Caller-owned staging only, never a live IRQ-owned core. Pattern-only recall
 * preserves sounds, levels, mute, transport, clock, UI and other instruments.
 * All source note fields are checked; unsupported expressive fields fail
 * atomically instead of being silently reduced to legacy on/off events.
 * This is not a whole-band experience loader, nor a live publication API. */
int bb_factory_prepare_synth_pattern(const bb_catalogue *, const bb_factory_choice *,
                                    unsigned synth, bb_core *staging);
#endif
