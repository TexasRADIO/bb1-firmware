#include "bb_audio.h"
#include "bb_audio_trio.h"
#include <string.h>
#if BB_WITH_PATTERN
static int outside(const bb_audio *a,const void *p,size_t length) {
    uintptr_t begin=(uintptr_t)p,owner=(uintptr_t)a;
    if(!p || begin>UINTPTR_MAX-length || owner>UINTPTR_MAX-sizeof *a)return 0;
    return bb_audio_groups_outside(a,p,length);
}
static int phrase_valid(unsigned n) { return n==16 || n==32 || n==48 || n==64; }
static int data_valid(const bb_pattern_data *data) {
    if(!data || !data->length || data->length>BB_STEPS)return 0;
    for(unsigned i=0;i<BB_STEPS;i++) {
        const bb_note *n=&data->notes[i];
        /* Latent accent/slide on a rest is meaningful Studio source state. */
        if(n->note>127 || n->gate>1 || n->accent>1 || n->slide>1)return 0;
        if(data->expression[i]) {
            bb_note_expression_view expression;
            if(!bb_note_expression_read(data->expression[i],&expression)||
               (expression.has_velocity&&!expression.velocity)||
               (expression.has_gate&&!expression.gate_percent))return 0;
        }
    }
    return 1;
}
static uintptr_t enter(bb_audio *a) { return a->commands.enter?a->commands.enter(a->commands.lock_context):0; }
static void leave(bb_audio *a,uintptr_t token) { if(a->commands.leave)a->commands.leave(a->commands.lock_context,token); }
int bb_audio_pattern_submit(bb_audio *a,unsigned synth,const bb_pattern_data *data,unsigned phrase,uint32_t *out) {
    if(!a || !a->patterns.ready || synth>=BB_SYNTHS || !phrase_valid(phrase) ||
       !outside(a,data,sizeof *data) || (out && !outside(a,out,sizeof *out)) ||
       (!a->commands.enter!=!a->commands.leave) || !data_valid(data))return 0;
    if(out && !((uintptr_t)out+sizeof *out<=(uintptr_t)data ||
                (uintptr_t)data+sizeof *data<=(uintptr_t)out))return 0;
    uintptr_t token=enter(a);unsigned slot;
    for(slot=0;slot<BB_PATTERN_SLOTS;slot++)if(a->patterns.inbox[slot].state==BB_PATTERN_SLOT_FREE)break;
    if(slot==BB_PATTERN_SLOTS) { leave(a,token);return 0; }
    bb_pattern_slot *payload=&a->patterns.inbox[slot];
    uint32_t ticket=++a->patterns.next_ticket;
    if(!ticket)ticket=++a->patterns.next_ticket;
    payload->ticket=ticket;payload->state=BB_PATTERN_SLOT_WRITING;leave(a,token);
    /* Audio never reads a WRITING slot. Keep the complete copy and all source
     * reads out of the interrupt-masked region. Source is stable caller storage. */
    payload->data=*data;payload->synth=(uint8_t)synth;payload->phrase=(uint8_t)phrase;
    token=enter(a);payload->state=BB_PATTERN_SLOT_QUEUED;leave(a,token);
    bb_command command={0};command.type=BB_CMD_SYNTH_PATTERN;command.target=(uint8_t)synth;
    command.step=(uint8_t)slot;command.value=(uint16_t)ticket;
    command.voice=(uint8_t)(ticket>>16);command.note.note=(uint8_t)(ticket>>24);
    if(!bb_command_push(&a->commands,&command)) {
        token=enter(a);payload->state=BB_PATTERN_SLOT_FREE;leave(a,token);return 0;
    }
    if(out)*out=ticket;
    return 1;
}
int bb_audio_pattern_status(bb_audio *a,unsigned synth,bb_pattern_status *out) {
    if(!a || !a->patterns.ready || synth>=BB_SYNTHS || !outside(a,out,sizeof *out) ||
       (!a->commands.enter!=!a->commands.leave))return 0;
    uintptr_t token=enter(a);*out=a->patterns.status[synth];leave(a,token);return 1;
}
void bb_audio_pattern_cancel(bb_audio *a,int clear_labels) {
    bb_audio_drum_pattern_cancel(a,clear_labels);
    for(unsigned s=0;s<BB_SYNTHS;s++) {
        bb_pattern_status *status=&a->patterns.status[s];
        if(a->patterns.pending[s].ready)status->result=BB_PATTERN_CANCELLED;
        a->patterns.pending[s].ready=0;status->queued=0;
        if(clear_labels) {status->applied=0;status->applied_frame=0;}
    }
}
void bb_audio_pattern_transport(bb_audio *a) {
    bb_audio_pattern_cancel(a,0);a->patterns.position=0;
}
static void apply_pattern(bb_audio *a,unsigned s) {
    bb_pattern_pending *pending=&a->patterns.pending[s];
#if BB_WITH_EVOLVE
    int matching=!a->evolve.ready || bb_audio_evolve_matches(a);
    if(a->evolve_home_pending) {
        a->evolve_home_pending=0;a->evolve_result=BB_EV_RESULT_CANCELLED;
    }
#endif
    memcpy(a->core.synths[s].notes,pending->data.notes,sizeof pending->data.notes);
    memcpy(a->patterns.expression[s],pending->data.expression,sizeof pending->data.expression);
    a->core.synths[s].length=pending->data.length;a->core.synths[s].next=0;
#if BB_WITH_EVOLVE
    if(a->evolve.ready) {
        /* Rebase only the deliberately replaced instrument; other home
         * patterns, locks, settings and RNG remain intact. Do not hide an
         * existing authored-edit conflict elsewhere in the ensemble. */
        unsigned part=BB_MACHINES+s;
        memcpy(a->evolve.home.synths[s],pending->data.notes,sizeof pending->data.notes);
        memcpy(a->evolve.expected.synths[s],pending->data.notes,sizeof pending->data.notes);
        memcpy(a->evolve_expression[s],pending->data.expression,sizeof pending->data.expression);
        a->evolve.home.length[part]=a->evolve.expected.length[part]=pending->data.length;
        a->evolve.phrases[part]=a->evolve.seen[part]=0;
        if(!matching) {a->evolve.conflict=1;a->evolve.frozen=1;a->evolve_result=BB_EV_RESULT_CONFLICT;}
    }
#endif
    bb_pattern_status *status=&a->patterns.status[s];
    status->applied=pending->ticket;status->queued=0;status->result=BB_PATTERN_APPLIED;status->launches++;
    status->applied_frame=a->core.frame;
    pending->ready=0;
}
void bb_audio_pattern_command(bb_audio *a,const bb_command *command) {
    uint32_t ticket=(uint32_t)command->value|((uint32_t)command->voice<<16)|((uint32_t)command->note.note<<24);
    if(command->target>=BB_SYNTHS || command->step>=BB_PATTERN_SLOTS || !ticket)return;
    bb_pattern_slot *payload=&a->patterns.inbox[command->step];
    bb_pattern_status *status=&a->patterns.status[command->target];
    if(payload->state!=BB_PATTERN_SLOT_QUEUED || payload->ticket!=ticket || payload->synth!=command->target) {
        /* A stale/raw token must not free somebody else's live reservation. */
        status->seen=ticket;status->result=BB_PATTERN_REJECTED;return;
    }
    if(!phrase_valid(payload->phrase) || !data_valid(&payload->data)) {
        payload->state=BB_PATTERN_SLOT_FREE;status->seen=ticket;status->result=BB_PATTERN_REJECTED;return;
    }
    bb_pattern_pending *pending=&a->patterns.pending[command->target];
    pending->data=payload->data;pending->ticket=ticket;pending->phrase=payload->phrase;pending->ready=1;
    payload->state=BB_PATTERN_SLOT_FREE;
    status->seen=ticket;status->queued=ticket;status->result=BB_PATTERN_QUEUED;
#if BB_WITH_PARTS
    bb_audio_trio_cancel(a);
    /* Last accepted launch wins across pattern-only and whole-band modes.
     * Rejecting a bad payload above never cancels an earlier valid launch. */
    #if BB_WITH_PREPARED_BANK
    if(pb_part_pending(&a->parts)) {
        pb_cancel_pending(&a->parts);a->parts_result=BB_PART_CANCELLED;
    }
    #else
    if(a->parts.ready && a->parts.queued!=BB_PART_NONE) {
        a->parts.queued=BB_PART_NONE;a->parts_result=BB_PART_CANCELLED;
    }
    #endif
#endif
    if(!a->core.running)apply_pattern(a,command->target);
}
void bb_audio_pattern_tick(bb_audio *a) {
    unsigned position=a->patterns.position;
    bb_audio_drum_pattern_tick(a,position);
    for(unsigned s=0;s<BB_SYNTHS;s++)if(a->patterns.pending[s].ready) {
        unsigned phrase=a->patterns.pending[s].phrase;
        int boundary=phrase==48?(position==0 || position==48 || position==96 || position==144):!(position&(phrase-1u));
        if(boundary)apply_pattern(a,s);
    }
    a->patterns.position=(uint8_t)(position+1u==192u?0u:position+1u);
}

static int drum_data_valid(const bb_drum_pattern_data *data) {
    const uint16_t voices=(1u<<BB_DRUM_TRACKS)-1u;
    if(!data || !data->length || data->length>BB_STEPS || data->family>1 || data->apply_modes>1 || (data->family && data->apply_modes) ||
       /* Only tom/conga, rim/claves and clap/maracas have alternate banks. */
       (data->apply_modes && (data->mode_value & ~UINT16_C(0x007c))) ||
       (data->family==1 && (data->mode_mask || data->mode_value)) ||
       (data->mode_mask&~voices) || (data->mode_value&~data->mode_mask))return 0;
    for(unsigned i=0;i<BB_STEPS;i++)
        if((data->steps[i].hits&~voices) ||
           (data->steps[i].accents&~data->steps[i].hits))return 0;
    return 1;
}
int bb_audio_drum_pattern_submit(bb_audio *a,unsigned machine,const bb_drum_pattern_data *data,
                                 unsigned phrase,uint32_t *out) {
    if(!a || !a->patterns.ready || machine>=BB_MACHINES || !phrase_valid(phrase) ||
       !outside(a,data,sizeof *data) || (out && !outside(a,out,sizeof *out)) ||
       (!a->commands.enter!=!a->commands.leave) || !drum_data_valid(data))return 0;
    if(out && !((uintptr_t)out+sizeof *out<=(uintptr_t)data ||
                (uintptr_t)data+sizeof *data<=(uintptr_t)out))return 0;
    uintptr_t token=enter(a);unsigned slot;
    if(data->family!=a->drum_patterns.family[machine]){leave(a,token);return 0;}
    uint32_t epoch=a->drum_patterns.family_epoch[machine];
    for(slot=0;slot<BB_PATTERN_SLOTS;slot++)
        if(a->drum_patterns.inbox[slot].state==BB_PATTERN_SLOT_FREE)break;
    if(slot==BB_PATTERN_SLOTS){leave(a,token);return 0;}
    bb_drum_pattern_slot *payload=&a->drum_patterns.inbox[slot];
    uint32_t ticket=++a->patterns.next_ticket;
    if(!ticket)ticket=++a->patterns.next_ticket;
    payload->ticket=ticket;payload->state=BB_PATTERN_SLOT_WRITING;leave(a,token);
    payload->data=*data;payload->family_epoch=epoch;payload->machine=(uint8_t)machine;payload->phrase=(uint8_t)phrase;
    token=enter(a);payload->state=BB_PATTERN_SLOT_QUEUED;leave(a,token);
    bb_command command={0};command.type=BB_CMD_DRUM_PATTERN;command.target=(uint8_t)machine;
    command.step=(uint8_t)slot;command.value=(uint16_t)ticket;
    command.voice=(uint8_t)(ticket>>16);command.note.note=(uint8_t)(ticket>>24);
    if(!bb_command_push(&a->commands,&command)) {
        token=enter(a);payload->state=BB_PATTERN_SLOT_FREE;leave(a,token);return 0;
    }
    if(out)*out=ticket;
    return 1;
}
int bb_audio_drum_pattern_status(bb_audio *a,unsigned machine,bb_drum_pattern_status *out) {
    if(!a || !a->patterns.ready || machine>=BB_MACHINES || !outside(a,out,sizeof *out) ||
       (!a->commands.enter!=!a->commands.leave))return 0;
    uintptr_t token=enter(a);*out=a->drum_patterns.status[machine];leave(a,token);return 1;
}
void bb_audio_drum_pattern_cancel(bb_audio *a,int clear_labels) {
    for(unsigned m=0;m<BB_MACHINES;m++) {
        bb_pattern_status *status=&a->drum_patterns.status[m].state;
        if(a->drum_patterns.pending[m].ready)status->result=BB_PATTERN_CANCELLED;
        a->drum_patterns.pending[m].ready=0;status->queued=0;
        if(clear_labels){status->applied=0;status->applied_frame=0;}
    }
}
static void apply_drum_pattern(bb_audio *a,unsigned machine) {
    bb_drum_pattern_pending *pending=&a->drum_patterns.pending[machine];
    bb_drum_pattern_status *report=&a->drum_patterns.status[machine];
    bb_machine *drum=&a->core.drums[machine];
    if(pending->data.family!=a->drum_patterns.family[machine] ||
       pending->family_epoch!=a->drum_patterns.family_epoch[machine]) {
        report->state.queued=0;report->state.result=BB_PATTERN_REJECTED;
        report->error=BB_DRUM_PATTERN_ERROR_FAMILY;pending->ready=0;return;
    }
    /* Pattern-only requests still require exact modes. Explicit combined
     * requests compare against the owner snapshot taken in FIFO order before
     * committing modes with notes. A later conflicting edit wins. */
#if BB_SAMPLE_HOST_INTEGRATION
    bb_sample_audio_binding *sample=bb_audio_sample_layer(a,machine);
    if(sample
#if defined(BB_SAMPLE_HYBRID_TRIAL)
       &&bb_audio_sample_layer_id(a,machine)!=BB_SAMPLE_808_ID
#endif
       ){
        unsigned needed=0,supported=0;
        for(unsigned i=0;i<BB_STEPS;i++)needed|=pending->data.steps[i].hits;
        for(unsigned i=0;i<BB_DRUM_TRACKS;i++)if(sample->lane_notes[i]!=255)supported|=1u<<i;
        /* Re-evaluate on launch: selected kit may differ since submission.
         * 808 alternate-bank mode semantics are not sampled-kit semantics. */
        if((needed&~supported)||pending->data.apply_modes||pending->data.mode_mask||pending->data.mode_value){
            report->state.queued=0;report->state.result=BB_PATTERN_REJECTED;
            report->error=BB_DRUM_PATTERN_ERROR_SAMPLE;pending->ready=0;return;
        }
    }
#endif
    for(unsigned lane=0;lane<BB_DRUM_TRACKS;lane++)if(pending->data.mode_mask&(1u<<lane)) {
        if(drum->sound.mode[lane]!=(((pending->data.apply_modes ?
            pending->expected_modes : pending->data.mode_value)>>lane)&1u)) {
            report->state.queued=0;report->state.result=BB_PATTERN_REJECTED;
            report->error=BB_DRUM_PATTERN_ERROR_MODE;pending->ready=0;return;
        }
    }
#if BB_WITH_EVOLVE
    int matching=!a->evolve.ready || bb_audio_evolve_matches(a);
    if(a->evolve_home_pending) {
        a->evolve_home_pending=0;a->evolve_result=BB_EV_RESULT_CANCELLED;
    }
#endif
    if(pending->data.apply_modes) {
        unsigned changed=0;
        for(unsigned lane=0;lane<BB_DRUM_TRACKS;lane++)
            if(pending->data.mode_mask&(1u<<lane)) {
                changed|=drum->sound.mode[lane]!=((pending->data.mode_value>>lane)&1u);
                drum->sound.mode[lane]=(pending->data.mode_value>>lane)&1u;
            }
        if(changed)drum->sound_revision++;
    }
    memcpy(drum->steps,pending->data.steps,sizeof pending->data.steps);
#if BB_WITH_RICH_DRUM_RENDER
    bb_audio_phrase_legacy(a,machine);
#endif
    drum->length=pending->data.length;drum->next=0;
#if BB_WITH_EVOLVE
    if(a->evolve.ready) {
        memcpy(a->evolve.home.drums[machine],pending->data.steps,sizeof pending->data.steps);
        memcpy(a->evolve.expected.drums[machine],pending->data.steps,sizeof pending->data.steps);
        a->evolve.home.length[machine]=a->evolve.expected.length[machine]=pending->data.length;
        a->evolve.phrases[machine]=a->evolve.seen[machine]=0;
        if(!matching){a->evolve.conflict=1;a->evolve.frozen=1;a->evolve_result=BB_EV_RESULT_CONFLICT;}
    }
#endif
    report->state.applied=pending->ticket;report->state.queued=0;
    report->state.result=BB_PATTERN_APPLIED;report->state.launches++;
    report->state.applied_frame=a->core.frame;report->error=BB_DRUM_PATTERN_ERROR_NONE;
    pending->ready=0;
}
void bb_audio_drum_pattern_command(bb_audio *a,const bb_command *command) {
    uint32_t ticket=(uint32_t)command->value|((uint32_t)command->voice<<16)|((uint32_t)command->note.note<<24);
    if(command->target>=BB_MACHINES || command->step>=BB_PATTERN_SLOTS || !ticket)return;
    bb_drum_pattern_slot *payload=&a->drum_patterns.inbox[command->step];
    bb_drum_pattern_status *report=&a->drum_patterns.status[command->target];
    if(payload->state!=BB_PATTERN_SLOT_QUEUED || payload->ticket!=ticket || payload->machine!=command->target) {
        report->state.seen=ticket;report->state.result=BB_PATTERN_REJECTED;
        report->error=BB_DRUM_PATTERN_ERROR_TOKEN;return;
    }
    if(payload->data.family!=a->drum_patterns.family[command->target] ||
       payload->family_epoch!=a->drum_patterns.family_epoch[command->target]) {
        payload->state=BB_PATTERN_SLOT_FREE;report->state.seen=ticket;
        report->state.result=BB_PATTERN_REJECTED;report->error=BB_DRUM_PATTERN_ERROR_FAMILY;return;
    }
    if(!phrase_valid(payload->phrase) || !drum_data_valid(&payload->data)) {
        payload->state=BB_PATTERN_SLOT_FREE;report->state.seen=ticket;
        report->state.result=BB_PATTERN_REJECTED;report->error=BB_DRUM_PATTERN_ERROR_DATA;return;
    }
    bb_drum_pattern_pending *pending=&a->drum_patterns.pending[command->target];
#if BB_WITH_RICH_DRUM_RENDER
    bb_audio_phrase_cancel_pending(a,command->target);
#endif
    pending->expected_modes=0;
    for(unsigned lane=0;lane<BB_DRUM_TRACKS;lane++)
        pending->expected_modes|=(uint16_t)(a->core.drums[command->target].sound.mode[lane]<<lane);
    pending->data=payload->data;pending->family_epoch=payload->family_epoch;
    pending->ticket=ticket;pending->phrase=payload->phrase;pending->ready=1;
    payload->state=BB_PATTERN_SLOT_FREE;
    report->state.seen=ticket;report->state.queued=ticket;report->state.result=BB_PATTERN_QUEUED;
    report->error=BB_DRUM_PATTERN_ERROR_NONE;
#if BB_WITH_PARTS
    bb_audio_trio_cancel(a);
    #if BB_WITH_PREPARED_BANK
    if(pb_part_pending(&a->parts)) {
        pb_cancel_pending(&a->parts);a->parts_result=BB_PART_CANCELLED;
    }
    #else
    if(a->parts.ready && a->parts.queued!=BB_PART_NONE) {
        a->parts.queued=BB_PART_NONE;a->parts_result=BB_PART_CANCELLED;
    }
    #endif
#endif
    if(!a->core.running)apply_drum_pattern(a,command->target);
}
void bb_audio_drum_pattern_tick(bb_audio *a,unsigned position) {
    for(unsigned m=0;m<BB_MACHINES;m++)if(a->drum_patterns.pending[m].ready) {
        unsigned phrase=a->drum_patterns.pending[m].phrase;
        int boundary=phrase==48?(position==0 || position==48 || position==96 || position==144):!(position&(phrase-1u));
        if(boundary)apply_drum_pattern(a,m);
    }
}
#endif
