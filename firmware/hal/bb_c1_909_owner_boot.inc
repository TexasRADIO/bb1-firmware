/* SPDX-License-Identifier: GPL-3.0-only */
/* PRE-AUDIO only. Both actors persist across checked requests. On any
 * failure leave ownership armed: existing boot failure MUST hold CPU1
 * before either union can be reused by normal audio. */
static int bb_c1_909_owner_boot_jobs(void) {
    if(bb_c1_phase!=BB_C1_PHASE_RUNNING || bb_c1_mail.bad
       || bb_c1_mail.job!=bb_c1_mail.done || bb_c1_909_owner_armed)return BB_C1_RESULT_BAD;
    bb_c1_909_owner_report.status=BB_C1_PASS;
    const void *pack=(const void *)(uintptr_t)BB909_PACK_XIP_ADDR;
    uint32_t previous=bb_c1_mail.job;
    if(!bb909_owner_init(&bb_c1_private_pool.owner,previous,pack,BB909_PACK_BYTES)
       || !bb909_owner_init(&bb_c1_phase_pool.owner,previous,pack,BB909_PACK_BYTES)) {
        bb_c1_909_owner_report.status=7u; /* Asset not ready: no owner armed. */
        return BB_C1_PASS;
    }
    bb_c1_909_owner_armed=1u;BB_C1_SYNC();
    for(unsigned c=0;c<8u;c++) {
        bb909_request *r=&bb_c1_909_owner_request;
        *r=(bb909_request){0};r->ticket=bb_c1_mail.job+1u;r->frames=128u;
        if(c==0u) {
            /* Verified Rev/Dly indices, all 11 voices; separate from hits
             * because 22 settings plus 11 triggers exceeds the 32-event cap. */
            static const uint8_t rev[11]={8,7,6,6,6,4,5,5,5,5,5};
            for(unsigned v=0;v<11u;v++) {
                r->events[r->count++]=(bb909_event){0,BB909_EVENT_SET,v,rev[v],48};
                r->events[r->count++]=(bb909_event){0,BB909_EVENT_SET,v,rev[v]+1u,64};
            }
        } else if(c==1u || c==5u) {
            for(unsigned v=0;v<11u;v++)
                r->events[r->count++]=(bb909_event){0,BB909_EVENT_TRIGGER,v,0,100};
        } else if(c==3u) {
            r->events[r->count++]=(bb909_event){32,BB909_EVENT_SET,0,2,96};
            r->events[r->count++]=(bb909_event){64,BB909_EVENT_TRIGGER,0,0,110};
            r->events[r->count++]=(bb909_event){96,BB909_EVENT_TRIGGER,1,0,90};
        }
        r->check=bb909_request_check(r);
        uint32_t expected,ticks,t0=fm1_ticks();
        if(!bb909_owner_run(&bb_c1_phase_pool.owner,r,r->ticket,&expected))goto mismatch;
        bb_c1_909_owner_report.cpu0_ticks[c]=fm1_ticks()-t0;
        int status=bb_c1_dsp_exchange(BB_C1_909_OWNER_JOB,r->ticket,expected,480000u,&ticks);
        bb_c1_909_owner_report.exchange_ticks[c]=ticks;
        if(status!=BB_C1_PASS) {
            bb_c1_909_owner_report.status=status;return status;
        }
        BB_C1_SYNC();
        if(!bb909_owner_accept(&bb_c1_private_pool.owner,r->ticket,expected)
           || !bb_c1_dsp_bytes_equal(&bb_c1_private_pool.owner,&bb_c1_phase_pool.owner,sizeof(bb909_owner)))goto mismatch;
        bb_c1_909_owner_report.worker_ticks[c]=bb_c1_909_owner_worker_ticks;
        bb_c1_909_owner_report.jobs++;
    }
    /* Successful exact comparison AND idle acknowledgement, then disarm.
     * No post-failure path may clear this flag and admit an 808 overwrite. */
    BB_C1_SYNC();
    if(bb_c1_mail.bad || bb_c1_mail.job!=bb_c1_mail.done)goto mismatch;
    bb_c1_909_owner_armed=0u;BB_C1_SYNC();return BB_C1_PASS;
mismatch:
    bb_c1_909_owner_report.status=BB_C1_RESULT_BAD;return BB_C1_RESULT_BAD;
}
