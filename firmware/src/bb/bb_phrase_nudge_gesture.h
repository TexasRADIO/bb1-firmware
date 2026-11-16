/* SPDX-License-Identifier: GPL-3.0-only */
/* Main-loop gesture transaction for rich-phrase microtiming.  It deliberately
 * does not publish to audio or mutate an Undo journal.  A caller begins from
 * one exact immutable owner, applies any number of encoder detents locally,
 * then receives at most one before/after transaction on release. */
#ifndef BB_PHRASE_NUDGE_GESTURE_H
#define BB_PHRASE_NUDGE_GESTURE_H
#include "bb_phrase_nudge.h"

enum {
    BB_NUDGE_GESTURE_INVALID,
    BB_NUDGE_GESTURE_UNCHANGED,
    BB_NUDGE_GESTURE_CHANGED,
    BB_NUDGE_GESTURE_STALE
};

typedef struct {
    bb_phrase_nudges before, after;
    uint32_t source_ticket, source_epoch;
    uint8_t machine, voice, step, reserved;
} bb_phrase_nudge_transaction;

typedef struct {
    bb_phrase_nudge_transaction transaction;
    int8_t value;
    uint8_t active, changed;
} bb_phrase_nudge_gesture;

/* Success starts a new gesture and snapshots the exact source sidecar.  Invalid
 * input leaves an existing gesture byte-identical. */
int bb_phrase_nudge_gesture_begin(bb_phrase_nudge_gesture *,const bb_phrase *,
                                  const bb_phrase_nudges *,uint32_t ticket,
                                  uint32_t epoch,unsigned machine,
                                  unsigned voice,unsigned step);
/* Apply all detents to the private transaction.  Values saturate at -8/+7.
 * Queue-capacity or source validation failure leaves the gesture unchanged. */
int bb_phrase_nudge_gesture_turn(bb_phrase_nudge_gesture *,const bb_phrase *,int delta);
/* Release the gesture.  An exact owner produces one transaction only when its
 * final sidecar differs from its opening snapshot.  A changed owner cancels
 * the gesture and returns STALE.  Non-CHANGED results leave out untouched. */
int bb_phrase_nudge_gesture_finish(bb_phrase_nudge_gesture *,uint32_t ticket,
                                   uint32_t epoch,bb_phrase_nudge_transaction *out);
void bb_phrase_nudge_gesture_cancel(bb_phrase_nudge_gesture *);

#endif
