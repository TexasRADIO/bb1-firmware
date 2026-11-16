/* SPDX-License-Identifier: GPL-3.0-only */
#include "prepared_parts.h"
#include "bb_session.h"
#include "bb_wire.h"
#include <string.h>
static int phrase(unsigned n){return n>=16&&n<=BB_STEPS&&!(n%16);}
static int tags(const uint8_t *f){for(unsigned n=0;n<BB_MACHINES;n++)if(f[n]>1)return 0;return 1;}
static int ready(const prepared_parts *p){return p&&p->ready==1&&p->active<BB_PART_COUNT&&phrase(p->phrase_steps[p->active])&&p->position<p->phrase_steps[p->active]&&tags(p->current_family);}
static int match(const prepared_parts *p,unsigned slot){return tags(p->family[slot])&&!memcmp(p->family[slot],p->current_family,BB_MACHINES);}
uint32_t pp_core_check(const bb_core *c){const unsigned char *b=(const unsigned char *)c;uint32_t h=2166136261u;for(unsigned n=0;n<sizeof *c;n++)h=(h^b[n])*16777619u;return h;}
int pp_init(prepared_parts *p,const bb_parts *from){
    if(!p||!from||!bb_wire_separated(p,sizeof *p,from,sizeof *from)||from->ready!=1||from->active>=BB_PART_COUNT||from->queued!=BB_PART_NONE||!tags(from->current_family))return 0;
    for(unsigned s=0;s<BB_PART_COUNT;s++)if(!phrase(from->phrase_steps[s])||!tags(from->family[s])||!bb_session_state_valid(&from->scenes[s]))return 0;
    if(from->position>=from->phrase_steps[from->active])return 0;
    /* Initialization is exclusive and offline, never in a running callback. */
    memset(p,0,sizeof *p);
    for(unsigned s=0;s<BB_PART_COUNT;s++){if(!compact_capture(&p->scenes[s],&from->scenes[s]))return 0;p->phrase_steps[s]=from->phrase_steps[s];}
    memcpy(p->family,from->family,sizeof p->family);memcpy(p->current_family,from->current_family,sizeof p->current_family);
    p->active=from->active;p->position=from->position;p->launches=from->launches;p->queued=BB_PART_NONE;p->ready=1;return 1;
}
int pp_init_core(prepared_parts *p,const bb_core *core,unsigned steps,const uint8_t family[BB_MACHINES]){
    if(!p||!core||!family||!bb_wire_separated(p,sizeof *p,core,sizeof *core)||!phrase(steps)||!tags(family)||!bb_session_state_valid(core))return 0;
    uint8_t saved_family[BB_MACHINES];memcpy(saved_family,family,sizeof saved_family);
    /* Exclusive initialization. All checks precede the first write, and
     * compact_capture cannot fail for these validated disjoint arguments. */
    memset(p,0,sizeof *p);
    if(!compact_capture(&p->scenes[0],core))return 0;
    for(unsigned s=0;s<BB_PART_COUNT;s++){
        if(s)memcpy(&p->scenes[s],&p->scenes[0],sizeof p->scenes[s]);
        p->phrase_steps[s]=(uint8_t)steps;memcpy(p->family[s],saved_family,sizeof saved_family);
    }
    memcpy(p->current_family,saved_family,sizeof saved_family);
    p->queued=BB_PART_NONE;p->ready=1;
    if(core->running)p->position=(uint8_t)(core->ticks%steps);
    return 1;
}
int pp_store_suspended(prepared_parts *p,unsigned slot,const bb_core *core,unsigned steps){
    if(!ready(p)||!core||p->handoff.state!=PP_IDLE||slot>=BB_PART_COUNT||slot==p->active||slot==p->queued||
       !phrase(steps)||!bb_wire_separated(p,sizeof *p,core,sizeof *core)||!bb_session_state_valid(core))return 0;
    if(!compact_capture(&p->scenes[slot],core))return 0;
    p->phrase_steps[slot]=(uint8_t)steps;memcpy(p->family[slot],p->current_family,BB_MACHINES);return 1;
}
int pp_bind_families(prepared_parts *p,const uint8_t family[BB_MACHINES]){
    if(!ready(p)||!family||!tags(family))return 0;
    uint8_t saved[BB_MACHINES];memcpy(saved,family,sizeof saved);memcpy(p->current_family,saved,sizeof saved);return 1;
}
void pp_cancel_pending(prepared_parts *p){
    if(!p)return;
    p->queued=BB_PART_NONE;p->handoff.result=BB_PART_CANCELLED;
    /* A cancelled writer retains its lease until finish; never free scratch
     * while the main loop can still be writing it. */
    if(p->handoff.state==PP_READY)p->handoff.state=PP_IDLE;
}
void pp_cancel(prepared_parts *p){pp_cancel_pending(p);if(p)p->position=0;}
int pp_request(prepared_parts *p,unsigned slot){
    if(!ready(p)||slot>=BB_PART_COUNT||!phrase(p->phrase_steps[slot])||!match(p,slot)||p->handoff.serial==UINT32_MAX)return BB_PART_REJECTED;
    p->handoff.serial++;p->queued=slot==p->active?BB_PART_NONE:(uint8_t)slot;
    if(p->handoff.state==PP_READY)p->handoff.state=PP_IDLE;
    return p->handoff.result=slot==p->active?BB_PART_UNCHANGED:BB_PART_QUEUED;
}
int pp_claim(prepared_parts *p,uint32_t *ticket){
    if(!ready(p)||!ticket||!bb_wire_separated(p,sizeof *p,ticket,sizeof *ticket)||p->handoff.state!=PP_IDLE||p->queued>=BB_PART_COUNT||p->queued==p->active||!match(p,p->queued))return 0;
    p->handoff.work_serial=p->handoff.serial;p->handoff.work_slot=p->queued;p->handoff.expanded=0;p->handoff.state=PP_PREPARING;*ticket=p->handoff.work_serial;return 1;
}
int pp_expand(prepared_parts *p,uint32_t ticket,unsigned rate){
    if(!p||!ticket||p->handoff.state!=PP_PREPARING||p->handoff.work_serial!=ticket||p->handoff.work_slot>=BB_PART_COUNT)return 0;
    /* work_slot remains inactive for the lifetime of this lease. Request and
     * STOP may invalidate its serial but may not publish or reuse scratch. */
    int valid=compact_prepare(&p->prepared,&p->scenes[p->handoff.work_slot],rate);
    if(valid)p->handoff.checksum=pp_core_check(&p->prepared);
    p->handoff.expanded=(uint8_t)(valid!=0);
    return valid;
}
int pp_finish(prepared_parts *p,uint32_t ticket,int valid){
    if(!p||!ticket||p->handoff.state!=PP_PREPARING||p->handoff.work_serial!=ticket)return 0;
    if(ticket!=p->handoff.serial||p->queued!=p->handoff.work_slot){p->handoff.state=PP_IDLE;return 0;}
    if(!valid||!p->handoff.expanded){p->handoff.state=PP_IDLE;p->queued=BB_PART_NONE;p->handoff.result=BB_PART_REJECTED;return 0;}
    p->handoff.state=PP_READY;return 1;
}
/* Same explicit musical publication as the current bb_parts.c. Runtime
 * clocks, transport and navigation remain owned by the live core. */
void pp_publish_music(bb_core *core,const bb_core *next){
    for(unsigned i=0;i<BB_MACHINES;i++){
        uint32_t revision=core->drums[i].sound_revision;
        if(memcmp(&core->drums[i].sound,&next->drums[i].sound,sizeof next->drums[i].sound))revision++;
        core->drums[i]=next->drums[i];core->drums[i].next=0;core->drums[i].sound_revision=revision;
    }
    for(unsigned i=0;i<BB_SYNTHS;i++){core->synths[i]=next->synths[i];core->synths[i].next=0;}
    core->dillo=next->dillo;core->telegram=next->telegram;core->effects=next->effects;
    memcpy(core->band_level,next->band_level,sizeof core->band_level);core->bpm=next->bpm;core->swing=next->swing;
    if(!core->swing_odd)core->active_swing=core->swing;
}
int pp_commit(prepared_parts *p,bb_core *core){
    if(!ready(p)||!core||!bb_wire_separated(p,sizeof *p,core,sizeof *core))return BB_PART_REJECTED;
    if(core->running&&p->position)return BB_PART_UNCHANGED;
    if(p->handoff.state!=PP_READY)return BB_PART_UNCHANGED;
    unsigned slot=p->handoff.work_slot;
    if(p->handoff.work_serial!=p->handoff.serial||slot!=p->queued||slot>=BB_PART_COUNT||slot==p->active||!phrase(p->phrase_steps[slot])||!match(p,slot)||
       p->prepared.sample_rate!=core->sample_rate||p->handoff.checksum!=pp_core_check(&p->prepared)||!bb_session_state_valid(&p->prepared)||!bb_session_state_valid(core)){
        p->handoff.state=PP_IDLE;p->queued=BB_PART_NONE;return p->handoff.result=BB_PART_REJECTED;
    }
    /* Only the current outgoing section is packed on the audio owner. This
     * remaining cost must be measured; it is not called free or constant-time. */
    if(!compact_capture(&p->scenes[p->active],core))return BB_PART_REJECTED;
    memcpy(p->family[p->active],p->current_family,BB_MACHINES);pp_publish_music(core,&p->prepared);
    p->active=(uint8_t)slot;p->queued=BB_PART_NONE;p->position=0;p->launches++;
    p->handoff.state=PP_IDLE;return p->handoff.result=BB_PART_APPLIED;
}
int pp_before_tick(prepared_parts *p,bb_core *core){
    if(!ready(p)||!core||!core->running)return BB_PART_REJECTED;
    int result=BB_PART_UNCHANGED;
    if(!p->position&&p->queued!=BB_PART_NONE)result=pp_commit(p,core);
    p->position=(uint8_t)((p->position+1u)%p->phrase_steps[p->active]);return result;
}
