/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef BB_SNAPSHOT_PROBE_H
#define BB_SNAPSHOT_PROBE_H
#include <stdint.h>
/* Cold benchmark only. Keep it in diagnostic builds; playable profiles may
 * omit it without changing the live snapshot-sync implementation. */
#ifndef BB_SNAPSHOT_STARTUP_DIAGNOSTIC
#define BB_SNAPSHOT_STARTUP_DIAGNOSTIC 1
#endif
enum {BB_SNAPSHOT_PHASES=5,BB_SNAPSHOT_POLLS=64};
enum {BB_SNAPSHOT_ABSENT,BB_SNAPSHOT_PASS,BB_SNAPSHOT_ADMISSION,
      BB_SNAPSHOT_GUARD,BB_SNAPSHOT_STATE,BB_SNAPSHOT_RESTORE,BB_SNAPSHOT_TIME};
typedef struct {
    uint32_t version,status,polls,completed,failures,restored;
    uint32_t ticks[BB_SNAPSHOT_PHASES],guards[BB_SNAPSHOT_PHASES],guard_max[BB_SNAPSHOT_PHASES];
} bb_snapshot_probe_report;
#endif
