/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef BB_AUDIO_SESSION_H
#define BB_AUDIO_SESSION_H
#include "bb_audio.h"
#include "bb_session.h"

typedef enum {
    BB_AUDIO_SESSION_OK,
    BB_AUDIO_SESSION_INVALID,
    BB_AUDIO_SESSION_BUSY,
    BB_AUDIO_SESSION_INIT_FAILED
} bb_audio_session_result;

/* Main-loop workspace, not an interrupt stack object. All arguments must
 * occupy non-overlapping storage. Native DSP is fixed at 44100 Hz. */
typedef struct { bb_core decoded, scratch; } bb_audio_session_workspace;

/* EXCLUSIVE ACCESS REQUIRED: renderer and all control producers must be
 * suspended by the platform before entry, and remain suspended until return.
 * A stopped transport is NOT exclusive access: tails normally keep rendering.
 * No internal lock or IRQ masking is attempted around this large reset.
 *
 * Busy (running, queued commands or a reserved pattern inbox) and invalid input leave the entire audio
 * object unchanged. Success replaces the scene stopped, clears all old DSP
 * tails/ramps/scope/output history, and keeps the command queue's hook/context
 * ownership. The caller must rebuild its UI mirror before accepting edits.
 * INIT_FAILED is a fail-closed exceptional condition: keep output suspended;
 * audio has been reinitialized partially, not rolled back.
 */
bb_audio_session_result bb_audio_session_restore(bb_audio *audio,
    const uint8_t *wire,size_t length,bb_audio_session_workspace *workspace);
#endif
