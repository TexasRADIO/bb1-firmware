/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef BB_STAGE_PROBE_H
#define BB_STAGE_PROBE_H
#include <stdint.h>
#ifndef BB_QUIET_STAGE_PROBE
#define BB_QUIET_STAGE_PROBE 0
#endif
/* Top six scopes are the existing disjoint profile stages. A/B are children
 * of DRUMS and MUST NOT be added to that parent's cost. Wall time only. */
enum { BB_STAGE_DRUM_A=6,BB_STAGE_DRUM_B=7,BB_STAGE_COUNT=8 };
typedef struct {uint32_t ticks,calls,frames;} bb_stage_cost;
typedef struct {
    uint32_t version,active,open,errors,route,block,total_ticks,complete;
    uint32_t started[BB_STAGE_COUNT];
    bb_stage_cost cost[BB_STAGE_COUNT];
} bb_stage_probe_report;
#if BB_QUIET_STAGE_PROBE
extern volatile bb_stage_probe_report bb_stage_probe;
uint32_t bb_stage_probe_clock(void);
void bb_stage_probe_reset(unsigned route,unsigned block);
void bb_stage_probe_begin(unsigned stage);
void bb_stage_probe_end(unsigned stage,unsigned frames);
void bb_stage_probe_finish(uint32_t total);
#define BB_STAGE_BEGIN(s) do {if(bb_stage_probe.active)bb_stage_probe_begin(s);} while(0)
#define BB_STAGE_END(s,n) do {if(bb_stage_probe.active)bb_stage_probe_end(s,n);} while(0)
#else
#define BB_STAGE_BEGIN(s) ((void)0)
#define BB_STAGE_END(s,n) ((void)0)
#endif
#endif
