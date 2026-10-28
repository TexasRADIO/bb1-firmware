/* SPDX-License-Identifier: GPL-3.0-only */
/* Big Beat diagnostic mailbox, informed by fm1-x0x/Melodee's single-writer,
 * separated-cache-line protocol. No engine pointers or live audio state. */
#ifndef BB_C1_PROBE_PROTOCOL_H
#define BB_C1_PROBE_PROTOCOL_H
#include <stdint.h>
#include <stddef.h>
#ifndef BB_C1_SYNC
#define BB_C1_SYNC() __asm__ volatile("csync" ::: "memory")
#endif
#define BB_C1_INLINE static inline __attribute__((always_inline))
#define BB_C1_ALIVE UINT32_C(0xC1A11E01)
#define BB_C1_GUARD UINT32_C(0xB16BEA71)
typedef struct {
    volatile uint32_t job,seed,nonce,check; /* CPU0 is the sole writer. */
    uint32_t pad0[12];
    volatile uint32_t alive,done,result,result_check,bad,bad_job; /* CPU1 only. */
    /* CPU1 timing occupies reserved output-line words; no mailbox offsets move. */
    volatile uint32_t timed_job,timed_ticks,timed_check;
    uint32_t pad1[7];
} bb_c1_mailbox;
typedef char bb_c1_output_line_must_be_separate[(offsetof(bb_c1_mailbox,alive)==64)?1:-1];
typedef char bb_c1_mailbox_must_have_two_lines[(sizeof(bb_c1_mailbox)==128)?1:-1];

BB_C1_INLINE uint32_t bb_c1_request_check(uint32_t job,uint32_t seed,uint32_t nonce) {
    return BB_C1_GUARD ^ job*UINT32_C(0x9E3779B1) ^ seed ^ ((nonce<<7)|(nonce>>25));
}
BB_C1_INLINE uint32_t bb_c1_calculate(uint32_t seed,uint32_t nonce) {
    uint32_t result=seed^UINT32_C(0xA51D1110);
    for(unsigned i=0;i<8;i++) {
        result^=nonce+UINT32_C(0x9E3779B9)+i;
        result=(result<<5)|(result>>27);
        result=result*UINT32_C(1664525)+UINT32_C(1013904223);
    }
    return result;
}
BB_C1_INLINE uint32_t bb_c1_result_check(uint32_t job,uint32_t result) {
    return BB_C1_GUARD ^ job*UINT32_C(0x85EBCA6B) ^ result;
}
/* One bounded worker iteration. The completed sequence is held in the
 * worker's register-local last_job, not repeatedly trusted from shared RAM. */
BB_C1_INLINE int bb_c1_worker_step(bb_c1_mailbox *mail,uint32_t *last_job) {
    uint32_t job=mail->job;BB_C1_SYNC();
    if(job==*last_job)return 0;
    uint32_t seed=mail->seed,nonce=mail->nonce,check=mail->check;
    if(job!=*last_job+1u || check!=bb_c1_request_check(job,seed,nonce)) {
        mail->bad_job=job;BB_C1_SYNC();mail->bad=1;BB_C1_SYNC();return -1;
    }
    uint32_t result=bb_c1_calculate(seed,nonce);
    mail->result=result;mail->result_check=bb_c1_result_check(job,result);
    *last_job=job;BB_C1_SYNC();mail->done=job;BB_C1_SYNC();return 1;
}
/* Refuse a second outstanding job or a previously bad mailbox. */
BB_C1_INLINE int bb_c1_publish(bb_c1_mailbox *mail,uint32_t seed,uint32_t nonce) {
    BB_C1_SYNC();
    if(mail->alive!=BB_C1_ALIVE || mail->bad || mail->job!=mail->done)return 0;
    uint32_t job=mail->job+1u;
    mail->seed=seed;mail->nonce=nonce;mail->check=bb_c1_request_check(job,seed,nonce);
    BB_C1_SYNC();mail->job=job;BB_C1_SYNC();return 1;
}
/* 0 waiting, 1 verified, -1 bad sequence/data. Never replay an uncertain job. */
BB_C1_INLINE int bb_c1_verify(const bb_c1_mailbox *mail,uint32_t job,uint32_t expected) {
    BB_C1_SYNC();
    if(mail->bad)return -1;
    uint32_t done=mail->done;
    if(done!=job)return done==job-1u?0:-1;
    BB_C1_SYNC();uint32_t result=mail->result,check=mail->result_check;
    return result==expected && check==bb_c1_result_check(job,result)?1:-1;
}
#endif
