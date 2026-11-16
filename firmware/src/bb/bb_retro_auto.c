/* SPDX-License-Identifier: GPL-3.0-only
 * BB1 2.3: Always-on retrospective capture.
 *
 * Copyright (C) 2026 Big Beat Project */
#include "bb_retro_auto.h"

#if BB_WITH_RETRO_TAKE

void bb_retro_auto_init(bb_retro_auto *r)
{
    if (!r) return;
    bb_retro_take_reset(&r->take);
    r->take.active = 1;  /* always on from boot */
    r->has_loop = 0;
    r->previewing = 0;
    r->committed = 0;
    r->capture_count = 0;
    r->last_snapshot.count = 0;
}

int bb_retro_auto_observe(bb_retro_auto *r, const bb_retro_owner *owner)
{
    int result;
    if (!r) return BB_RETRO_INVALID;
    result = bb_retro_take_observe(&r->take, owner);
    /* Check if a new loop completed */
    if (r->take.ready && !r->previewing) {
        r->has_loop = 1;
        r->capture_count++;
    }
    return result;
}

int bb_retro_auto_hit(bb_retro_auto *r, const bb_retro_owner *owner,
                      uint16_t event)
{
    if (!r) return BB_RETRO_INVALID;
    return bb_retro_take_hit(&r->take, owner, event);
}

int bb_retro_auto_preview(bb_retro_auto *r, bb_retro_snapshot *snap)
{
    int result;
    if (!r || !r->has_loop) return BB_RETRO_EMPTY;
    result = bb_retro_take_claim(&r->take, snap);
    if (result == BB_RETRO_OK) {
        r->previewing = 1;
        r->committed = 0;
        r->last_snapshot = *snap;
    }
    return result;
}

int bb_retro_auto_read(bb_retro_auto *r, uint16_t *events)
{
    if (!r || !r->previewing) return BB_RETRO_INVALID;
    return bb_retro_take_read(&r->take, events);
}

int bb_retro_auto_commit(bb_retro_auto *r)
{
    if (!r || !r->previewing) return 0;
    r->committed = 1;
    r->previewing = 0;
    r->has_loop = 0;
    bb_retro_take_release(&r->take);
    return (int)r->last_snapshot.count;
}

int bb_retro_auto_discard(bb_retro_auto *r)
{
    if (!r || !r->previewing) return BB_RETRO_INVALID;
    r->previewing = 0;
    /* has_loop stays true — the loop is still there for another try */
    bb_retro_take_release(&r->take);
    return BB_RETRO_OK;
}

#endif /* BB_WITH_RETRO_TAKE */
