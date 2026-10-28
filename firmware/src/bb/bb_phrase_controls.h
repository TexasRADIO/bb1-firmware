/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef BB_PHRASE_CONTROLS_H
#define BB_PHRASE_CONTROLS_H
#include "bb_phrase.h"
typedef struct {uint64_t accents;} bb_phrase_controls;
/* Prepare away from audio dispatch. Transactional refusal; original ordinals,
 * control events and modifiers remain in the immutable phrase. Reprepare on
 * every edit/replacement and transfer phrase+controls together to audio owner.
 * Accent controls with modifiers are refused, not silently simplified.
 */
int bb_phrase_controls_prepare(const bb_phrase *,bb_phrase_controls *);
/* 0 invalid, 1 sound event, 2 accent control (do not send to drum renderer).
 * Not engine admission: unsupported sound identities are preserved.
 */
int bb_phrase_resolve(const bb_phrase *,const bb_phrase_controls *,unsigned,
                     bb_phrase_event *);
#endif
