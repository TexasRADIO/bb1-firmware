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
#endif
