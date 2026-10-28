/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef BB_SOURCE_TRIGGER_H
#define BB_SOURCE_TRIGGER_H
#include "x0x/drum808.h"
/* Audio owner only. Explicit catalogue voice, not a track-mode selection.
 * Returns 0 without touching the engine for unsupported voices or velocity
 * outside (0,1]. Source SH uses the documented maracas adaptation.
 * Existing DSP lane sharing/chokes remain; this is not polyphony admission.
 */
int bb_drum808_source_trigger(drum808_t *,unsigned voice,float velocity);
#endif
