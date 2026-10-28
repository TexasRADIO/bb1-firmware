/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef BB_GROUP_SCENE_H
#define BB_GROUP_SCENE_H
#include "bb_family_scene.h"
#include "bb_wire.h"
#include "group_gain.h"
#define GG_SCENE_MIX_BYTES 16u
#define GG_SCENE_OVERHEAD 32u
#define GG_SCENE_BYTES (BB_FAMILY_SCENE_BYTES+GG_SCENE_OVERHEAD)
/* Exclusive stopped/suspended ownership only, never call on live worker
 * memory. One byte per kit/group: level in bits 0..6, mute in bit 7.
 * Capture/restore do not publish a scene or change worker ownership. */
int gg_scene_capture(const gg_state bank[4],uint8_t packed[GG_SCENE_MIX_BYTES]);
int gg_scene_restore(gg_state bank[4],const uint8_t packed[GG_SCENE_MIX_BYTES]);
/* Prepare only the selected owner's group state before publishing the bank. */
int gg_scene_restore_layer(gg_state *,const uint8_t packed[4]);
/* Default groups produce byte-identical legacy BBSC/BBFS records. Modified
 * groups use BBGM v1: 16-byte header, 16 mix bytes, then the legacy record.
 * Stable snapshots and nonoverlapping output required. No flash operations. */
size_t gg_scene_encode(const bb_family_scene *,const uint8_t *,void *,size_t);
/* Failure may alter disposable workspace, never packed output. Success
 * yields a normalized stopped scene and its requested group levels/mutes.
 * Apply both under the existing suspended-owner protocol, not separately. */
int gg_scene_decode_staged(const void *,size_t,uint32_t,
                           bb_family_scene_workspace *,uint8_t *);
#endif
