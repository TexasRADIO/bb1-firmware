/* SPDX-License-Identifier: GPL-3.0-only
 * Bounded whole-half ISR timing telemetry for the derived FM-1 target.
 * Included only by the generated audio.c translation unit. */
#ifndef BB_TARGET_TIMING_H
#define BB_TARGET_TIMING_H

#include <stdint.h>

typedef struct {
    uint32_t last_total_ticks;
    uint32_t max_total_ticks;
    uint32_t last_nested_ticks;
    uint32_t max_nested_ticks;
    uint32_t last_render_ticks;
    uint32_t max_render_ticks;
    uint32_t budget_ticks;
    uint32_t budget_exceeds;
    uint32_t dma_misses;
    uint32_t last_entry_spacing_ticks;
    uint32_t max_entry_spacing_ticks;
    uint32_t nested_gt_total;
    uint32_t has_entry_spacing;
    /* Timing P1b (boot39): whole-half peak split by step-boundary class.
     * Appended, so earlier field offsets are unchanged. A half whose renders
     * ran a boundary with a drum-B launch due counts as "launch"; one whose
     * boundaries were all quiet counts as "quiet"; halves with no boundary
     * are in neither (the overall max above still covers them). */
    uint32_t max_total_b_launch_ticks;
    uint32_t max_total_b_quiet_ticks;
    uint32_t b_launch_halves;
    uint32_t b_quiet_halves;
} bb_target_timing_stats;

/* Single writer: ALNK0 ISR. Main-loop diagnostics must snapshot this while
 * interrupts are masked. Counters intentionally wrap as unsigned 32-bit. */
static volatile bb_target_timing_stats bb_target_timing;
static uint32_t bb_target_timing_prev_entry;
static uint8_t bb_target_timing_have_prev_entry;

static inline void bb_target_timing_entry(uint32_t now)
{
#if BB_AUDIO_PROFILE
    bb_profile_window_begin();
#endif
    if (bb_target_timing_have_prev_entry) {
        uint32_t spacing = now - bb_target_timing_prev_entry;
        bb_target_timing.last_entry_spacing_ticks = spacing;
        if (spacing > bb_target_timing.max_entry_spacing_ticks)
            bb_target_timing.max_entry_spacing_ticks = spacing;
        bb_target_timing.has_entry_spacing = 1u;
    }
    bb_target_timing_prev_entry = now;
    bb_target_timing_have_prev_entry = 1u;
}

static inline void bb_target_timing_half(uint32_t total, uint32_t nested,
                                         uint32_t budget)
{
    uint32_t render;
#if BB_AUDIO_PROFILE
    /* The timestamp was taken before this reporting tail. The profiler adds
     * overhead and measures wall time, not exclusive engine CPU time. */
    bb_profile_window_end(total,nested,budget);
#endif
    bb_target_timing.last_total_ticks = total;
    bb_target_timing.last_nested_ticks = nested;
    bb_target_timing.budget_ticks = budget;
    if (nested > total) {
        /* Defensive saturation: never let a sampling race or timer anomaly
         * turn a diagnostic render duration into unsigned underflow. */
        render = 0u;
        bb_target_timing.nested_gt_total++;
    } else {
        render = total - nested;
    }
    bb_target_timing.last_render_ticks = render;
    if (total > bb_target_timing.max_total_ticks)
        bb_target_timing.max_total_ticks = total;
    if (nested > bb_target_timing.max_nested_ticks)
        bb_target_timing.max_nested_ticks = nested;
    if (render > bb_target_timing.max_render_ticks)
        bb_target_timing.max_render_ticks = render;
    if (total > budget)
        bb_target_timing.budget_exceeds++;
}

static inline void bb_target_timing_boundary(uint32_t total, uint32_t flags)
{
    if (flags & BB_BOUNDARY_B_LAUNCH) {
        bb_target_timing.b_launch_halves++;
        if (total > bb_target_timing.max_total_b_launch_ticks)
            bb_target_timing.max_total_b_launch_ticks = total;
    } else if (flags & BB_BOUNDARY_SEEN) {
        bb_target_timing.b_quiet_halves++;
        if (total > bb_target_timing.max_total_b_quiet_ticks)
            bb_target_timing.max_total_b_quiet_ticks = total;
    }
}

static inline void bb_target_timing_dma_miss(void)
{
    bb_target_timing.dma_misses++;
}

#endif
