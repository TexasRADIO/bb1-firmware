/* SPDX-License-Identifier: GPL-3.0-only */
/* Live completion: validates returned metadata without re-rendering on CPU0.
 * Caller still validates the complete private payload before committing it. */
#ifndef BB_C1_COMPLETION_H
#define BB_C1_COMPLETION_H
#include "bb_c1_probe_protocol.h"
BB_C1_INLINE int bb_c1_complete(const bb_c1_mailbox *mail,uint32_t ticket,uint32_t *out) {
    if(!mail || !ticket || !out)return -1;
    BB_C1_SYNC();
    if(mail->alive!=BB_C1_ALIVE || mail->bad || mail->job!=ticket)return -1;
    uint32_t done=mail->done;
    if(done!=ticket)return done==ticket-1u?0:-1;
    BB_C1_SYNC();uint32_t value=mail->result,check=mail->result_check;
    if(check!=bb_c1_result_check(ticket,value))return -1;
    *out=value;return 1;
}
#endif
