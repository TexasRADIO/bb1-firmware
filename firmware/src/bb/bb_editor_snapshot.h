/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef BB_EDITOR_SNAPSHOT_H
#define BB_EDITOR_SNAPSHOT_H

#include <stdint.h>
#include "bb_core.h"

/* Current BB1 core-owned state is under 160 bytes. Keep a bounded margin. */
#define BB_SNAPSHOT_MAX 256u

typedef struct {
    uint8_t buf[BB_SNAPSHOT_MAX];
    uint16_t len;
    uint32_t revision;
} bb_editor_snapshot;

int bb_editor_snapshot_build(bb_editor_snapshot *snapshot,
                             const bb_core *core,
                             uint32_t revision);

#endif
