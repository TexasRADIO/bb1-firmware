/* SPDX-License-Identifier: GPL-3.0-only */
/* Main-loop lease. An accepted command is not an audio acknowledgement. */
typedef struct { uint32_t deadline,epoch; unsigned active; } bb_remote_lease;
static int bb_remote_start(bb_remote_lease *s,uint32_t now,uint32_t epoch,
                           int safe,int (*send)(unsigned)) {
    if(s->active || !safe || !send(1))return 0;
    s->deadline=now+30000u;s->epoch=epoch;s->active=1;return 1;
}
static void bb_remote_poll(bb_remote_lease *s,uint32_t now,uint32_t epoch,
                           int connected,int safe,int owned,int (*send)(unsigned)) {
    if(!s->active)return;
    if(s->active==1 && (!connected || !safe || epoch!=s->epoch ||
                       (int32_t)(now-s->deadline)>=0))s->active=2;
    /* Audio ownership may be held by asset or scene service. Retain STOP
     * intent until the normal producer may access the command queue. */
    if(s->active==2 && !owned && send(0))s->active=0;
}
