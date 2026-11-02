/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef BB_ARR_SOURCES_H
#define BB_ARR_SOURCES_H
#include "bb_arrangement.h"
#include "bb_audio_section.h"
#include "bb_browser.h"
enum { BB_ARR_SOURCE_REFS=96, BB_ARR_WORK_PARTS=BB_ARR_WORK_LIMIT, BB_ARR_SOURCE_KEY=96 };
enum { BB_SOURCE_OK, BB_SOURCE_INVALID, BB_SOURCE_REVISION, BB_SOURCE_KEY_LONG,
       BB_SOURCE_AMBIGUOUS, BB_SOURCE_FULL, BB_SOURCE_MISSING, BB_SOURCE_KIND,
       BB_SOURCE_CONTENT, BB_SOURCE_WORK_FULL, BB_SOURCE_EXHAUSTED,
       BB_SOURCE_UNCHANGED, BB_SOURCE_HISTORY };
enum { BB_SOURCE_DRUM, BB_SOURCE_DILLO };
typedef struct {
    char bank[BB_ARR_SOURCE_KEY], part[BB_ARR_SOURCE_KEY];
    uint16_t bank_index,part_index; /* validated runtime cache, never durable identity */
    uint8_t group,kind,bank_name,part_name;
} bb_arr_source_entry;
typedef struct {
    union { bb_phrase_index drum; bb_pattern_data dillo; } data;
    bb_arr_part origin; /* keep source lineage; a copy is not an original transcription */
    uint64_t protected_steps;
    uint32_t revision;
    uint8_t kind;
} bb_arr_work_part;
typedef struct {
    const bb_catalogue *catalogue; /* runtime only, never written into a project file */
    uint32_t catalogue_crc,catalogue_stamp,next_revision;
    bb_arr_source_entry entry[BB_ARR_SOURCE_REFS];
    bb_arr_work_part work[BB_ARR_WORK_PARTS];
    uint8_t count,work_count; /* work_count is a physical high-water bound */
    uint16_t reserved; /* explicit caller pins returned by source_fork */
} bb_arr_sources;
int bb_arr_sources_init(bb_arr_sources *,const bb_catalogue *);
/* Cold exclusive staging only: rebuild runtime caches from durable keys.
 * Failure may alter staging caches; never use on a live published source set. */
int bb_arr_sources_reindex_staged(bb_arr_sources *);
int bb_arr_source_bind(bb_arr_sources *,const bb_factory_choice *,unsigned kind,bb_arr_part *);
int bb_arr_source_choice(const bb_arr_sources *,bb_arr_part,unsigned kind,bb_factory_choice *);
int bb_arr_source_drum(const bb_arr_sources *,bb_arr_part,bb_phrase_index *);
int bb_arr_source_dillo(const bb_arr_sources *,bb_arr_part,bb_pattern_data *);
/* Explicit immutable fork. Old referenced payloads survive Undo. No eviction. */
int bb_arr_source_fork(bb_arr_sources *,bb_arr_part,unsigned kind,bb_arr_part *);
/* Drop the explicit caller pin after publishing into a counted owner. Stale
 * revisions cannot release a newer occupant of the same physical slot. */
int bb_arr_source_release(bb_arr_sources *,bb_arr_part);
int bb_arr_source_owners_valid(const bb_arr_sources *);
/* Cold exclusive import lineage validation; runtime caller pins are never reconstructed.
 * Parent-edge counts are derived exactly into bounded main-stack scratch, not cached. */
int bb_arr_source_owners_rebuild(bb_arr_sources *);
/* Prospective exact-count release graph; no mutation on planning/refusal. */
int bb_arr_source_reserve(const bb_arr_sources *,const bb_arrangement *,unsigned *,uint16_t *);
/* Private commit tail: apply a previously proven mask after journal admission. */
void bb_arr_source_reclaim(bb_arr_sources *,uint16_t);
/* Main-owner SONG ratchet authoring primitive. Caller must hold model/source
 * edit ownership and coordinate the live owner separately. One section journal
 * entry points to an immutable fork; source lineage/protection and other parts
 * remain intact. Failure leaves both stores byte-identical; scratch may change.
 * expected_revision prevents a stale gesture from editing a newer project.
 * This API alone does not submit audio or permit concurrent/live authoring. */
int bb_arr_source_ratchet(bb_arrangement *,bb_arr_sources *,uint32_t section,
                          uint32_t expected_revision,unsigned target,
                          unsigned voice,unsigned step,unsigned count,
                          uint32_t playable,bb_phrase_index *scratch);
/* Main-owned scratch only: failure may alter scratch but never model/music.
 * Source globals, complete rich ordinals/controls and Dillo expression persist. */
int bb_arr_prepare_section(const bb_arrangement *,const bb_arr_sources *,uint32_t section,
                           unsigned repeats,bb_section_bundle *scratch);
/* Shared by the arranger units: both ranges non-null, non-wrapping and non-overlapping. */
int bb_arr_separated(const void *,size_t,const void *,size_t);
#endif
