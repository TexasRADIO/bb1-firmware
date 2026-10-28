/* SPDX-License-Identifier: GPL-3.0-only */
#include "bb_home_service.h"
#include <string.h>
static bb_performer home_controller;
static bb_factory_trio_workspace home_workspace;
static bb_factory_trio_report home_report;
int bb_home_reset(const bb_browser *view,unsigned primary) {
    memset(&home_controller,0,sizeof home_controller);
    return bb_performer_init(&home_controller,view,primary);
}
int bb_home_sync(bb_audio *audio){return bb_performer_sync(&home_controller,audio);}
const bb_performer *bb_home_view(void){return &home_controller;}
int bb_home_bank_key(bb_audio *audio,unsigned key,const bb_core *snapshot,unsigned policy,unsigned phrase) {
    return bb_performer_bank_key(&home_controller,audio,key,snapshot,policy,phrase,&home_workspace,&home_report);
}
int bb_home_move(bb_audio *audio,int delta,const bb_core *snapshot,unsigned policy,unsigned phrase) {
    return bb_performer_move(&home_controller,audio,delta,snapshot,policy,phrase,&home_workspace,&home_report);
}
int bb_home_part_key(bb_audio *audio,unsigned key){return bb_performer_part_key(&home_controller,audio,key);}
int bb_home_page(int delta){return bb_performer_page(&home_controller,delta);}

const char *bb_home_content_reason(void) {
    unsigned issues=0;
    for(unsigned i=0;i<BB_TRIO_PARTS;i++)issues|=home_report.part[i].drum_issues;
    if(issues&BB_FDRUM_ISSUE_DUPLICATE)return "DRUM VOICES COLLIDE";
    if(issues&BB_FDRUM_ISSUE_MODIFIER)return "DRUM MODIFIER UNSUPPORTED";
    if(issues&BB_FDRUM_ISSUE_VOICE)return "DRUM VOICE UNAVAILABLE";
    return "SOURCE NOT SUPPORTED";
}
