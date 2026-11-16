/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef BB_SAMPLE_PROBE_H
#define BB_SAMPLE_PROBE_H
#include <stdint.h>
enum {BB_SP_ABSENT,BB_SP_PASS,BB_SP_ADMISSION,BB_SP_SOURCE,BB_SP_RENDER,
      BB_SP_BUDGET,BB_SP_NONFINITE,BB_SP_REJECTED,BB_SP_RESTORE,BB_SP_TIME_LIMIT};
typedef struct {
    uint32_t version,status,kits,completed_kits,blocks,min_ticks,max_ticks,total_ticks;
    uint32_t sample_hits,sample_rejections,dillo_frames,wet_blocks,callback_faults;
    uint32_t selected_kit,restored;
    uint32_t transitions,overlap_blocks;
} bb_sample_probe_report;
#endif
