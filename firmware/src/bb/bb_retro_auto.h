/* SPDX-License-Identifier: GPL-3.0-only
 * BB1 2.3: Always-on retrospective capture.
 *
 * Wraps bb_retro_take to provide "always-on" background recording:
 * - Capture starts at boot, no user action required
 * - The last completed loop is always available for recovery
 * - A "Retro" indicator appears when a recoverable loop exists
 * - The Conductor UI offers: claim → preview → commit or discard
 *
 * Inspired by Skiff 2.5: record while stopped, propose tempo/downbeat.
 *
 * Copyright (C) 2026 Big Beat Project */
#ifndef BB_RETRO_AUTO_H
#define BB_RETRO_AUTO_H

#include "bb_retro_take.h"

#if BB_WITH_RETRO_TAKE

/* State for the always-on wrapper */
typedef struct {
    bb_retro_take take;
    bb_retro_snapshot last_snapshot;
    uint8_t has_loop;        /* a completed loop is available */
    uint8_t previewing;      /* the UI is previewing the claimed take */
    uint8_t committed;       /* the take was committed to the pattern */
    uint32_t capture_count;  /* total loops captured since boot */
} bb_retro_auto;

/* Initialize at boot. Sets active=1 so capture begins immediately. */
void bb_retro_auto_init(bb_retro_auto *r);

/* Called from the audio boundary (same place as bb_retro_take_observe).
 * Keeps the retro system fed with every loop. */
int bb_retro_auto_observe(bb_retro_auto *r, const bb_retro_owner *owner);

/* Called from the audio hit path (same place as bb_retro_take_hit). */
int bb_retro_auto_hit(bb_retro_auto *r, const bb_retro_owner *owner,
                      uint16_t event);

/* UI query: is there a recoverable loop? */
static inline int bb_retro_auto_available(const bb_retro_auto *r)
{
    return r && r->has_loop && !r->previewing;
}

/* UI: begin preview (claim the take, mute the drum part, play the take).
 * Returns BB_RETRO_OK or an error. The caller should mute the original
 * pattern and play the take events instead. */
int bb_retro_auto_preview(bb_retro_auto *r, bb_retro_snapshot *snap);

/* UI: read the previewed take's events into a buffer. */
int bb_retro_auto_read(bb_retro_auto *r, uint16_t *events);

/* UI: commit the take to the pattern (caller writes the events to the
 * phrase). Returns the event count, or 0 if nothing to commit. */
int bb_retro_auto_commit(bb_retro_auto *r);

/* UI: discard the preview and resume normal playback. */
int bb_retro_auto_discard(bb_retro_auto *r);

#endif /* BB_WITH_RETRO_TAKE */
#endif
