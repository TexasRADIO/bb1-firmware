/* SPDX-License-Identifier: GPL-3.0-only */
/* Startup-only reproduction. Include after target/audio/worker definitions;
 * call after runtime binding and factory init, BEFORE audio_init/usb_start.
 * Includes output conversion, NOT DMA/physical output or transport commands. */
#include "bb/bb_factory.h"
#include "bb/bb_factory_drum_launch.h"
typedef struct {
    uint32_t status,blocks,ticket,applied,max_ticks,total_ticks;
    uint32_t worker_reason,worker_frames,worker_trace,source_error;
    uint32_t transition_ticket,transition_applied,transition_frame;
    /* v2 kind1: complete private audio blocks, NOT legacy worker phases. */
    uint32_t version,kind,completed_blocks,render_ticks_sum,physical_jobs,logical_spans,min_ticks;
    uint32_t send_blocks,wet_blocks,drum_b_hits,callback_faults;
} bb_private_render_report;
enum {BB_PR_NOT_ADMITTED=1,BB_PR_SOURCE,BB_PR_REQUEST,BB_PR_RENDER,
      BB_PR_TIME_LIMIT,BB_PR_NOT_APPLIED,BB_PR_NONFINITE,BB_PR_PASS,BB_PR_RESTORE,BB_PR_PROFILE,BB_PR_PARITY};
static bb_private_render_report bb_private_render_result;
/* Detailed attribution is optional. Its records stay explicitly invalid when
 * disabled; independent whole-half deadline/DMA counters and quiet timing stay
 * active. Do not require observer overhead in a production-cost comparison. */
#include "bb/bb_attribution.h"
/* One correlated interval. No new clock reads or renderer hooks. Snapshot
 * happens AFTER the existing elapsed timestamp, only on a new maximum.
 * Copying can perturb subsequent cache state: not a speed comparison. */
static struct {
    uint32_t version,valid,block,frame,elapsed,stage_sum;
    uint32_t ticks[BB_PROF_COUNT],calls[BB_PROF_COUNT],frames[BB_PROF_COUNT];
    uint32_t hits_a,hits_b,next_a,next_b,next_dillo,next_telegram;
    uint32_t worker_service,worker_wait;
} bb_private_peak;
/* Supplemental snapshot; existing v1 peak semantics unchanged. */
static bb_attribution bb_private_peak_attribution;

/* SPDX-License-Identifier: GPL-3.0-only */
/* PRE-AUDIO diagnostic. CPU0 single-track attack cost, not whole-arrangement
 * acceptance. Shared metal-bank cost is included in each metal voice and
 * must not be added across those voices. No DSP approximation or live PLAY. */
enum {BB_VC_NOT_RUN,BB_VC_RUNNING,BB_VC_PASS,BB_VC_REFUSED,BB_VC_INVALID,BB_VC_CAP,BB_VC_RESTORE};
static struct {
    uint32_t version,status,cases,failed_case,elapsed,calibration;
    uint32_t ticks[D8S_NUM][4];
} bb_voice_cost;

#include "bb/x0x/dense_sweep_api.h"
static bb_dense_sweep_result bb_dense_cost;
static uint32_t bb_private_block_active;
/* Legacy read-only record remains NOT_RUN, never a fabricated parity pass. */
static struct {uint32_t version,status,cases,bytes,jobs,failed_case,worker_reason;} bb_retained_parity;

#include "dillo-startup-diagnostic-r1.inc"
