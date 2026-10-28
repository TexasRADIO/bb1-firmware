/* SPDX-License-Identifier: GPL-3.0-only */
/* CPU0 PRE-AUDIO only. Both scratch lifetimes are exclusively owned here.
 * A failed exchange returns without inspecting or reusing worker payload;
 * the existing caller must verify hardware hold before starting audio. */
static int bb_c1_606_boot_jobs(void) {
    if(bb_c1_phase!=BB_C1_PHASE_RUNNING || bb_c1_mail.bad
       || bb_c1_mail.job!=bb_c1_mail.done)return BB_C1_MAILBOX_BAD;
    bb_c1_606_report.status=BB_C1_RESULT_BAD;
    for(unsigned c=0;c<8u;c++) {
        uint32_t ticket=bb_c1_mail.job+1u,expected,ticks,t0;
        if(!ticket)return BB_C1_MAILBOX_BAD;
        unsigned mask=c<7u?1u<<c:127u;
        bb_c1_606_record *r=&bb_c1_606_report.record[c];
        r->mask=mask;r->frames=128;r->exact=0;
        if(!bb606_probe_prepare(&bb_c1_phase_pool.six,ticket,mask,128,127))return BB_C1_MAILBOX_BAD;
        t0=fm1_ticks();
        bb_c1_drum_copy(&bb_c1_private_pool.six,&bb_c1_phase_pool.six,sizeof(bb606_probe_job));
        r->copy_ticks=fm1_ticks()-t0;
        t0=fm1_ticks();
        if(!bb606_probe_run_timed(&bb_c1_phase_pool.six,ticket,&expected,&r->cpu0))return BB_C1_RESULT_BAD;
        r->cpu0_ticks=fm1_ticks()-t0;
        /* Startup-only 20ms wait, not the live-audio deadline. */
        int status=bb_c1_dsp_exchange(BB_C1_606_JOB,ticket,expected,480000u,&ticks);
        r->exchange_ticks=ticks;
        if(status!=BB_C1_PASS){bb_c1_606_report.status=status;return status;}
        r->cpu1_ticks=bb_c1_606_work_ticks;
        r->cpu1=bb_c1_606_work_phases;
        if(!r->cpu0.valid || !r->cpu1.valid)return BB_C1_RESULT_BAD;
        /* RAM acknowledgement follows return from the fixed worker. */
        if(!bb606_probe_accept(&bb_c1_private_pool.six,ticket,expected)
           || !bb_c1_dsp_bytes_equal(&bb_c1_private_pool.six,&bb_c1_phase_pool.six,sizeof(bb606_probe_job)))
            return BB_C1_RESULT_BAD;
        r->exact=1;bb_c1_606_report.jobs++;
    }
    bb_c1_606_report.status=BB_C1_PASS;return BB_C1_PASS;
}
