/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef BB_PAGED_RECORDS_H
#define BB_PAGED_RECORDS_H
#include "bb_paged_container.h"
#define BB_PR_NONE UINT16_MAX
enum {BB_PR_NULL,BB_PR_FALSE,BB_PR_TRUE,BB_PR_NUMBER,BB_PR_TEXT,BB_PR_ARRAY,BB_PR_OBJECT};
typedef struct { bb_pc_view container; uint32_t validated; } bb_pr_view;
/* Immutable bytes and exclusive caller-owned cache must outlive this view.
 * Failed open preserves out; cache is scratch and may change. No live audio
 * or IRQ use. This module is an isolated candidate, not production ABI. */
int bb_pr_open(bb_pr_view *,const uint8_t *,size_t,bb_pc_cache *);
int bb_pr_kind(const bb_pr_view *,uint16_t);
size_t bb_pr_count(const bb_pr_view *,uint16_t);
uint16_t bb_pr_at(const bb_pr_view *,uint16_t,size_t);
uint16_t bb_pr_key_at(const bb_pr_view *,uint16_t,size_t);
uint16_t bb_pr_find(const bb_pr_view *,uint16_t,const char *);
int bb_pr_text(const bb_pr_view *,uint16_t,const uint8_t **,size_t *);
/* Exact binary64 representation; no target floating-point helper required. */
int bb_pr_number_bits(const bb_pr_view *,uint16_t,uint8_t[8]);
int bb_pr_number(const bb_pr_view *,uint16_t,double *);
int bb_pr_unsigned(const bb_pr_view *,uint16_t,uint32_t,uint32_t *);
int bb_pr_copy_text(const bb_pr_view *,uint16_t,char *,size_t);
int bb_pr_scaled(const bb_pr_view *,uint16_t,unsigned,unsigned,uint32_t,uint32_t *);
#endif
