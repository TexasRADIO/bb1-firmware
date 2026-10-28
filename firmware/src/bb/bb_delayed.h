/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef BB_DELAYED_EXPERIMENT_H
#define BB_DELAYED_EXPERIMENT_H
#include <stdint.h>
#define BB_DELAYED_CAPACITY 16u
typedef struct {uint64_t frame;uint32_t payload,generation;} bb_delayed_event;
typedef struct {
    bb_delayed_event items[BB_DELAYED_CAPACITY];
    uint64_t floor;
    uint32_t generation;
    uint8_t count,active;
} bb_delayed;
enum {BB_DELAYED_OK,BB_DELAYED_WAIT,BB_DELAYED_INVALID,BB_DELAYED_FULL,
      BB_DELAYED_STALE,BB_DELAYED_LATE};
/* Audio-owner-only prototype. No locks, allocation, DSP or timing expansion.
 * Payload is opaque: compact event/source ordinal/strike identity may live here.
 * Increasing generation is required; exhausted uint32 range requires a stopped
 * owner reset, not silent wrap. Begin cancels prior pending strikes. */
int bb_delayed_begin(bb_delayed *,uint32_t generation,uint64_t frame);
void bb_delayed_stop(bb_delayed *);
/* Whole batch accepted or unchanged; equal-frame entries preserve offer order. */
int bb_delayed_offer(bb_delayed *,uint64_t now,const bb_delayed_event *,unsigned);
/* Peek supplies the next render boundary. Pop never silently dispatches late. */
int bb_delayed_peek(const bb_delayed *,bb_delayed_event *);
int bb_delayed_pop(bb_delayed *,uint64_t frame,bb_delayed_event *);
#endif
