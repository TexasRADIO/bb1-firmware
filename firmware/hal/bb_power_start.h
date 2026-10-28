#pragma once
#include "fm1_power.h"
/* Startup-only: CPU1 held, IRQs off. Raw register bytes, not inferred levels.
 * status: 0 unattempted, 1 passed, 2 initial read failed, 3 setup failed. */
typedef struct { uint32_t status, before, after, timeouts; } bb_power_record;
bb_power_record bb_power_boot;
static int bb_power_start(void) {
    uint32_t start=fm1_p33_timeouts;
    uint32_t vdc=fm1_p33_read(0x06u),sys=fm1_p33_read(0x09u);
    bb_power_boot.before=vdc|(sys<<8);
    if(fm1_p33_timeouts!=start) {
        bb_power_boot.status=2;bb_power_boot.timeouts=fm1_p33_timeouts-start;return 0;
    }
    int ok=fm1_power_init();
    if(ok) {
        vdc=fm1_p33_read(0x06u);sys=fm1_p33_read(0x09u);
        bb_power_boot.after=vdc|(sys<<8);
    }
    bb_power_boot.timeouts=fm1_p33_timeouts-start;
    ok=ok&&!bb_power_boot.timeouts;
    bb_power_boot.status=ok?1:3;return ok;
}
