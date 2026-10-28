/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef BB_CATALOGUE_H
#define BB_CATALOGUE_H
#include "bb_paged_records.h"
typedef uint16_t bb_catalogue_id;
#define BB_CATALOGUE_NONE UINT16_MAX
enum { BB_CAT_NULL,BB_CAT_FALSE,BB_CAT_TRUE,BB_CAT_NUMBER,BB_CAT_TEXT,BB_CAT_ARRAY,BB_CAT_OBJECT };
typedef bb_pr_view bb_catalogue;
/* Explicit caller-owned cache; immutable source and exclusive control-owner
 * lifetime. No catalogue work in audio/IRQ. Failure preserves output view. */
int bb_catalogue_open(bb_catalogue *,const uint8_t *,size_t,bb_pc_cache *);
int bb_catalogue_kind(const bb_catalogue *,bb_catalogue_id);
size_t bb_catalogue_count(const bb_catalogue *,bb_catalogue_id);
bb_catalogue_id bb_catalogue_at(const bb_catalogue *,bb_catalogue_id,size_t);
bb_catalogue_id bb_catalogue_key_at(const bb_catalogue *,bb_catalogue_id,size_t);
bb_catalogue_id bb_catalogue_find(const bb_catalogue *,bb_catalogue_id,const char *);
int bb_catalogue_number(const bb_catalogue *,bb_catalogue_id,double *);
int bb_catalogue_unsigned(const bb_catalogue *,bb_catalogue_id,uint32_t,uint32_t *);
int bb_catalogue_scaled(const bb_catalogue *,bb_catalogue_id,unsigned,unsigned,uint32_t,uint32_t *);
int bb_catalogue_text(const bb_catalogue *,bb_catalogue_id,const uint8_t **,size_t *);
int bb_catalogue_copy_text(const bb_catalogue *,bb_catalogue_id,char *,size_t);
/* Mapped region outside the view, immutable storage and mutable cache. */
int bb_catalogue_private(const bb_catalogue *,const void *,size_t);
#endif
