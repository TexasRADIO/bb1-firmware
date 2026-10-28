/* SPDX-License-Identifier: GPL-3.0-only
 * Offline candidate: no device, catalogue, audio or transport ownership. */
#ifndef BB_MASTER_CONTROL_H
#define BB_MASTER_CONTROL_H
#include <stdint.h>
enum hm_instrument { HM_DRUMS, HM_DILLO, HM_TELEGRAM, HM_INSTRUMENTS };
enum hm_encoder { HM_SELECT, HM_PRESETS, HM_ALGORITHM, HM_K1, HM_K2, HM_K3, HM_K4 };
enum hm_action { HM_NONE, HM_FOCUS, HM_SOUND, HM_PATTERN, HM_LEVEL, HM_TEMPO };
typedef struct { uint8_t selected; } hm_state;
typedef struct { enum hm_action action; uint8_t instrument; int delta; } hm_intent;
/* Return 1 for a consumed HOME encoder, 0 for invalid input.
 * The caller must not forward consumed input to instrument-page handlers.
 * Intents are requests; only audio/catalogue acknowledgement changes displayed
 * playing content. Focus changes do not issue any musical command. */
int hm_turn(hm_state *state, enum hm_encoder encoder, int delta, hm_intent *out);
/* Availability is supplied by the compiled profile. Disabled slots retain
 * their physical knob positions: no shifting a level knob onto another bus. */
int hm_available(unsigned mask, unsigned instrument);
int hm_turn_available(hm_state *state, enum hm_encoder encoder, int delta,
                      unsigned mask, hm_intent *out);
#endif
