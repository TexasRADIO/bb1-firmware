/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once
#include "bb909_controls.h"
#include "bb909_owner.h"
#include <stddef.h>
#define BB909_PATCH_BYTES 168u
int bb909_patch_valid(const bb909_controls *);
int bb909_patch_encode(const bb909_controls *,void *,size_t);
int bb909_patch_decode(const void *,size_t,bb909_controls *);
enum {BB909_RESTORE_INVALID,BB909_RESTORE_READY,BB909_RESTORE_DONE};
/* Main/suspended staging only. Cursor is 0..132 descriptor slots. A ready
 * request contains <=32 touched SET events, no triggers, one render frame.
 * DONE leaves request unchanged: do not render an unnecessary empty job.
 * Caller publishes only to a newly initialized, idle, acknowledged actor.
 * This is an inner 909 patch codec, not the complete Big Beat scene format. */
int bb909_patch_request(const bb909_controls *,unsigned cursor,uint32_t ticket,bb909_request *,unsigned *next);
