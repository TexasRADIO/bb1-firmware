/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef BB_SAMPLE_SCENE_H
#define BB_SAMPLE_SCENE_H
#include "group_scene.h"
/* Cold, stopped-session codec. No flash or live audio operations. */
#define BB_SAMPLE_SCENE_OVERHEAD 32u
#define BB_SAMPLE_SCENE_BYTES (GG_SCENE_BYTES+BB_SAMPLE_SCENE_OVERHEAD)
typedef struct {
    uint32_t pack_header_crc, pack_payload_crc;
    float gain;
    uint8_t kit, echo, spring, kind; /* 0=sample(v1),1=retained808(v2,canonical kit0) */
} bb_sample_scene_identity;
/* A sampled scene MUST carry library identity. A legacy scene is not silently
 * interpreted as the currently selected sampled kit. Call its legacy decoder
 * explicitly instead. Scene/workspace must be owned exclusively by caller. */
size_t bb_sample_scene_encode(const bb_family_scene *, const uint8_t *,
                             const bb_sample_scene_identity *, void *, size_t);
/* Expected library fingerprints and kit count come from the validated resident
 * pack. Failure may touch workspace only; identity/mix outputs stay unchanged. */
/* 1.2 scene conversion: accept scenes pinned to this predecessor library (kits 0..kits-1); kits 0 clears */
void bb_sample_scene_set_predecessor(uint32_t header,uint32_t payload,unsigned kits);
int bb_sample_scene_decode(const void *,size_t,uint32_t,uint32_t,uint32_t,unsigned,
                           bb_family_scene_workspace *,uint8_t *,bb_sample_scene_identity *);
/* v3 adds B identity without changing v1/v2 interpretation. Offline codec;
 * caller must still stage both owners and rich phrases before live restore. */
#define BB_SAMPLE_PAIR_SCENE_OVERHEAD 48u
/* Pair codec rejects all nonzero family tags: nested data is BBSC, optionally
 * BBGM, never the larger four-909 BBFS record. Keep the bound exact. */
#define BB_SAMPLE_PAIR_SCENE_BYTES (BB_SESSION_BYTES+GG_SCENE_OVERHEAD+BB_SAMPLE_PAIR_SCENE_OVERHEAD)
size_t bb_sample_pair_scene_encode(const bb_family_scene *,const uint8_t *,
                                  const bb_sample_scene_identity [2],void *,size_t);
int bb_sample_pair_scene_decode(const void *,size_t,uint32_t,uint32_t,uint32_t,unsigned,
                               bb_family_scene_workspace *,uint8_t *,bb_sample_scene_identity [2]);
#ifndef BB_WITH_USER_KITS
#define BB_WITH_USER_KITS 0
#endif
#if BB_WITH_USER_KITS
#include "bb_user_kit.h"
/* v4 (2.0 kit builder): v3 plus, for each layer whose kind is 2 (USER), its
 * complete BBUK record after the 48-byte header. A scene carries the kit it
 * plays, so a later SAVE AS over that slot never changes an old scene. The
 * kit byte is the slot it came from (0..3) or 7 for an unsaved BUILD draft.
 * A scene with no USER layer is written as byte-identical v3; 1.5 decoders
 * refuse v4 rather than misread it. */
#define BB_SAMPLE_USER_SCENE_BYTES (BB_SAMPLE_PAIR_SCENE_BYTES+2u*BB_UK_RECORD_BYTES)
/* v5: the existing per-layer kinds remain 0=sample, 1=808, 2=USER;
 * 3=SLOOP adds one 22-byte record (BS31 recipe/image CRCs, 14 packed control
 * bytes). Header 40 is USER mask; 41 is SLOOP mask. No SLOOP writes exact v3/v4.
 * Expected provenance must come from the independently validated resident bank.
 * Codecs never bind a bank or mutate a live owner. */
typedef struct {uint32_t recipe_crc,image_crc;} bb_sample_sloop_bank_identity;
typedef struct {bb_sample_sloop_bank_identity bank;uint8_t controls[14];} bb_sample_sloop_scene;
#define BB_SAMPLE_SLOOP_RECORD_BYTES 22u
#define BB_SAMPLE_SHARED_SCENE_BYTES (BB_SAMPLE_USER_SCENE_BYTES)
int bb_sample_sloop_controls_pack(uint8_t [14],const int8_t [16],const int8_t [16]);
int bb_sample_sloop_controls_unpack(const uint8_t [14],int8_t [16],int8_t [16]);
size_t bb_sample_shared_scene_encode(const bb_family_scene *,const uint8_t *,const bb_sample_scene_identity [2],
                                    const bb_user_kit *const [2],const bb_sample_sloop_scene *const [2],void *,size_t);
int bb_sample_shared_scene_decode(const void *,size_t,uint32_t,uint32_t,uint32_t,unsigned,
                                 bb_family_scene_workspace *,uint8_t *,bb_sample_scene_identity [2],
                                 bb_user_kit [2],unsigned *,const bb_sample_sloop_bank_identity *,
                                 bb_sample_sloop_scene [2],unsigned *);
size_t bb_sample_user_scene_encode(const bb_family_scene *,const uint8_t *,const bb_sample_scene_identity [2],
                                   const bb_user_kit *const [2],void *,size_t);
int bb_sample_user_scene_decode(const void *,size_t,uint32_t,uint32_t,uint32_t,unsigned,
                                bb_family_scene_workspace *,uint8_t *,bb_sample_scene_identity [2],
                                bb_user_kit [2],unsigned *user_mask);
#endif
#endif
