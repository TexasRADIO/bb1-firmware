/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef BB_AUX_NAVIGATION_H
#define BB_AUX_NAVIGATION_H
#include <stdint.h>

/* Native integration helper. The target UI adapter owns
 * the low fourteen snapshot bits; this module never touches musical state.
 * MIX, FX and SENDS can each occur once in a bounded return chain. */
enum { AN_BASE=0, AN_MIX=1, AN_FX=2, AN_SENDS=3, AN_DEPTH=3, AN_KIND_SHIFT=14 };
#define AN_PAYLOAD_MASK UINT16_C(0x3fff)
typedef struct { uint16_t frames[AN_DEPTH];uint8_t depth; } an_return;
void an_reset(an_return *);
/* Toggle the requested auxiliary page; pop to an existing ancestor rather
 * than creating a cycle. Closing a page with no recorded return uses a BASE
 * fallback supplied by the adapter. Invalid state leaves state/out intact. */
int an_toggle(an_return *,uint16_t current,uint16_t requested,
              uint16_t fallback,uint16_t *out);
unsigned an_kind(uint16_t snapshot);

enum { AN_FX_NONE, AN_FX_TAP, AN_FX_HOLD };
enum { AN_FX_HOLD_MS=600 };
typedef struct { uint32_t since;uint8_t state; } an_fx_gesture;
/* Plain FX tap -> effects; hold -> sends. REC+FX diagnostics stays with the
 * adapter, which passes rec=1 to suppress a later tap. cancel covers STOP,
 * page changes, recovery and scene pauses. No timer/heap/IO owned here. */
unsigned an_fx_step(an_fx_gesture *,uint32_t now,int held,int pressed,
                    int rec,int cancel);
#endif
