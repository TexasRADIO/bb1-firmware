/* SPDX-License-Identifier: GPL-3.0-only */
/* DIAGNOSTIC ONLY. Include after the fixed mailbox declaration.
 * CPU1 is the sole timestamp writer during the private experiment. CPU0
 * initializes/calibrates only with a verified idle mailbox, before enabling
 * private rendering. CPU0 samples after verified completion/payload checks
 * and before the next render can publish another job. No DSP payload reads.
 * One last-completed job per block is sampled, NOT every event-split job.
 * Intervals include observer overhead. Calibration is never subtracted.
 */
#ifndef BB_PRIVATE_PHASE_COST_H
#define BB_PRIVATE_PHASE_COST_H
#include <stdint.h>
static volatile uint32_t bb_private_cost_enabled;
static volatile struct {
    uint32_t ticket,mask,stamp[6];
} bb_private_cost_record;
static struct {
    uint32_t samples,errors,last_ticket;
    uint32_t minimum[5],maximum[5],sum[5];
    uint32_t calibration_min,calibration_max,calibration_count;
    uint32_t failed_record_ticket,failed_record_mask;
} bb_private_cost;
static void bb_private_phase_stamp(uint32_t stage){
    if(!bb_private_cost_enabled)return;
    if(stage==BB_C1_DPH_INPUT){
        bb_private_cost_record.mask=0;
        bb_private_cost_record.ticket=bb_c1_mail.job;
    }
    if(stage<BB_C1_DPH_INPUT||stage>BB_C1_DPH_RETURN){
        bb_private_cost_record.mask|=0x80000000u;return;
    }
    unsigned index=stage-BB_C1_DPH_INPUT;
    bb_private_cost_record.stamp[index]=fm1_ticks();
    bb_private_cost_record.mask|=1u<<index;
}
static int bb_private_phase_begin(void){
    if(bb_private_cost_enabled||bb_c1_mail.bad||bb_c1_mail.job!=bb_c1_mail.done)return 0;
    unsigned char *zero=(unsigned char *)&bb_private_cost;
    for(unsigned i=0;i<sizeof bb_private_cost;i++)zero[i]=0;
    for(unsigned i=0;i<5;i++)bb_private_cost.minimum[i]=UINT32_MAX;
    bb_private_cost.calibration_min=UINT32_MAX;
    /* The worker is idle: these are deliberate CPU0 calibration writes,
     * never simultaneous with a published job. Brackets include their own
     * clock-read overhead and omit DSP; they are not correction constants. */
    bb_private_cost_enabled=1;BB_C1_SYNC();
    for(unsigned repeat=0;repeat<16;repeat++){
        uint32_t start=fm1_ticks();
        for(unsigned stage=BB_C1_DPH_INPUT;stage<=BB_C1_DPH_RETURN;stage++)bb_private_phase_stamp(stage);
        uint32_t elapsed=fm1_ticks()-start;
        if(elapsed<bb_private_cost.calibration_min)bb_private_cost.calibration_min=elapsed;
        if(elapsed>bb_private_cost.calibration_max)bb_private_cost.calibration_max=elapsed;
        bb_private_cost.calibration_count++;
    }
    bb_private_cost_record.mask=0;bb_private_cost_record.ticket=0;
    BB_C1_SYNC();return 1;
}
static int bb_private_phase_collect(uint32_t ticket){
    if(!bb_private_cost_enabled||!ticket||ticket==bb_private_cost.last_ticket)return 1;
    BB_C1_SYNC();
    if(bb_c1_mail.bad||bb_c1_mail.job!=ticket||bb_c1_mail.done!=ticket||
       bb_private_cost_record.ticket!=ticket||bb_private_cost_record.mask!=63u)goto bad;
    uint32_t delta[5];
    for(unsigned i=0;i<5;i++){
        delta[i]=bb_private_cost_record.stamp[i+1]-bb_private_cost_record.stamp[i];
        /* Telemetry plausibility bound only; never a changed worker deadline. */
        if(delta[i]>480000u||UINT32_MAX-bb_private_cost.sum[i]<delta[i])goto bad;
    }
    for(unsigned i=0;i<5;i++){
        if(delta[i]<bb_private_cost.minimum[i])bb_private_cost.minimum[i]=delta[i];
        if(delta[i]>bb_private_cost.maximum[i])bb_private_cost.maximum[i]=delta[i];
        bb_private_cost.sum[i]+=delta[i];
    }
    bb_private_cost.last_ticket=ticket;bb_private_cost.samples++;return 1;
bad:
    bb_private_cost.errors++;return 0;
}
static int bb_private_phase_end(uint32_t failed,uint32_t held,uint32_t failed_ticket){
    /* Called only once the worker is idle or its hold was verified. Never
     * inspect a failed drum job. Only this independent trace record is read. */
    if(failed?!held:bb_c1_mail.job!=bb_c1_mail.done){bb_private_cost.errors++;return 0;}
    bb_private_cost_enabled=0;BB_C1_SYNC();
    if(failed){
        bb_private_cost.failed_record_ticket=bb_private_cost_record.ticket;
        if(bb_private_cost_record.ticket==failed_ticket)
            bb_private_cost.failed_record_mask=bb_private_cost_record.mask;
    }
    return 1;
}
#endif
