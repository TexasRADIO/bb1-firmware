/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef BB_GROUP_PHASE_LAYOUT_H
#define BB_GROUP_PHASE_LAYOUT_H
#include "bb_trio.h"
#include "bb909_owner.h"
#include "group_boot_reference.h"
typedef struct {bb_trio_program program;gg_state groups[BB_MACHINES];} bb_group_runtime;
typedef union {
    bb_group_boot_reference reference;
    bb_group_runtime runtime;
    bb909_owner owner;
} bb_group_phase_pool;
_Static_assert(sizeof(bb_group_phase_pool)==10352,"phase pool must not grow");
_Static_assert(offsetof(bb_group_runtime,groups)>=sizeof(bb_trio_program),"groups must not overlap program");
#endif
