/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once
#include <stddef.h>
#include <stdint.h>
/* Caller owns aligned storage and excludes control changes during rendering.
 * Eleven voices: BD SD LT MT HT RS CP CH OH CR RD. Hats/cymbals retain the
 * original 909 sample path; the seven other voices use the circuit model.
 * Exact 44.1 kHz, 1..128 frames. No allocation or shared mutable render state.
 */
size_t bb909_size(void);
size_t bb909_alignment(void);
int bb909_init(void *, size_t capacity, unsigned rate);
/* Validated immutable image is read in place; caller keeps it alive.
 * CRC and layout checking occur here before audio, never inside render. */
int bb909_init_pack(void *, size_t capacity, unsigned rate, const void *image, size_t image_bytes);
int bb909_set(void *, unsigned voice, unsigned parameter, unsigned value);
int bb909_trigger(void *, unsigned voice, unsigned velocity);
int bb909_active(const void *);
/* Buses are overwritten, distinct and outside state. right=NULL means mono.
 * Echo precedes spring in this API. A nonfinite render rejects the private
 * transaction; caller must discard its advanced state on failure. */
int bb909_render(void *, float *left, float *right, float *echo, float *spring, unsigned frames);

#include "group_gain.h"
int bb909_render_groups(void *,gg_state *,float *,float *,float *,float *,unsigned);
