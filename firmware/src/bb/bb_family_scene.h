/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once
#include "bb_session.h"
#include "bb909_patch.h"
#define BB_FAMILY_SCENE_BYTES (16u+BB_SESSION_BYTES+4u+4u*BB909_PATCH_BYTES)
typedef struct {bb_core core;uint8_t family[4];bb909_controls patch[4];} bb_family_scene;
typedef struct {bb_family_scene decoded;bb_core core_scratch;} bb_family_scene_workspace;
/* Stable main/suspended snapshots only. Family 0=808,1=909; unknown families
 * reject explicitly until their codecs exist. All-808 scenes remain original
 * byte-identical BBSC. Extended scenes use BBFS and cannot load as old BBSC.
 * No flash writes or live DSP reinitialization occurs in this codec. */
int bb_family_scene_valid(const bb_family_scene *);
enum {BB_FAMILY_ADMISSION_INVALID,BB_FAMILY_ADMISSION_OK,BB_FAMILY_ADMISSION_CAPACITY};
int bb_family_scene_admit(const bb_family_scene *,unsigned available_909_owners);
size_t bb_family_scene_encode(const bb_family_scene *,void *,size_t);
/* Caller-owned workspace avoids a large decoder stack frame. Neither output
 * nor workspace may overlap input or each other. Output unchanged on failure.
 * More saved 909 layers than live capacity is DATA, not permission to play it;
 * the separate activation admission must reject, never convert/drop layers. */
int bb_family_scene_decode(const void *,size_t,uint32_t,bb_family_scene *,bb_family_scene_workspace *);
/* Exclusive staging only: failure may change workspace, never live music.
 * On success decoded is complete and valid; avoids a duplicate scene buffer. */
int bb_family_scene_decode_staged(const void *,size_t,uint32_t,bb_family_scene_workspace *);
