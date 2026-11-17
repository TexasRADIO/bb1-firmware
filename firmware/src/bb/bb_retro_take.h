/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef BB_RETRO_TAKE_H
#define BB_RETRO_TAKE_H

#include "bb_live_capture.h"
#include <stddef.h>
#include <stdint.h>

#ifndef BB_WITH_RETRO_TAKE
#define BB_WITH_RETRO_TAKE 0
#endif

/* A bounded, transient last-loop recorder for the currently focused drum
 * part.  It is deliberately separate from bb_live_capture: the latter is an
 * armed pass, while this owner continuously retains one completed loop. */
enum { BB_RETRO_BANKS = 2, BB_RETRO_CAPACITY = BB_CAPTURE_CAPACITY };
enum {
    BB_RETRO_OK,
    BB_RETRO_INVALID,
    BB_RETRO_EMPTY,
    BB_RETRO_BUSY,
    BB_RETRO_OVERFLOW,
    BB_RETRO_CHANGED,
    BB_RETRO_CLOSED
};

/* loop is an audio-owner serial incremented exactly when the acknowledged
 * rich phrase wraps.  All remaining fields are immutable for that loop. */
typedef struct {
    uint32_t source_epoch, applied, transport_epoch, loop, sample_rate;
    uint16_t bpm;
    uint8_t target, kit, lattice, length, swing;
} bb_retro_owner;

typedef struct {
    bb_retro_owner owner;
    uint8_t count;
} bb_retro_snapshot;

typedef struct {
    uint16_t event[BB_RETRO_CAPACITY]; /* voice5 | zero-based cell6 | accent2 */
    bb_retro_owner owner;
    uint8_t count, valid, overflow;
} bb_retro_bank;

typedef struct {
    bb_retro_bank bank[BB_RETRO_BANKS];
    uint32_t overwritten, refused;
    uint8_t current, ready, claimed, cursor, active;
} bb_retro_take;

/* Boot/test reset.  Production callers must hold the existing audio command
 * guard around every entry point; this object does not add another lock. */
void bb_retro_take_reset(bb_retro_take *);

/* Observe the focused source at an audio-owned boundary.  A consecutive loop
 * serial publishes the loop just completed.  Identity changes and skipped
 * serials invalidate unpublished history rather than guessing. */
int bb_retro_take_observe(bb_retro_take *, const bb_retro_owner *);

/* Resolve the exact nearest cell from the audio owner's current step/phase,
 * then add that accepted manual hit to the current loop. */
int bb_retro_take_cell(const bb_retro_owner *,unsigned step,uint32_t phase,
                       uint32_t denominator,unsigned voice,unsigned accented,
                       uint16_t *);
int bb_retro_take_hit(bb_retro_take *, const bb_retro_owner *, uint16_t event);

/* Main-loop claim/read/release.  A claim pins its bank until release.  If a
 * second loop completes while pinned, that newer loop is refused atomically;
 * the claimed take remains byte-identical and readable. */
int bb_retro_take_claim(bb_retro_take *, bb_retro_snapshot *);
int bb_retro_take_read(bb_retro_take *, uint16_t *);
int bb_retro_take_release(bb_retro_take *);

/* Explicit resource evidence for release gates and UI trade-off decisions. */
size_t bb_retro_take_ram_bytes(void);

#endif
