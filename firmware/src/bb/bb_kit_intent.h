/* SPDX-License-Identifier: GPL-3.0-only */
/* A chosen drum kit is a main-loop intent until the exact kit+phrase request
 * that carried it ends. Pure decision helper; no audio or catalogue access. */
#ifndef BB_KIT_INTENT_H
#define BB_KIT_INTENT_H
#include "bb_player.h"
#include "bb_pattern.h"
/* 1 when the intent (valid, id) must be dropped: the drum layer's request
 * carried exactly that kit and has reached a terminal outcome (applied,
 * rejected for any reason, or cancelled). After that the screen names the
 * kit the audio owner reports, never a stale "NEXT KIT". */
static inline int bb_kit_intent_settled(const bb_player *p, int valid, unsigned id)
{
    if (!p || !valid || !p->is_drum || !p->request.ticket || !p->request.has_kit) return 0;
    if (p->request.kit != id || p->status.seen != p->request.ticket) return 0;
    return p->status.result == BB_PATTERN_APPLIED || p->status.result == BB_PATTERN_REJECTED ||
           p->status.result == BB_PATTERN_CANCELLED;
}
#endif
