/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef BB606_PROBE_H
#define BB606_PROBE_H
#include "bb606.h"
/* Private PRE-AUDIO diagnostic payload, never a live engine. Caller must prove
 * the previous worker completed or was held before preparing or inspecting it.
 * No pointers or callbacks cross the core boundary. Size is the compact PI32
 * profile; prepare refuses any differently compiled engine. */
#define BB606_PROBE_STATE_BYTES 4848u
#define BB606_PROBE_FRAMES 128u
#define BB606_PROBE_GUARD UINT32_C(0x606DA731)
typedef struct {
    uint32_t guard,ticket,voices,frames,velocity,input_check,finished;
    uint32_t state[BB606_PROBE_STATE_BYTES/4];
    float bus[3][BB606_PROBE_FRAMES];
    uint32_t end_guard;
} bb606_probe_job;
#ifdef __cplusplus
extern "C" {
#endif
int bb606_probe_prepare(bb606_probe_job *,uint32_t ticket,unsigned voices,
                       unsigned frames,unsigned velocity);
int bb606_probe_run(bb606_probe_job *,uint32_t ticket,uint32_t *result);
/* Timing data stays outside the checked engine payload. A zero valid flag
 * means a rejected/incomplete job, never a zero-cost success. */
typedef struct {uint32_t input_ticks,trigger_ticks,render_ticks,output_ticks,valid;} bb606_probe_timing;
uint32_t bb606_probe_clock(void); /* direct target timer, no mailbox callback */
int bb606_probe_run_timed(bb606_probe_job *,uint32_t,uint32_t *,bb606_probe_timing *);
int bb606_probe_accept(const bb606_probe_job *,uint32_t ticket,uint32_t result);
#ifdef __cplusplus
}
#endif
#endif
