/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef BB_RICH_PROBE_H
#define BB_RICH_PROBE_H
#include <stdint.h>
#if defined(BB_GROUP_TARGET_CANDIDATE) && BB_GROUP_TARGET_CANDIDATE
/* Isolated boot experiment: omit the long synthetic rich-pattern soak.
 * Normal bank binding, audio, and host tests remain separate. */
#undef BB_RICH_BOOT_QUALIFY
#define BB_RICH_BOOT_QUALIFY 0
#endif
#ifndef BB_RICH_BOOT_QUALIFY
#define BB_RICH_BOOT_QUALIFY 0
#endif
#if BB_RICH_BOOT_QUALIFY && (!BB_WITH_RICH_DRUM_RENDER || BB_OWNER_AUDIO_BRIDGE || BB_WITH_PARTS || !BB_USE_DRUM_SPAN)
#error "Rich quiet probe requires copied CPU0 rich span rendering"
#endif
enum {BB_RP_ABSENT,BB_RP_PASS,BB_RP_ADMISSION,BB_RP_SOURCE,BB_RP_RENDER,
      BB_RP_BUDGET,BB_RP_NONFINITE,BB_RP_RESTORE,BB_RP_TIME_LIMIT};
enum {BB_RP_ROUTES=7,BB_RP_BLOCKS=256};
typedef struct {
    uint32_t version,status,routes,completed,blocks,min_ticks,max_ticks,total_ticks;
    uint32_t hits_a,hits_b,dillo_frames,wet_blocks,faults,restored,transitions;
    uint32_t command_max_ticks,render_errors,primary[2],delayed[2],peaks[BB_RP_ROUTES];
    uint32_t source_commands,source_refreshes,source_pending_reads;
    uint32_t source_snapshot_max_ticks,source_queue_max_ticks;
    uint32_t sampled_rich_mask,sampled_rich_hits,sampled_rich_acks;
    /* Supplemental startup-only peak location, versioned independently.
     * Captured after elapsed timing; never an IRQ/live profiling record. */
    uint32_t trace_version,trace_valid,trace_route,trace_block,trace_flags,trace_source;
    uint32_t trace_frame_low,trace_frame_high,trace_hits_a,trace_hits_b;
    /* New sample-mix workload, not comparable to an older v3-only run. */
    uint32_t mix_version,mix_commands,mix_pending_reads,mix_refreshes;
    uint32_t mix_snapshot_max_ticks,mix_queue_max_ticks,mix_failures,mix_restored;
    /* Ride is queued only after sampled ownership exists, then verified in
     * the applied phrase AND an actual active ADPCM note51 voice slot. */
    uint32_t ride_version,ride_requests,ride_pending_reads,ride_applied;
    uint32_t ride_active_mask,ride_slot_mask,ride_snapshot_max_ticks,ride_failures,ride_restored;
} bb_rich_probe_report;
#endif
