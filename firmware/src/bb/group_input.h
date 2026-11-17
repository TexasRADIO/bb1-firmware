/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef BB_GROUP_INPUT_H
#define BB_GROUP_INPUT_H
#include "bb_control.h"
#include "group_gain.h"
typedef struct {
    uint8_t level[GG_COUNT];
    uint8_t muted, pending;
} bb_group_view;
enum { BB_GROUP_READ, BB_GROUP_TURN, BB_GROUP_TOGGLE };
enum { BB_GROUP_REFUSED, BB_GROUP_UNCHANGED, BB_GROUP_QUEUED };
/* CPU0 main-loop only. The matching bounded IRQ hooks protect CPU0 audio
 * ownership plus its FIFO, not a cross-core shared gg_state. Pass the live
 * CPU0 bank (not worker scratch) after its startup binding; NULL is refused.
 * No persistent mirror, allocation, playback start or kit enable occurs.
 * The view includes pending requests; it is not an audio acknowledgement.
 * READ and edits refuse pending/inflight STOP; leave output unchanged on
 * refusal. A full FIFO still permits a read or a no-change bounded turn. */
int bb_group_input(bb_commands *,const gg_state *,unsigned machine,
                   unsigned action,unsigned group,int delta,bb_group_view *);
#endif
