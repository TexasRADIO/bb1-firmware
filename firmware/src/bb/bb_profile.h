#ifndef BB_PROFILE_H
#define BB_PROFILE_H
#include <stdint.h>
#include "bb_stage_probe.h"
#ifndef BB_AUDIO_PROFILE
#define BB_AUDIO_PROFILE 0
#endif
enum {BB_PROF_DILLO,BB_PROF_TELEGRAM,BB_PROF_DRUMS,BB_PROF_ECHO,
      BB_PROF_SPRING,BB_PROF_OUTPUT,BB_PROF_COUNT};
#if BB_AUDIO_PROFILE
#define BB_PROFILE_HALF_REPORT 1
#include "bb_attribution.h"
extern bb_attribution bb_attr;
/* Optional single-audio-writer wall-time telemetry. No allocation, logging or
 * locks in the renderer. Includes any higher-priority interruption; never
 * label these exclusive CPU times. Main-loop readers need an IRQ snapshot.
 * Independent maxima cannot be added to obtain a simultaneous block cost. */
typedef struct {uint32_t ticks,calls,frames,max_ticks;} bb_profile_stage;
typedef struct {bb_profile_stage stage[BB_PROF_COUNT];uint32_t blocks,errors;} bb_profile_report;
extern volatile bb_profile_report bb_profile;
typedef struct {uint32_t ticks,calls,frames;} bb_profile_cost;
typedef struct {
    bb_profile_cost stage[BB_PROF_COUNT];
    uint32_t total_ticks,nested_ticks,budget_ticks,frames,blocks,sequence,valid;
    uint32_t worker_wait_ticks,worker_joins,worker_run_ticks,worker_runs;
} bb_profile_window;
typedef struct {
    bb_profile_window last,peak;
    uint32_t completed,errors;
} bb_profile_window_report;
extern volatile bb_profile_window_report bb_profile_windows;
uint32_t bb_profile_clock(void); /* platform monotonic modulo-32-bit ticks */
void bb_profile_block_begin(void);
void bb_profile_begin(unsigned stage);
void bb_profile_end(unsigned stage,uint32_t frames);
void bb_profile_block_end(uint32_t frames,int success);
/* CPU0 wall time waiting for rich-worker completion; a subset of DRUMS,
 * not additive to stage totals and not CPU1 execution time. */
void bb_profile_worker_wait(uint32_t elapsed);
void bb_profile_worker_run(uint32_t elapsed,int valid);
void bb_profile_window_begin(void);
void bb_profile_window_end(uint32_t total,uint32_t nested,uint32_t budget);
#define BB_PROFILE_BLOCK_BEGIN() bb_profile_block_begin()
#define BB_PROFILE_BEGIN(s) bb_profile_begin(s)
#define BB_PROFILE_END(s,n) bb_profile_end(s,n)
#define BB_PROFILE_RETURN(value) do { int bb_result=(value);bb_profile_block_end(frames,bb_result);return bb_result; } while(0)
#else
#define BB_PROFILE_BLOCK_BEGIN() ((void)0)
#define BB_PROFILE_BEGIN(s) BB_STAGE_BEGIN(s)
#define BB_PROFILE_END(s,n) BB_STAGE_END(s,n)
#define BB_PROFILE_RETURN(value) return (value)
#endif
#endif
