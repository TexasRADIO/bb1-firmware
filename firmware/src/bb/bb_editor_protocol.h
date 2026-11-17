/* SPDX-License-Identifier: GPL-3.0-only
 * BB1 editor protocol bindings.
 *
 * Wire IDs come only from the generated shared protocol header. The field
 * IDs below are the BB1 adapter contract, not firmware command enum values.
 */
#ifndef BB_EDITOR_PROTOCOL_H
#define BB_EDITOR_PROTOCOL_H

#include <stdint.h>
#include "bb_editor_gen.h"

#define BB_ED_SYSEX_FAMILY BB_ED_FAMILY

#define BB_ED_FIELD_BPM             0x0001u
#define BB_ED_FIELD_SWING_X2        0x0002u
#define BB_ED_FIELD_RUNNING         0x0010u
#define BB_ED_FIELD_PAGE            0x0011u
#define BB_ED_FIELD_MACHINE         0x0012u
#define BB_ED_FIELD_VOICE           0x0013u

/* Drum A and B share field IDs; their scope selects the machine. */
#define BB_ED_FIELD_DRUM_MUTED      0x0100u
#define BB_ED_FIELD_DRUM_LEVEL      0x0101u
#define BB_ED_FIELD_DRUM_LENGTH     0x0102u
#define BB_ED_FIELD_DRUM_B_MUTED    0x0200u
#define BB_ED_FIELD_DRUM_B_LEVEL    0x0201u
#define BB_ED_FIELD_DRUM_B_LENGTH   0x0202u

#define BB_ED_FIELD_DILLO_CUTOFF    0x0300u
#define BB_ED_FIELD_DILLO_RESONANCE 0x0301u
#define BB_ED_FIELD_DILLO_WAVE      0x0302u
#define BB_ED_FIELD_DILLO_ENVELOPE  0x0303u
#define BB_ED_FIELD_DILLO_DECAY     0x0304u
#define BB_ED_FIELD_DILLO_ACCENT    0x0305u
#define BB_ED_FIELD_DILLO_DRIVE     0x0306u
#define BB_ED_FIELD_DILLO_SLIDE_MS  0x0307u
#define BB_ED_FIELD_DILLO_ATTACK_MS 0x0308u
#define BB_ED_FIELD_DILLO_TRACKING  0x0309u

/* The 15 portable bb_fx_settings slots use 0x0400 + bb_fx_parameter. */
#define BB_ED_FIELD_FX_BASE         0x0400u
/* Extended 2.3 settings remain snapshot/read-only until they have a proper
 * audio-owner command. The editor must never mutate bb_fxbus directly. */
#define BB_ED_FIELD_FX_DELAY_TYPE   0x0410u
#define BB_ED_FIELD_FX_DELAY_DIV    0x0411u
#define BB_ED_FIELD_FX_DIST_TYPE    0x0412u
#define BB_ED_FIELD_FX_COMP_AMOUNT  0x0413u

/* Mix fields use 0x0500 + bb_mix_channel. */
#define BB_ED_FIELD_MIX_BASE        0x0500u

#define BB_SNAP_GLOBAL     0x01u
#define BB_SNAP_DRUM_A     0x03u
#define BB_SNAP_DRUM_B     0x04u
#define BB_SNAP_DILLO      0x05u
#define BB_SNAP_FX         0x06u
#define BB_SNAP_MIX        0x07u
#define BB_SNAP_TRANSPORT  0x09u

#define BB_ED_MAX_CHUNK_RAW 112u
#define BB_ED_MAX_TX_COMMANDS 1u

#endif
