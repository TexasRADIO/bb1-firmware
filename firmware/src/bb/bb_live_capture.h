/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef BB_LIVE_CAPTURE_H
#define BB_LIVE_CAPTURE_H
#include <stdint.h>
#ifndef BB_WITH_LIVE_CAPTURE
#define BB_WITH_LIVE_CAPTURE 0
#endif
enum {BB_CAPTURE_CAPACITY=32};
enum {BB_CAPTURE_OK,BB_CAPTURE_INVALID,BB_CAPTURE_BUSY,BB_CAPTURE_EMPTY,
      BB_CAPTURE_OVERFLOW,BB_CAPTURE_CHANGED,BB_CAPTURE_CLOSED,BB_CAPTURE_FAULT};
/* Accepted manual hits only, stamped by the audio owner. Raw velocity is
 * retained here; the current phrase recorder still supports accent classes
 * only. This transient ring is not a project/scene encoding. */
typedef struct {uint64_t frame;uint32_t epoch;uint8_t voice,velocity,target,kit;} bb_capture_event;
typedef struct {
 uint64_t frame;
 uint32_t phase,ticks,sample_rate,epoch,applied,transport_epoch,source_phase;
 uint16_t bpm;uint8_t target,kit,source_step,lattice,length,swing,source_odd;
} bb_capture_clock;
typedef struct {
 bb_capture_event event[BB_CAPTURE_CAPACITY];bb_capture_clock clock;
 uint32_t lost;uint8_t head,count,armed,error,valid;
} bb_live_capture;
#if BB_WITH_LIVE_CAPTURE
struct bb_audio;
/* Main loop, existing bounded IRQ guard. Begin requires an idle command
 * queue, running rich source, stable tempo/swing and no unconsumed pass. No transport, pattern,
 * flash or gain writes. Caller must establish any count-in/grid policy. */
int bb_audio_capture_begin(struct bb_audio *,unsigned,bb_capture_clock *);
/* One event at a time. Overflow/source/tempo/restart/fault invalidates the
 * complete pass: no partial successful read once that error is observed.
 * STOP/end permits remaining accepted events to drain, then CLOSED. A caller
 * must observe a successful end and drain to CLOSED without any error before
 * publishing a complete take; prior read events are provisional. End returns
 * BUSY while queued input remains, so an earlier pad is not silently omitted. */
int bb_audio_capture_read(struct bb_audio *,bb_capture_event *);
int bb_audio_capture_end(struct bb_audio *);
/* Explicit discard is required before starting another pass. */
int bb_audio_capture_discard(struct bb_audio *);
#endif
#endif
