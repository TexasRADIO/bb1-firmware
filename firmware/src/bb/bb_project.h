/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef BB_PROJECT_H
#define BB_PROJECT_H
#include "bb_arr_sources.h"
enum {BB_PROJECT_HEADER=64,BB_PROJECT_MAX_BYTES=32768,BB_PROJECT_SCENE_MAX=3840};
enum {BB_PROJECT_OK,BB_PROJECT_INVALID,BB_PROJECT_CAPACITY,BB_PROJECT_VERSION,
      BB_PROJECT_CHECKSUM,BB_PROJECT_CATALOGUE,BB_PROJECT_ASSETS,BB_PROJECT_SCENE,
      BB_PROJECT_SOURCE,BB_PROJECT_ALIAS,BB_PROJECT_IO,BB_PROJECT_CONFLICT};
typedef struct {bb_arrangement arrangement;bb_arr_sources sources;} bb_project;
typedef struct {uint32_t library_header_crc,library_payload_crc;} bb_project_assets;
/* Cold main-loop snapshots only. Nested sound/mix/tempo/expression scene must
 * be coherently captured by the existing suspended scene owner beforehand.
 * The callback validates that entire nested scene and its engine admission;
 * NULL is never an instruction to skip it. No flash, transport or DSP writes.
 * Project files do not carry runtime caches/pointers or executable Undo.
 * Reopening starts a fresh Undo history and retains every working lineage.
 * Version2 retains packed per-hit ratchets; version1 opens with all counts x1.
 * Older readers refuse version2 rather than silently losing metadata. */
typedef int (*bb_project_scene_check)(void *,const void *,size_t);
int bb_project_encode(const bb_project *,bb_project_assets,const void *scene,size_t,
                      bb_project_scene_check,void *context,void *wire,size_t,size_t *written);
/* Exclusive caller-owned staging only: failure may change stage, never live
 * music. Exact catalogue and library pins required. Returned scene borrows
 * immutable wire lifetime. Caller must obtain stopped edit/restore ownership
 * and commit scene + model together; this API is NOT a live restore API. */
int bb_project_open_staged(const void *,size_t,const bb_catalogue *,bb_project_assets,
                           bb_project_scene_check,void *context,bb_project *stage,
                           const void **scene,size_t *scene_bytes);
/* File adapter contract: immutable input / exclusively owned temporary output.
 * Exactly n bytes or return0; callbacks must be bounded and never edit music.
 * A failed write may leave a partial TEMP file. Publish/rename and saved-revision
 * acknowledgement belong to the caller after close/reopen/readback, not here.
 * Writes are <=256B, payload first, final header at offset0 only after success.
 * Reads are <=256B and monotonic; callback buffers borrow only the call lifetime. */
typedef int (*bb_project_read_at)(void *,size_t,void *,size_t);
typedef int (*bb_project_write_at)(void *,size_t,const void *,size_t);
int bb_project_write_stream(const bb_arrangement *,const bb_arr_sources *,bb_project_assets,
                            const void *,size_t,bb_project_scene_check,void *,
                            bb_project_write_at,void *,size_t *);
/* Split caller-owned staging allows sources in ordinary RAM and model in pool.
 * scene_buffer is caller-owned (>= nested size), never a project-sized stack
 * allocation. Failure may change staging/buffer, never live state or handles.
 * Only a validated result can proceed to separate atomic scene/model restore. */
int bb_project_read_stream_staged(bb_project_read_at,void *,size_t,const bb_catalogue *,
                                  bb_project_assets,bb_project_scene_check,void *,
                                  bb_arrangement *,bb_arr_sources *,void *,size_t,
                                  const void **,size_t *);
/* Main-loop save transaction. Caller owns STOP/scene capture and prevents all
 * project/scene edits throughout. begin owns a fresh private temporary file;
 * seal closes and reopens it as immutable input and reports exact byte length.
 * publish atomically replaces the named destination ONLY on success; on failure
 * the old destination stays intact. abort closes/removes temporary state, never
 * the destination. Failed begin cleans its own partial acquisition. All hooks
 * are bounded; neither these hooks nor the scene checker may edit live music.
 * No saved acknowledgement until full readback + exact header match + publish.
 * Staging must be disjoint from live model/sources/scene and callback context.
 * This is not the device file driver or the UI/atomic-live-restore owner. */
typedef struct {
 int (*begin)(void *);
 bb_project_write_at write;
 int (*seal)(void *,size_t,size_t *);
 bb_project_read_at read;
 int (*publish)(void *);
 void (*abort)(void *);
} bb_project_file_ops;
enum {BB_PROJECT_FILE_VERIFY=BB_PROJECT_CONFLICT+1,BB_PROJECT_FILE_PUBLISH};
int bb_project_save_verified(bb_arrangement *,const bb_arr_sources *,bb_project_assets,
 const void *,size_t,bb_project_scene_check,void *,const bb_project_file_ops *,void *,
 bb_arrangement *,bb_arr_sources *,void *,size_t,size_t *);
#endif
