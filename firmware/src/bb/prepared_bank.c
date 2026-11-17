/* SPDX-License-Identifier: GPL-3.0-only */
#include "prepared_bank.h"
#include "bb_session.h"
#include "bb_wire.h"
#include <string.h>
static int outside(const prepared_bank *b,const void *out,size_t n){
    return bb_wire_separated(b,sizeof *b,out,n)&&(!b->bank.source||bb_wire_separated(b->bank.source,sizeof *b->bank.source,out,n));
}
static int phrase(unsigned n){return n>=16&&n<=BB_STEPS&&!(n%16);}
static int tags(const uint8_t *f){for(unsigned n=0;n<BB_MACHINES;n++)if(f[n]>1)return 0;return 1;}
static void release(prepared_bank *b){
    b->bank.source=0;b->bank.state=PB_IDLE;b->bank.expanded=0;b->bank.cancel=0;b->parts.handoff.state=PP_IDLE;
}
static int reject(prepared_bank *b){release(b);return b->bank.result=BB_PART_REJECTED;}
static uint32_t bank_check_from_program(const prepared_bank *b,uint32_t h){
    h=(h^(b->bank.base_modes&255u))*16777619u;return(h^(b->bank.base_modes>>8))*16777619u;
}
static uint32_t bank_check(const prepared_bank *b){
    return bank_check_from_program(b,bb_trio_check(b->bank.source));
}
int pb_init(prepared_bank *b,const bb_parts *from){
    if(!b||!from||!bb_wire_separated(b,sizeof *b,from,sizeof *from)||!pp_init(&b->parts,from))return 0;
    memset(&b->bank,0,sizeof b->bank);b->bank.valid_mask=255;return 1;
}
void pb_init_empty(prepared_bank *b){
    if(b){memset(b,0,sizeof *b);b->parts.queued=BB_PART_NONE;}
}
int pb_init_core(prepared_bank *b,const bb_core *core,unsigned steps,const uint8_t family[BB_MACHINES]){
    if(!b||!core||!family||!bb_wire_separated(b,sizeof *b,core,sizeof *core))return 0;
    if(!pp_init_core(&b->parts,core,steps,family))return 0;
    memset(&b->bank,0,sizeof b->bank);b->bank.valid_mask=255;return 1;
}
int pb_seed_accepting(const prepared_bank *b){
    if(!b||b->bank.source||b->parts.ready||b->parts.handoff.state!=PP_IDLE)return 0;
    if(b->bank.state==PB_SEED||b->bank.state==PB_SEED_WRITING)return 1;
    return b->bank.state==PB_IDLE&&!b->bank.valid_mask&&b->parts.queued==BB_PART_NONE;
}
int pb_seed_request(prepared_bank *b,const bb_core *core,unsigned steps,
    const uint8_t family[BB_MACHINES],unsigned slot) {
    if(!b||!core||!family||slot>=BB_PART_COUNT||!phrase(steps)||!tags(family)||
       !bb_wire_separated(b,sizeof *b,core,sizeof *core)||!bb_session_state_valid(core)||
       !pb_seed_accepting(b))return BB_PART_REJECTED;
    uint8_t saved[BB_MACHINES];memcpy(saved,family,sizeof saved);
    if(b->bank.state==PB_SEED||b->bank.state==PB_SEED_WRITING) {
        if(memcmp(saved,b->parts.current_family,sizeof saved))return BB_PART_REJECTED;
        b->parts.queued=slot==b->parts.active?BB_PART_NONE:(uint8_t)slot;
        return b->bank.result=b->parts.handoff.result=
            slot==b->parts.active?BB_PART_UNCHANGED:BB_PART_QUEUED;
    }
    if(b->bank.ticket==UINT32_MAX||!compact_capture(&b->parts.scenes[0],core))return BB_PART_REJECTED;
    for(unsigned s=0;s<BB_PART_COUNT;s++){
        b->parts.phrase_steps[s]=(uint8_t)steps;
        memcpy(b->parts.family[s],saved,sizeof saved);
    }
    memcpy(b->parts.current_family,saved,sizeof saved);
    b->parts.active=0;b->parts.queued=slot?((uint8_t)slot):BB_PART_NONE;
    b->parts.position=core->running?(uint8_t)(core->ticks%steps):0;
    b->parts.ready=0;b->parts.launches=0;
    memset(&b->parts.handoff,0,sizeof b->parts.handoff);
    b->bank.ticket++;b->bank.source=0;b->bank.valid_mask=1;b->bank.slot=0;
    b->bank.expanded=0;b->bank.cancel=0;b->bank.state=PB_SEED;
    return b->bank.result=b->parts.handoff.result=
        slot?BB_PART_QUEUED:BB_PART_UNCHANGED;
}
int pb_seed_claim(prepared_bank *b,uint32_t *ticket,unsigned *slot){
    if(!b||!ticket||!slot||!outside(b,ticket,sizeof *ticket)||!outside(b,slot,sizeof *slot)||
       !bb_wire_separated(ticket,sizeof *ticket,slot,sizeof *slot)||b->bank.state!=PB_SEED)return 0;
    for(unsigned s=1;s<BB_PART_COUNT;s++)if(!(b->bank.valid_mask&(1u<<s))){
        b->bank.slot=(uint8_t)s;b->bank.expanded=0;b->bank.state=PB_SEED_WRITING;
        *ticket=b->bank.ticket;*slot=s;return 1;
    }
    return 0;
}
int pb_seed_copy(prepared_bank *b,uint32_t ticket,unsigned slot){
    if(!b||!ticket||b->bank.state!=PB_SEED_WRITING||ticket!=b->bank.ticket||
       slot!=b->bank.slot||!slot||slot>=BB_PART_COUNT||!(b->bank.valid_mask&1u)||
       (b->bank.valid_mask&(1u<<slot)))return 0;
    if(b->bank.expanded){b->bank.expanded=2;return 0;}
    b->bank.expanded=2;
    memcpy(&b->parts.scenes[slot],&b->parts.scenes[0],sizeof b->parts.scenes[slot]);
    b->bank.expanded=1;return 1;
}
int pb_seed_finish(prepared_bank *b,uint32_t ticket,unsigned slot,int valid){
    if(!b||!ticket||b->bank.state!=PB_SEED_WRITING||ticket!=b->bank.ticket||slot!=b->bank.slot)return 0;
    if(b->bank.cancel){pb_init_empty(b);b->bank.result=b->parts.handoff.result=BB_PART_CANCELLED;return 0;}
    if(!valid||b->bank.expanded!=1){b->bank.state=PB_FAILED;b->bank.result=BB_PART_REJECTED;return 0;}
    b->bank.valid_mask|=(uint8_t)(1u<<slot);b->bank.expanded=0;b->bank.state=PB_SEED;
    if(b->bank.valid_mask==255){
        unsigned requested=b->parts.queued;b->parts.queued=BB_PART_NONE;b->parts.ready=1;
        b->bank.state=PB_IDLE;b->bank.slot=0;
        if(requested!=BB_PART_NONE){
            int result=pp_request(&b->parts,requested);
            if(result==BB_PART_REJECTED){b->parts.ready=0;b->bank.state=PB_FAILED;b->bank.result=result;return 0;}
            b->bank.result=(uint8_t)result;
        }
    }
    return 1;
}
int pb_store_suspended(prepared_bank *b,unsigned slot,const bb_core *core,unsigned steps){
    return pb_scenes_ready(b)&&b->parts.handoff.state==PP_IDLE&&core&&outside(b,core,sizeof *core)?
        pp_store_suspended(&b->parts,slot,core,steps):0;
}
int pb_bind_families(prepared_bank *b,const uint8_t family[BB_MACHINES]){
    return pb_scenes_ready(b)?pp_bind_families(&b->parts,family):0;
}
int pb_begin(prepared_bank *b,bb_trio_program *source,uint32_t *ticket){
    if(!b||!source||!ticket||!outside(b,ticket,sizeof *ticket)||!bb_wire_separated(b,sizeof *b,source,sizeof *source)||!bb_wire_separated(source,sizeof *source,ticket,sizeof *ticket)||
       b->parts.ready>1||b->bank.state!=PB_IDLE||b->parts.handoff.state!=PP_IDLE||b->bank.ticket==UINT32_MAX)return 0;
    for(unsigned m=0;m<BB_MACHINES;m++)if(b->parts.current_family[m])return 0;
    b->bank.source=source;b->bank.ticket++;b->bank.state=PB_PREPARING;b->bank.cancel=0;b->bank.expanded=0;
    b->parts.handoff.state=PP_PREPARING;*ticket=b->bank.ticket;return 1;
}
int pb_expand_initial(prepared_bank *b,uint32_t ticket,unsigned rate){
    if(!b||!ticket||b->bank.state!=PB_PREPARING||b->bank.ticket!=ticket)return 0;
    if(b->bank.expanded){b->bank.expanded=2;return 0;}
    b->bank.expanded=2;
    bb_trio_program *source=b->bank.source;
    /* Same initialization domain as the current bb_init. */
    if(!source||rate<8000||rate>192000||source->seed.sample_rate!=rate||!bb_trio_valid(source))return 0;
    bb_core *dst=&source->seed;
    b->bank.base_modes=0;
    for(unsigned v=0;v<BB_DRUM_TRACKS;v++)b->bank.base_modes|=(uint16_t)(dst->drums[source->primary].sound.mode[v]<<v);
    const bb_trio_part *s=&source->part[source->initial];bb_machine *drum=&dst->drums[source->primary];
    memcpy(drum->steps,s->drums,sizeof s->drums);drum->length=s->drum_length;
    for(unsigned v=0;v<BB_DRUM_TRACKS;v++)if(s->mode_mask&(1u<<v))drum->sound.mode[v]=(uint8_t)((s->mode_value>>v)&1u);
    for(unsigned n=0;n<BB_SYNTHS;n++){dst->synths[n].length=s->synth_length[n];bb_trio_unpack_notes(dst->synths[n].notes,s->notes[n],BB_STEPS);}
    if(!bb_session_state_valid(dst))return 0;
    b->bank.source_check=bank_check(b);b->bank.slot=source->initial;b->bank.expanded=1;
    return 1;
}
int pb_publish(prepared_bank *b,uint32_t ticket,int valid){
    if(!b||!ticket||b->bank.state!=PB_PREPARING||b->bank.ticket!=ticket)return 0;
    if(b->bank.cancel){release(b);b->bank.result=BB_PART_CANCELLED;return 0;}
    if(!valid||b->bank.expanded!=1){reject(b);return 0;}
    b->parts.queued=BB_PART_NONE;b->parts.handoff.state=PP_READY;b->bank.state=PB_READY;b->bank.result=BB_PART_QUEUED;return 1;
}
static int commit(prepared_bank *b,bb_core *core,int boundary,
    const bb_trio_program *expected_source,uint32_t expected_check,int checked){
    if(!b||!core||!outside(b,core,sizeof *core)||(boundary!=0&&boundary!=1))return BB_PART_REJECTED;
    if(b->bank.state!=PB_READY||(core->running&&!boundary))return BB_PART_UNCHANGED;
    const bb_trio_program *source=b->bank.source;
    if(!source)return reject(b);
    if(checked&&source!=expected_source)return reject(b);
    /* Keep full coverage and both independent expected comparisons. */
    uint32_t program_check=bb_trio_check(source);
    if(checked&&program_check!=expected_check)return reject(b);
    const bb_core *next=&source->seed;
    if(b->bank.slot>=BB_PART_COUNT||b->bank.slot!=source->initial||b->bank.source_check!=bank_check_from_program(b,program_check)||
       core->sample_rate!=next->sample_rate||!bb_session_state_valid(next)||!bb_session_state_valid(core))return reject(b);
    for(unsigned n=0;n<BB_MACHINES;n++)if(b->parts.current_family[n])return reject(b);
    /* Preserve the original bank until this publication. Explicit bank load
     * replaces it, just as bb_parts_load_trio does; this is not save/undo.
     * Source stays pinned for refill. No decoding or eight-scene copying here.
     * The source checksum also covers the prepared first section and original
     * mode bits. Its full scan still costs time and must be measured. */
    pp_publish_music(core,next);
    for(unsigned n=0;n<BB_PART_COUNT;n++)b->parts.phrase_steps[n]=source->part[n].phrase;
    memset(b->parts.family,0,sizeof b->parts.family);memset(b->parts.current_family,0,sizeof b->parts.current_family);
    b->parts.active=b->bank.slot;b->parts.queued=BB_PART_NONE;b->parts.position=0;b->parts.launches=1;b->parts.ready=1;b->parts.handoff.state=PP_IDLE;
    b->bank.valid_mask=0;b->bank.expanded=0;b->bank.state=PB_REFILL;return b->bank.result=BB_PART_APPLIED;
}
int pb_commit(prepared_bank *b,bb_core *core,int boundary){
    return commit(b,core,boundary,0,0,0);
}
int pb_commit_checked(prepared_bank *b,bb_core *core,int boundary,
    const bb_trio_program *expected_source,uint32_t expected_check){
    return commit(b,core,boundary,expected_source,expected_check,1);
}
int pb_refill_claim(prepared_bank *b,uint32_t *ticket,unsigned *slot){
    if(!b||!ticket||!slot||!outside(b,ticket,sizeof *ticket)||!outside(b,slot,sizeof *slot)||!bb_wire_separated(ticket,sizeof *ticket,slot,sizeof *slot)||b->bank.state!=PB_REFILL)return 0;
    for(unsigned s=0;s<BB_PART_COUNT;s++)if(!(b->bank.valid_mask&(1u<<s))){b->bank.slot=(uint8_t)s;b->bank.expanded=0;b->bank.state=PB_REFILL_WRITING;*ticket=b->bank.ticket;*slot=s;return 1;}
    return 0;
}
int pb_refill_expand(prepared_bank *b,uint32_t ticket,unsigned slot){
    if(!b||!ticket||b->bank.state!=PB_REFILL_WRITING||ticket!=b->bank.ticket||slot!=b->bank.slot||slot>=BB_PART_COUNT)return 0;
    if(b->bank.expanded){b->bank.expanded=2;return 0;}
    b->bank.expanded=2;
    const bb_trio_program *source=b->bank.source;
    if(!source||b->bank.source_check!=bank_check(b)||!bb_trio_valid(source))return 0;
    compact_scene *dst=&b->parts.scenes[slot];const bb_trio_part *s=&source->part[slot];
    if(!compact_capture(dst,&source->seed))return 0;
    bb_machine *drum=&dst->drums[source->primary];memcpy(drum->steps,s->drums,sizeof s->drums);drum->length=s->drum_length;
    for(unsigned v=0;v<BB_DRUM_TRACKS;v++)drum->sound.mode[v]=(uint8_t)(((s->mode_mask&(1u<<v))?s->mode_value:b->bank.base_modes)>>v)&1u;
    for(unsigned n=0;n<BB_SYNTHS;n++){dst->synths[n].length=s->synth_length[n];memcpy(dst->synths[n].notes,s->notes[n],sizeof s->notes[n]);}
    b->bank.expanded=1;return 1;
}
int pb_refill_finish(prepared_bank *b,uint32_t ticket,unsigned slot,int valid){
    if(!b||!ticket||b->bank.state!=PB_REFILL_WRITING||ticket!=b->bank.ticket||slot!=b->bank.slot)return 0;
    if(!valid||b->bank.expanded!=1){b->bank.state=PB_FAILED;b->bank.result=BB_PART_REJECTED;return 0;}
    b->bank.valid_mask|=(uint8_t)(1u<<slot);b->bank.expanded=0;
    if(b->bank.valid_mask==255)release(b);else b->bank.state=PB_REFILL;
    return 1;
}
void pb_cancel_pending(prepared_bank *b){
    if(!b)return;
    if(b->bank.state==PB_SEED){pb_init_empty(b);b->bank.result=b->parts.handoff.result=BB_PART_CANCELLED;return;}
    if(b->bank.state==PB_SEED_WRITING){b->bank.cancel=1;b->bank.result=b->parts.handoff.result=BB_PART_CANCELLED;return;}
    pp_cancel_pending(&b->parts);
    if(b->bank.state==PB_PREPARING){b->bank.cancel=1;b->bank.result=BB_PART_CANCELLED;}
    else if(b->bank.state==PB_READY){release(b);b->bank.result=BB_PART_CANCELLED;}
    /* A committed bank remains live. STOP never releases its refill writer
     * or pinned source, and never implies a rollback to the previous bank. */
}
void pb_cancel(prepared_bank *b){pb_cancel_pending(b);if(b)b->parts.position=0;}
int pb_part_request(prepared_bank *b,unsigned slot){
    if(!b||b->bank.state==PB_PREPARING||b->bank.state==PB_READY||b->bank.state==PB_FAILED)return BB_PART_REJECTED;
    return pp_request(&b->parts,slot);
}
int pb_part_claim(prepared_bank *b,uint32_t *ticket){
    if(!b||!ticket||!outside(b,ticket,sizeof *ticket)||b->bank.state!=PB_IDLE)return 0;
    return pp_claim(&b->parts,ticket);
}
int pb_part_commit(prepared_bank *b,bb_core *core){
    if(!b||!core||!outside(b,core,sizeof *core))return BB_PART_REJECTED;
    return b->bank.state==PB_IDLE?pp_commit(&b->parts,core):BB_PART_UNCHANGED;
}
int pb_before_tick(prepared_bank *b,bb_core *core){
    if(!b||!core||!outside(b,core,sizeof *core))return BB_PART_REJECTED;
    if(b->bank.state==PB_IDLE)return pp_before_tick(&b->parts,core);
    if(!core->running||b->parts.active>=BB_PART_COUNT)return BB_PART_REJECTED;
    unsigned phrase=b->parts.phrase_steps[b->parts.active];
    if(phrase<16||phrase>BB_STEPS||phrase%16||b->parts.position>=phrase)return BB_PART_REJECTED;
    b->parts.position=(uint8_t)((b->parts.position+1u)%phrase);return BB_PART_UNCHANGED;
}
int pb_scenes_ready(const prepared_bank *b){
    return b&&b->bank.state==PB_IDLE&&!b->bank.source&&b->bank.valid_mask==255&&b->parts.ready==1;
}
int pb_part_pending(const prepared_bank *b){
    return b&&((b->bank.state==PB_SEED||b->bank.state==PB_SEED_WRITING)||
        (b->parts.ready&&b->parts.queued!=BB_PART_NONE));
}
int pb_ownership_idle(const prepared_bank *b){
    return b&&b->bank.state==PB_IDLE&&!b->bank.source&&b->parts.handoff.state==PP_IDLE&&
        (!b->parts.ready||b->parts.queued==BB_PART_NONE);
}
