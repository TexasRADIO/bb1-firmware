/* SPDX-License-Identifier: GPL-3.0-only */
/* Native qualification only: charge all proposed caller-owned staging memory.
 * No claim that a live storage/file owner has adopted or used these objects. */
#include "bb_project.h"
static bb_arr_sources bb_project_qualified_sources __attribute__((used));
static bb_arrangement bb_project_qualified_model __attribute__((used,section(".pool")));
static uint8_t bb_project_qualified_scene[BB_PROJECT_SCENE_MAX] __attribute__((used,section(".pool")));
