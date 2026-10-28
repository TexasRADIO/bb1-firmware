#include "bb_profile.h"
#if BB_QUIET_STAGE_PROBE
_Static_assert(BB_PROF_COUNT==6,"Quiet stage IDs must match the existing stage vocabulary");
#if BB_AUDIO_PROFILE
#error "Quiet stage attribution must not enable the full runtime profiler"
#endif
volatile bb_stage_probe_report bb_stage_probe;
void bb_stage_probe_reset(unsigned route,unsigned block) {
    bb_stage_probe=(bb_stage_probe_report){.version=1,.route=route,.block=block,.active=1};
}
void bb_stage_probe_begin(unsigned stage) {
    if(!bb_stage_probe.active)return;
    if(stage>=BB_STAGE_COUNT || (bb_stage_probe.open&(1u<<stage))){bb_stage_probe.errors++;return;}
    bb_stage_probe.open|=1u<<stage;
    bb_stage_probe.started[stage]=bb_stage_probe_clock();
}
void bb_stage_probe_end(unsigned stage,unsigned frames) {
    if(!bb_stage_probe.active)return;
    uint32_t now=bb_stage_probe_clock();
    if(stage>=BB_STAGE_COUNT || !(bb_stage_probe.open&(1u<<stage))){bb_stage_probe.errors++;return;}
    bb_stage_probe.cost[stage].ticks+=now-bb_stage_probe.started[stage];
    bb_stage_probe.cost[stage].calls++;
    bb_stage_probe.cost[stage].frames+=frames;
    bb_stage_probe.open&=~(1u<<stage);
}
void bb_stage_probe_finish(uint32_t total) {
    bb_stage_probe.active=0;bb_stage_probe.total_ticks=total;
    uint64_t sum=0;
    for(unsigned i=0;i<6;i++)sum+=bb_stage_probe.cost[i].ticks;
    uint64_t children=(uint64_t)bb_stage_probe.cost[BB_STAGE_DRUM_A].ticks+
        bb_stage_probe.cost[BB_STAGE_DRUM_B].ticks;
    bb_stage_probe.complete=!bb_stage_probe.open&&!bb_stage_probe.errors&&sum<=total&&
        children<=bb_stage_probe.cost[BB_PROF_DRUMS].ticks&&
        bb_stage_probe.cost[BB_PROF_DILLO].calls&&bb_stage_probe.cost[BB_PROF_DILLO].frames;
}
#endif
#if BB_AUDIO_PROFILE
volatile bb_profile_report bb_profile;
bb_attribution bb_attr;
volatile bb_profile_window_report bb_profile_windows;
static bb_profile_window window;
static uint32_t window_start_errors;
static unsigned window_open,block_open,window_failed;
static bb_profile_stage working[BB_PROF_COUNT];
static uint32_t started;
static unsigned active=BB_PROF_COUNT;
void bb_profile_block_begin(void) {
    if(active!=BB_PROF_COUNT || block_open)bb_profile.errors++;
    block_open=1;
    bb_attr_reset(&bb_attr);
    active=BB_PROF_COUNT;
    for(unsigned s=0;s<BB_PROF_COUNT;s++)working[s]=(bb_profile_stage){0,0,0,0};
}
void bb_profile_begin(unsigned stage) {
    if(stage>=BB_PROF_COUNT || active!=BB_PROF_COUNT || !block_open){bb_profile.errors++;return;}
    active=stage;started=bb_profile_clock();
}
void bb_profile_end(unsigned stage,uint32_t frames) {
    uint32_t ended=bb_profile_clock();
    if(stage>=BB_PROF_COUNT || active!=stage){bb_profile.errors++;return;}
    working[stage].ticks+=ended-started;
    working[stage].calls++;working[stage].frames+=frames;
    active=BB_PROF_COUNT;
}
void bb_profile_block_end(uint32_t frames,int success) {
    if(!block_open){bb_profile.errors++;return;}
    if(active!=BB_PROF_COUNT){bb_profile.errors++;active=BB_PROF_COUNT;}
    for(unsigned s=0;s<BB_PROF_COUNT;s++) {
        uint32_t previous=bb_profile.stage[s].max_ticks;
        working[s].max_ticks=working[s].ticks>previous?working[s].ticks:previous;
        bb_profile.stage[s]=working[s];
        if(window_open) {
            window.stage[s].ticks+=working[s].ticks;
            window.stage[s].calls+=working[s].calls;
            window.stage[s].frames+=working[s].frames;
        }
    }
    if(window_open){window.blocks++;window.frames+=frames;if(!success)window_failed=1;}
    block_open=0;
    bb_profile.blocks++;
}
void bb_profile_worker_wait(uint32_t elapsed) {
    if(!window_open || !block_open || active!=BB_PROF_DRUMS ||
       UINT32_MAX-window.worker_wait_ticks<elapsed || window.worker_joins==UINT32_MAX) {
        bb_profile.errors++;return;
    }
    window.worker_wait_ticks+=elapsed;window.worker_joins++;
}
void bb_profile_worker_run(uint32_t elapsed,int valid) {
    if(!valid || !window_open || !block_open || active!=BB_PROF_DRUMS ||
       window.worker_runs>=window.worker_joins ||
       UINT32_MAX-window.worker_run_ticks<elapsed) {bb_profile.errors++;return;}
    window.worker_run_ticks+=elapsed;window.worker_runs++;
}
void bb_profile_window_begin(void) {
    if(window_open || block_open || active!=BB_PROF_COUNT) {
        bb_profile_windows.errors++;bb_profile.errors++;
    }
    for(unsigned s=0;s<BB_PROF_COUNT;s++)window.stage[s]=(bb_profile_cost){0,0,0};
    window.total_ticks=window.nested_ticks=window.budget_ticks=0;
    window.frames=window.blocks=window.sequence=window.valid=0;
    window.worker_wait_ticks=window.worker_joins=window.worker_run_ticks=window.worker_runs=0;
    window_open=1;window_failed=0;
    window_start_errors=bb_profile.errors;
    /* Recover a malformed previous window, never combine its spans with this one. */
    block_open=0;active=BB_PROF_COUNT;
}
void bb_profile_window_end(uint32_t total,uint32_t nested,uint32_t budget) {
    uint64_t stages=0;
    if(!window_open){bb_profile_windows.errors++;return;}
    window.total_ticks=total;window.nested_ticks=nested;window.budget_ticks=budget;
    window.sequence=++bb_profile_windows.completed;
    for(unsigned s=0;s<BB_PROF_COUNT;s++) {
        stages+=window.stage[s].ticks;
        if(window.stage[s].frames>window.frames)window_failed=1;
    }
    window.valid=!window_failed && !block_open && active==BB_PROF_COUNT &&
        window.blocks && window.frames && bb_profile.errors==window_start_errors &&
        stages<=total && nested<=total &&
        window.worker_wait_ticks<=window.stage[BB_PROF_DRUMS].ticks &&
        window.worker_run_ticks<=total && window.worker_runs==window.worker_joins;
    if(!window.valid)bb_profile_windows.errors++;
    bb_profile_windows.last=window;
    /* Keep ONE complete, correlated peak interval, not per-stage maxima. */
    if(window.valid && (!bb_profile_windows.peak.valid || total>bb_profile_windows.peak.total_ticks))
        bb_profile_windows.peak=window;
    window_open=0;
}
#endif
