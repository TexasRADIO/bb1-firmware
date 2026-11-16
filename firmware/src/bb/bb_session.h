/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef BB_SESSION_H
#define BB_SESSION_H
#include "bb_core.h"

/* Version 1: explicit little-endian fields, not a native structure dump.
 * One scene is smaller than Felucca's 3840-byte journal payload. This codec
 * does NOT write flash, choose a flash object, or install state into audio.
 * Main/control thread only, with stable, non-overlapping buffers. The caller
 * must acquire a coherent snapshot; never serialize a live IRQ-owned core.
 */
#define BB_SESSION_VERSION 1u /* legacy whole-percent writer */
#define BB_SESSION_FRACTIONAL_VERSION 2u
#define BB_SESSION_BYTES 2274u

/* Shared musical-state validation, without encoding, allocation or writes.
 * Runtime/UI fields are outside the session format and ignored here. */
int bb_session_state_valid(const bb_core *scene);

/* Return 1 on success. Invalid musical state or short capacity leaves dst
 * untouched. Runtime clocks, DSP state and UI navigation are not saved. */
int bb_session_encode(const bb_core *scene, uint8_t *dst, size_t capacity);

/* Caller-owned scratch avoids a large embedded stack frame. scratch and out
 * must be different objects, neither overlapping src. scratch is disposable;
 * out is byte-for-byte unchanged on failure. On success out is initialized
 * at the requested device sample rate, stopped at step zero with default UI.
 * Applying it to a running audio engine requires a separate safe reset/swap,
 * including DSP/cache invalidation. This function is NOT that live operation.
 */
int bb_session_decode(const uint8_t *src, size_t length, uint32_t sample_rate,
                      bb_core *out, bb_core *scratch);
#endif
