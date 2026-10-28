/* SPDX-License-Identifier: GPL-3.0-only */
/* Startup-only diagnostic. No live state, heap, peripheral or worker access.
 * Caller must enforce pre-audio admission. Timing includes sample stores and
 * two clock calls, excludes snapshot copying and result validation. */
#include "dense-peak-fixture-r1.inc"
#include "dense_sweep_api.h"
static int bb_dense_fixture_sweep(bb_dense_sweep_result *out,uint32_t (*clock_read)(void)) {
    if(!out || !clock_read) return 0;
    bb_bd_tick_fn prepared_bd_tick=bb_bd_tick_exec_target();
    bb_sd_tick_fn prepared_sd_tick=bb_sd_tick_exec_target();
    bb_tom_tick_fn prepared_tom_tick=bb_tom_tick_exec_target();
    memset(out,0,sizeof *out);
    const float one=1.0f; const uint32_t endian=0x04030201u;
    const unsigned char ieee[4]={0,0,128,63},little[4]={1,2,3,4};
    if(memcmp(&one,ieee,4) || memcmp(&endian,little,4)) return 0;
    union { d8_bd_t bd; d8_sd_t sd; d8_tom_t tom; } state;
    float samples[64];
    for(unsigned c=0;c<12;c++) {
        unsigned voice=bb_dense_fixture[c].voice,n=bb_dense_fixture[c].frames;
        unsigned bytes=voice==0?sizeof(d8_bd_t):voice==1?sizeof(d8_sd_t):sizeof(d8_tom_t);
        if(voice>3 || !n || n>64 || bytes!=bb_dense_fixture[c].bytes) return 0;
        memcpy(&state,bb_dense_fixture[c].before,bytes);
        uint32_t start=clock_read();
        if(voice==0) for(unsigned i=0;i<n;i++) samples[i]=state.bd.active?prepared_bd_tick(&state.bd):0;
        else if(voice==1) for(unsigned i=0;i<n;i++) samples[i]=state.sd.active?prepared_sd_tick(&state.sd):0;
        else for(unsigned i=0;i<n;i++) samples[i]=state.tom.active?prepared_tom_tick(&state.tom):0;
        uint32_t elapsed=clock_read()-start;
        if(memcmp(&state,bb_dense_fixture[c].after,bytes)) return 0;
        for(unsigned i=0;i<n;i++) if(!(samples[i]>=-64 && samples[i]<=64)) return 0;
        out->ticks[c]=elapsed;out->cases++;
    }
    out->passed=1;return 1;
}
