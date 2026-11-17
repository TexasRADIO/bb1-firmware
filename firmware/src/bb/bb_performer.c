/* SPDX-License-Identifier: GPL-3.0-only */
#include "bb_performer.h"
#include <string.h>
static int separated(const void *a,size_t n,const void *b,size_t m) {
    uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;
    return a && b && x<=UINTPTR_MAX-n && y<=UINTPTR_MAX-m && (x+n<=y || y+m<=x);
}
static int outside_audio(const bb_performer *p,const bb_audio *a) {
    if(!p || !p->ready || !a || !separated(p,sizeof *p,a,sizeof *a))return 0;
#if BB_WITH_PARTS && BB_TRIO_EXTERNAL_PROGRAM
    if(a->trio.program && !separated(p,sizeof *p,a->trio.program,sizeof *a->trio.program))return 0;
#endif
    return 1;
}
int bb_performer_init(bb_performer *p,const bb_browser *seed,unsigned primary) {
    bb_factory_choice choice;
    if(!p || !seed || primary>=BB_MACHINES || !separated(p,sizeof *p,seed,sizeof *seed) ||
       !bb_browser_current(seed,&choice) || choice.group!=BB_BROWSER_ERAS ||
       !separated(p,sizeof *p,seed->catalogue,sizeof *seed->catalogue) ||
       !bb_catalogue_private(seed->catalogue,p,sizeof *p) ||
       !bb_catalogue_private(seed->catalogue,seed,sizeof *seed))return 0;
    bb_performer next;memset(&next,0,sizeof next);next.view=*seed;
    next.primary=(uint8_t)primary;next.ready=1;*p=next;return 1;
}
static int sync_with(bb_performer *p,bb_audio *a,const bb_performer_selection *incoming) {
    if(!outside_audio(p,a))return 0;
    bb_performance_status status;
    if(!bb_audio_performance_status(a,&status))return 0;
    if(!status.bank.applied)memset(&p->playing,0,sizeof p->playing);
    else if(incoming && incoming->ticket==status.bank.applied)p->playing=*incoming;
    else if(p->request.ticket==status.bank.applied)p->playing=p->request;
    else if(p->playing.ticket!=status.bank.applied)memset(&p->playing,0,sizeof p->playing);
    p->status=status;return 1;
}
int bb_performer_sync(bb_performer *p,bb_audio *a){return sync_with(p,a,NULL);}
static int launch(bb_performer *p,bb_audio *a,const bb_browser *view,const bb_core *snapshot,
    unsigned policy,unsigned phrase,bb_factory_trio_workspace *work,bb_factory_trio_report *report) {
    if(!outside_audio(p,a) || !snapshot || !work || !report ||
       !separated(p,sizeof *p,snapshot,sizeof *snapshot) ||
       !separated(p,sizeof *p,work,sizeof *work) || !separated(p,sizeof *p,report,sizeof *report))return BB_FTRIO_INVALID;
    bb_performer_selection incoming;memset(&incoming,0,sizeof incoming);
    if(!bb_browser_current(view,&incoming.choice) || incoming.choice.group!=BB_BROWSER_ERAS)return BB_FTRIO_INVALID;
    (void)bb_performer_sync(p,a);
    int result=bb_factory_request_trio(a,view->catalogue,&incoming.choice,snapshot,p->primary,
        policy,phrase,work,report,&incoming.ticket);
    p->view=*view;p->error=(uint16_t)result;
    if(result==BB_FTRIO_OK) {
        incoming.capabilities=report->capabilities;
        /* Preserve old identity if audio applied it while the producer worked;
         * also recognize an incoming bank consumed before this call returns. */
        (void)sync_with(p,a,&incoming);p->request=incoming;
    }
    return result;
}
int bb_performer_bank_key(bb_performer *p,bb_audio *a,unsigned key,const bb_core *snapshot,
    unsigned policy,unsigned phrase,bb_factory_trio_workspace *work,bb_factory_trio_report *report) {
    if(!p || !p->ready)return BB_FTRIO_INVALID;
    bb_browser view=p->view;
    if(!bb_browser_bank_key(&view,key,NULL))return BB_FTRIO_INVALID;
    return launch(p,a,&view,snapshot,policy,phrase,work,report);
}
int bb_performer_move(bb_performer *p,bb_audio *a,int delta,const bb_core *snapshot,
    unsigned policy,unsigned phrase,bb_factory_trio_workspace *work,bb_factory_trio_report *report) {
    if(!p || !p->ready)return BB_FTRIO_INVALID;
    bb_browser view=p->view;
    if(!bb_browser_move_bank(&view,delta))return BB_FTRIO_INVALID;
    return launch(p,a,&view,snapshot,policy,phrase,work,report);
}
int bb_performer_page(bb_performer *p,int delta) {
    return p && p->ready?bb_browser_move_bank_page(&p->view,delta):0;
}
int bb_performer_part_key(bb_performer *p,bb_audio *a,unsigned key) {
    if(!outside_audio(p,a) || key>=BB_TRIO_PARTS)return BB_FTRIO_INVALID;
#if BB_WITH_PARTS
    int accepted=bb_audio_part_request(a,key);
    int synced=bb_performer_sync(p,a);
    int result=accepted?BB_FTRIO_OK:!synced?BB_FTRIO_REQUEST_UNAVAILABLE:
        !p->status.ready?BB_PERFORMER_NO_BANK:BB_FTRIO_REQUEST_BUSY;
#else
    int result=BB_FTRIO_REQUEST_UNAVAILABLE;
#endif
    p->error=(uint16_t)result;return result;
}
unsigned bb_performer_pending(const bb_performer *p) {
    if(!p || !p->ready || !p->request.ticket || p->status.bank.applied==p->request.ticket)return 0;
    if(p->status.bank.queued==p->request.ticket)return 2;
    /* Trio allocation stops at UINT32_MAX rather than wrapping. */
    return p->status.bank.seen>=p->request.ticket?0u:1u;
}
