/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once
#include "bb_core.h"
enum {BB_SWITCH_808,BB_SWITCH_909};
enum {BB_SWITCH_OK,BB_SWITCH_INVALID,BB_SWITCH_MISSING_VOICE,BB_SWITCH_MODE};
/* Main-loop staging while audio is acknowledged suspended. No DSP writes.
 * Source lanes have explicit family meaning; target 808 recipe is explicit.
 * Preserves all musical timing/mix/enable state, output unchanged on failure.
 * A future commit must carry family identity/revision alongside the grid. */
int bb909_switch_project(const bb_machine *,unsigned source_family,unsigned target_family,
                         const bb_drum_sound *target808,bb_machine *out,uint32_t *missing_roles);
