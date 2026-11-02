/* SPDX-License-Identifier: GPL-3.0-only */
#include "bb_audio.h"
#if BB_WITH_PATTERN && BB_WITH_RICH_DRUM_RENDER
#include <string.h>
#include "bb_wire.h"
static uintptr_t enter(bb_audio *a){return a->commands.enter?a->commands.enter(a->commands.lock_context):0;}
static void leave(bb_audio *a,uintptr_t t){if(a->commands.leave)a->commands.leave(a->commands.lock_context,t);}
static uint32_t token(const bb_command *c){
    return (uint32_t)c->value|((uint32_t)c->voice<<16)|((uint32_t)c->note.note<<24);
}
static void release(bb_audio *a){
    bb_section_handoff *s=&a->section;
    for(unsigned m=0;m<2;m++){
        if(s->rich_slot[m]>=BB_PATTERN_SLOTS)continue;
        bb_rich_slot *p=&a->rich.inbox[s->rich_slot[m]];
        if(p->ticket==s->ticket&&(p->state==BB_SECTION_SLOT||p->state==BB_PATTERN_SLOT_WRITING))p->state=BB_PATTERN_SLOT_FREE;
    }
    if(s->synth_slot<BB_PATTERN_SLOTS){
        bb_pattern_slot *p=&a->patterns.inbox[s->synth_slot];
        if(p->ticket==s->ticket&&(p->state==BB_SECTION_SLOT||p->state==BB_PATTERN_SLOT_WRITING))p->state=BB_PATTERN_SLOT_FREE;
    }
    s->phase=BB_SECTION_FREE;s->status.phase=BB_SECTION_FREE;s->status.state.queued=0;
}
void bb_audio_section_cancel_owned(bb_audio *a,unsigned error){
    bb_section_handoff *s=&a->section;
    if(s->restart_guard)return; /* bounded owner-only reuse of existing Start */
    if(s->cancel_serial!=UINT32_MAX)s->cancel_serial++;
    if(s->phase==BB_SECTION_FREE)return;
    s->status.error=(uint8_t)error;
    s->status.state.result=error?BB_PATTERN_REJECTED:BB_PATTERN_CANCELLED;
    /* The producer may still be copying outside the guard. Do not release
     * WRITING storage for another producer or let the owner read its data. */
    if(s->phase!=BB_SECTION_WRITING)release(a);
}
static int outside(bb_audio *a,const void *p,size_t n){
    return p&&bb_audio_groups_outside(a,p,n);
}
static int identities_ok(const bb_audio *a){
    const bb_section_handoff *s=&a->section;
    for(unsigned m=0;m<2;m++)if(a->drum_patterns.family[m]||
       a->drum_patterns.family_epoch[m]!=s->epoch[m]||
       a->rich.player[m].generation!=s->generation[m])return 0;
    return a->patterns.status[0].applied==s->synth_applied;
}
static int engines_ok(const bb_audio *a){
    const bb_section_handoff *s=&a->section;
    for(unsigned m=0;m<2;m++){
        if(s->rich_slot[m]>=BB_PATTERN_SLOTS)return 0;
        const bb_rich_slot *p=&a->rich.inbox[s->rich_slot[m]];
        const bb_delayed *q=&a->rich.player[m].queue;
        if(p->state!=BB_SECTION_SLOT||p->ticket!=s->ticket||p->machine!=m||
           p->epoch!=s->epoch[m]||p->kit!=255u||
           (p->data.voices&~bb_audio_phrase_source_voices_owned(a,m))||
           a->rich.player[m].generation==UINT32_MAX||q->count>BB_DELAYED_CAPACITY||
           q->active>1||q->generation>a->rich.player[m].generation)return 0;
    }
    if(s->synth_slot>=BB_PATTERN_SLOTS)return 0;
    const bb_pattern_slot *p=&a->patterns.inbox[s->synth_slot];
    return p->state==BB_SECTION_SLOT&&p->ticket==s->ticket&&!p->synth;
}
int bb_audio_section_submit(bb_audio *a,const bb_section_bundle *b,uint32_t *out){
    if(!a||!a->patterns.ready||(!a->commands.enter!=!a->commands.leave)||
       !outside(a,b,sizeof *b)||(out&&!outside(a,out,sizeof *out))||
       !b->section||!b->revision||!b->project_revision||b->wait_ticks>4095||b->duration_ticks>262144u||b->restart>1||b->at_tick>1||
       !bb_audio_pattern_data_valid(&b->dillo))return 0;
    if(out){uintptr_t x=(uintptr_t)out,y=(uintptr_t)b;
        if(x>UINTPTR_MAX-sizeof *out||y>UINTPTR_MAX-sizeof *b||
           (x<y+sizeof *b&&y<x+sizeof *out))return 0;
    }
    for(unsigned m=0;m<2;m++)if(!bb_audio_phrase_preflight_playable(&b->drum[m],BB_RICH_REPRESENTABLE_VOICES))return 0;
    uintptr_t guard=enter(a);bb_section_handoff *s=&a->section;
    if(s->phase!=BB_SECTION_FREE||s->cancel_serial==UINT32_MAX||
       a->patterns.next_ticket==UINT32_MAX||a->commands.stop_pending||a->commands.stop_inflight||
       (b->at_tick&&(uint32_t)(b->target_tick-a->core.ticks)>4095u)){leave(a,guard);return 0;}
    unsigned slots[2],count=0,synth=BB_PATTERN_SLOTS;
    for(unsigned i=0;i<BB_PATTERN_SLOTS;i++){
        if(a->rich.inbox[i].state==BB_PATTERN_SLOT_FREE&&count<2)slots[count++]=i;
        if(a->patterns.inbox[i].state==BB_PATTERN_SLOT_FREE&&synth==BB_PATTERN_SLOTS)synth=i;
    }
    if(count!=2||synth==BB_PATTERN_SLOTS){leave(a,guard);return 0;}
    for(unsigned m=0;m<2;m++)if(a->drum_patterns.family[m]||
       (b->drum[m].voices&~bb_audio_phrase_source_voices_owned(a,m))){leave(a,guard);return 0;}
    uint32_t ticket=++a->patterns.next_ticket,serial=s->cancel_serial;
    s->next_duration=b->duration_ticks;s->restart=b->restart;
    s->ticket=ticket;s->due_tick=b->at_tick?b->target_tick:a->core.ticks+b->wait_ticks;s->synth_applied=a->patterns.status[0].applied;
    s->status.section=b->section;s->status.revision=b->revision;s->status.project_revision=b->project_revision;
    s->phase=BB_SECTION_WRITING;s->status.phase=BB_SECTION_WRITING;s->status.error=BB_SECTION_ERROR_NONE;
    for(unsigned m=0;m<2;m++){
        s->rich_slot[m]=(uint8_t)slots[m];s->generation[m]=a->rich.player[m].generation;
        s->epoch[m]=a->drum_patterns.family_epoch[m];
        a->rich.inbox[slots[m]].ticket=ticket;a->rich.inbox[slots[m]].state=BB_PATTERN_SLOT_WRITING;
    }
    s->synth_slot=(uint8_t)synth;
    a->patterns.inbox[synth].ticket=ticket;a->patterns.inbox[synth].state=BB_PATTERN_SLOT_WRITING;
    leave(a,guard);
    /* Complete metadata and source ordinals remain immutable. No sample,
     * catalogue, DSP voice, mix or effect state is copied or recalled. */
    for(unsigned m=0;m<2;m++){
        bb_rich_slot *p=&a->rich.inbox[slots[m]];p->data=b->drum[m];p->epoch=s->epoch[m];p->enable_on_apply=0;
        p->machine=(uint8_t)m;p->phrase=16;p->kit=255;
    }
    a->patterns.inbox[synth].data=b->dillo;a->patterns.inbox[synth].synth=0;
    a->patterns.inbox[synth].phrase=16;
    guard=enter(a);
    if(s->cancel_serial!=serial){release(a);leave(a,guard);return 0;}
    for(unsigned m=0;m<2;m++)a->rich.inbox[slots[m]].state=BB_SECTION_SLOT;
    a->patterns.inbox[synth].state=BB_SECTION_SLOT;
    s->phase=BB_SECTION_QUEUED;s->status.phase=BB_SECTION_QUEUED;
    leave(a,guard);
    bb_command c={0};c.type=BB_CMD_SECTION_PATTERN;c.value=(uint16_t)ticket;
    c.voice=(uint8_t)(ticket>>16);c.note.note=(uint8_t)(ticket>>24);
    if(!bb_command_push(&a->commands,&c)){
        guard=enter(a);
        if(s->phase==BB_SECTION_QUEUED&&s->ticket==ticket){
            s->status.state.result=BB_PATTERN_REJECTED;release(a);
        }
        leave(a,guard);return 0;
    }
    if(out)*out=ticket;return 1;
}
/* No copy of a phrase or DSP state is made under the producer guard. */
int bb_audio_section_ratchet_submit(bb_audio *a,const bb_section_status *expected,
                                    const bb_phrase_view *source,unsigned m,
                                    unsigned voice,unsigned step,unsigned count,uint32_t *out){
 if(!a||!a->patterns.ready||!expected||!source||m>=2||!source->valid||
    (expected->playing_section&&!expected->playing_project_revision)||expected->playing_project_revision==UINT32_MAX||
    !outside(a,expected,sizeof *expected)||!outside(a,source,sizeof *source)||
    (out&&(!outside(a,out,sizeof *out)||
       !bb_wire_separated(out,sizeof *out,expected,sizeof *expected)||
       !bb_wire_separated(out,sizeof *out,source,sizeof *source)))||
    (!a->commands.enter!=!a->commands.leave)||voice>=BB_PHRASE_VOICES||
    step>=source->phrase.length||count<1||count>4)return 0;
 if(voice==BB_FDV_ACCENT||voice==BB_FDV_ACC||!bb_phrase_ratchets_valid(&source->phrase))return 0;
 unsigned changed=0;
 for(unsigned i=0;i<source->phrase.count;i++){
  bb_phrase_event e;if(!bb_phrase_read(&source->phrase,i,&e))return 0;
  if(e.voice==voice&&e.step==step+1u)changed|=bb_phrase_ratchet_count(&source->phrase,i)!=count;
 }
 if(!changed)return 0;
 uintptr_t g=enter(a);bb_section_handoff *s=&a->section;bb_section_ratchet *r=&s->ratchet;
 if(r->phase!=BB_SECTION_EDIT_IDLE||r->ticket==UINT32_MAX||
    s->phase==BB_SECTION_WRITING||(!expected->playing_section&&s->phase!=BB_SECTION_FREE)||a->commands.stop_pending||a->commands.stop_inflight||
    s->status.playing_section!=expected->playing_section||
    s->status.playing_revision!=expected->playing_revision||
    s->status.playing_project_revision!=expected->playing_project_revision||
    !a->rich.valid[m]||a->drum_patterns.family[m]||a->rich.edit_revision[m]==UINT32_MAX||a->rich.status[m].applied!=source->applied||
    a->rich.edit_revision[m]!=source->edits||a->rich.player[m].generation!=source->generation){leave(a,g);return 0;}
 r->ticket++;r->section=expected->playing_section;r->revision=expected->playing_revision;
 r->project_revision=expected->playing_project_revision;
 r->source_ticket=source->applied;r->source_edits=source->edits;r->source_generation=source->generation;
 r->mode=(uint8_t)(expected->playing_section?0:1);r->old_count=0;
 r->edited_section=r->edited_revision=r->result_revision=0;
 r->target=(uint8_t)m;r->voice=(uint8_t)voice;r->step=(uint8_t)step;r->count=(uint8_t)count;
 r->phase=BB_SECTION_EDIT_QUEUED;r->error=0;uint32_t ticket=r->ticket;leave(a,g);
 bb_command c={0};c.type=BB_CMD_SECTION_RATCHET;c.value=(uint16_t)ticket;
 c.voice=(uint8_t)(ticket>>16);c.note.note=(uint8_t)(ticket>>24);
 if(!bb_command_push(&a->commands,&c)){
  g=enter(a);if(r->ticket==ticket&&r->phase==BB_SECTION_EDIT_QUEUED)r->phase=BB_SECTION_EDIT_IDLE;leave(a,g);return 0;
 }
 if(out)*out=ticket;return 1;
}
int bb_audio_section_counts_restore_submit(bb_audio *a,const bb_section_status *expected,
    const bb_phrase_view *source,unsigned m,uint32_t edited_section,uint32_t edited_revision,
    uint32_t result_revision,const bb_phrase_index *after,const bb_phrase_index *before,uint32_t *out){
 if(!a||!a->patterns.ready||m>=2||!outside(a,expected,sizeof *expected)||
    !outside(a,source,sizeof *source)||!outside(a,after,sizeof *after)||!outside(a,before,sizeof *before)||
    !source->valid||!expected->playing_section||!expected->playing_project_revision||
    expected->playing_project_revision==UINT32_MAX||!edited_section||!edited_revision||!result_revision||
    (out&&(!outside(a,out,sizeof *out)||!bb_wire_separated(out,sizeof *out,expected,sizeof *expected)||
      !bb_wire_separated(out,sizeof *out,source,sizeof *source)||!bb_wire_separated(out,sizeof *out,after,sizeof *after)||
      !bb_wire_separated(out,sizeof *out,before,sizeof *before)))||(!a->commands.enter!=!a->commands.leave))return 0;
 const bb_phrase *x=&after->phrase,*y=&before->phrase;
 if(x->length!=y->length||x->count!=y->count||x->lattice!=y->lattice||x->bars!=y->bars||
    memcmp(x->events,y->events,sizeof x->events)||!memcmp(x->ratchets,y->ratchets,sizeof x->ratchets)||
    !bb_phrase_index_valid(after,UINT32_MAX)||!bb_phrase_index_valid(before,UINT32_MAX)||
    !bb_audio_phrase_preflight_playable(before,UINT32_MAX))return 0;
 uintptr_t g=enter(a);bb_section_handoff *s=&a->section;bb_section_ratchet *r=&s->ratchet;
 if(r->phase!=BB_SECTION_EDIT_IDLE||r->ticket==UINT32_MAX||s->phase==BB_SECTION_WRITING||
    a->commands.stop_pending||a->commands.stop_inflight||
    s->status.playing_section!=expected->playing_section||s->status.playing_revision!=expected->playing_revision||
    s->status.playing_project_revision!=expected->playing_project_revision||!a->rich.valid[m]||a->drum_patterns.family[m]||
    a->rich.status[m].applied!=source->applied||a->rich.edit_revision[m]!=source->edits||
    a->rich.edit_revision[m]==UINT32_MAX||a->rich.player[m].generation!=source->generation){leave(a,g);return 0;}
 r->ticket++;uint32_t ticket=r->ticket;
 r->section=expected->playing_section;r->revision=expected->playing_revision;r->project_revision=expected->playing_project_revision;
 r->source_ticket=source->applied;r->source_edits=source->edits;r->source_generation=source->generation;
 r->edited_section=edited_section;r->edited_revision=edited_revision;r->result_revision=result_revision;
 r->target=(uint8_t)m;r->mode=(uint8_t)(edited_section==expected->playing_section?3:4);
 r->voice=r->step=r->count=r->old_count=0;r->error=0;r->phase=BB_SECTION_EDIT_WRITING;leave(a,g);
 /* Owner never reads WRITING payloads. Fault cancellation may mark the
  * reservation terminal; publication rechecks that state before queueing. */
 s->ratchet_expected=*x;memcpy(s->ratchet_restore_counts,y->ratchets,sizeof s->ratchet_restore_counts);
 g=enter(a);
 if(r->ticket!=ticket||r->phase!=BB_SECTION_EDIT_WRITING){
  if(r->ticket==ticket)r->phase=BB_SECTION_EDIT_IDLE;leave(a,g);return 0;
 }
 r->phase=BB_SECTION_EDIT_QUEUED;leave(a,g);
 bb_command c={0};c.type=BB_CMD_SECTION_RATCHET;c.value=(uint16_t)ticket;c.voice=(uint8_t)(ticket>>16);c.note.note=(uint8_t)(ticket>>24);
 if(!bb_command_push(&a->commands,&c)){
  g=enter(a);if(r->ticket==ticket&&r->phase==BB_SECTION_EDIT_QUEUED)r->phase=BB_SECTION_EDIT_IDLE;leave(a,g);return 0;
 }
 if(out)*out=ticket;return 1;
}
int bb_audio_native_pattern_identity(bb_audio *a,unsigned m,uint32_t out[2]){
 if(!a||!a->patterns.ready||m>=2||!outside(a,out,2*sizeof *out)||(!a->commands.enter!=!a->commands.leave))return 0;
 uintptr_t g=enter(a);int ok=!a->rich.valid[m]&&!a->drum_patterns.family[m];
 if(ok){out[0]=a->drum_patterns.status[m].state.applied;out[1]=a->drum_patterns.family_epoch[m];}
 leave(a,g);return ok;
}
int bb_audio_native_ratchet_view(bb_audio *a,unsigned m,unsigned voice,unsigned step,bb_native_ratchet_view *out){
 if(!a||!a->patterns.ready||m>=2||voice>=BB_VOICES||step>=BB_STEPS||
    !outside(a,out,sizeof *out)||(!a->commands.enter!=!a->commands.leave))return 0;
 uintptr_t g=enter(a);int ok=!a->rich.valid[m]&&!a->drum_patterns.family[m]&&
    !a->section.status.playing_section&&a->section.phase==BB_SECTION_FREE&&step<a->core.drums[m].length&&
    (a->core.drums[m].steps[step].hits&(1u<<voice));
 if(ok){out->source_ticket=a->drum_patterns.status[m].state.applied;out->epoch=a->drum_patterns.family_epoch[m];
  out->packed=a->core.drums[m].steps[step].ratchets;out->hits=a->core.drums[m].steps[step].hits;
  out->target=(uint8_t)m;out->voice=(uint8_t)voice;out->step=(uint8_t)step;out->length=a->core.drums[m].length;}
 leave(a,g);return ok;
}
int bb_audio_native_ratchet_submit(bb_audio *a,const bb_native_ratchet_view *v,unsigned count,uint32_t *out){
 if(!a||!a->patterns.ready||!outside(a,v,sizeof *v)||v->target>=2||v->voice>=BB_VOICES||v->step>=BB_STEPS||
    count<1||count>4||bb_ratchet_bits_get(v->packed,v->voice)==count||
    (out&&(!outside(a,out,sizeof *out)||!bb_wire_separated(out,sizeof *out,v,sizeof *v)))||
    (!a->commands.enter!=!a->commands.leave))return 0;
 uintptr_t g=enter(a);bb_section_handoff *s=&a->section;bb_section_ratchet *r=&s->ratchet;
 unsigned m=v->target,step=v->step;
 if(r->phase!=BB_SECTION_EDIT_IDLE||r->ticket==UINT32_MAX||s->phase!=BB_SECTION_FREE||
    s->status.playing_section||a->commands.stop_pending||a->commands.stop_inflight||a->rich.valid[m]||
    a->drum_patterns.family[m]||a->drum_patterns.status[m].state.applied!=v->source_ticket||
    a->drum_patterns.family_epoch[m]!=v->epoch||a->core.drums[m].length!=v->length||
    step>=v->length||a->core.drums[m].steps[step].ratchets!=v->packed||
    a->core.drums[m].steps[step].hits!=v->hits||!(v->hits&(1u<<v->voice))){leave(a,g);return 0;}
 r->ticket++;r->section=r->revision=r->project_revision=0;r->mode=2;
 r->edited_section=r->edited_revision=r->result_revision=0;
 r->source_ticket=v->source_ticket;r->source_generation=v->epoch;r->source_edits=v->packed;
 r->target=v->target;r->voice=v->voice;r->step=v->step;r->count=(uint8_t)count;
 r->old_count=(uint8_t)bb_ratchet_bits_get(v->packed,v->voice);r->phase=BB_SECTION_EDIT_QUEUED;r->error=0;
 uint32_t ticket=r->ticket;leave(a,g);
 bb_command c={0};c.type=BB_CMD_SECTION_RATCHET;c.value=(uint16_t)ticket;c.voice=(uint8_t)(ticket>>16);c.note.note=(uint8_t)(ticket>>24);
 if(!bb_command_push(&a->commands,&c)){
  g=enter(a);if(r->ticket==ticket&&r->phase==BB_SECTION_EDIT_QUEUED)r->phase=BB_SECTION_EDIT_IDLE;leave(a,g);return 0;
 }
 if(out)*out=ticket;return 1;
}
int bb_audio_section_ratchet_read(bb_audio *a,bb_section_ratchet *out){
 if(!a||!outside(a,out,sizeof *out)||(!a->commands.enter!=!a->commands.leave))return 0;
 uintptr_t g=enter(a);*out=a->section.ratchet;leave(a,g);return 1;
}
int bb_audio_section_ratchet_ack(bb_audio *a,uint32_t ticket){
 if(!a||!ticket||(!a->commands.enter!=!a->commands.leave))return 0;
 uintptr_t g=enter(a);bb_section_ratchet *r=&a->section.ratchet;
 int ok=r->ticket==ticket&&(r->phase==BB_SECTION_EDIT_APPLIED||r->phase==BB_SECTION_EDIT_REJECTED);
 if(ok)r->phase=BB_SECTION_EDIT_IDLE;leave(a,g);return ok;
}
void bb_audio_section_ratchet_fault(bb_audio *a){
 bb_section_ratchet *r=&a->section.ratchet;
 if(r->phase==BB_SECTION_EDIT_QUEUED||r->phase==BB_SECTION_EDIT_WRITING){r->phase=BB_SECTION_EDIT_REJECTED;r->error=BB_SECTION_ERROR_FAULT;a->rich.edit_refusals++;}
}
void bb_audio_section_ratchet_command(bb_audio *a,const bb_command *c){
 bb_section_handoff *s=&a->section;bb_section_ratchet *r=&s->ratchet;
 if(r->phase!=BB_SECTION_EDIT_QUEUED||token(c)!=r->ticket)return;
 unsigned m=r->target;bb_phrase_index *next=NULL;unsigned error=BB_SECTION_ERROR_STALE;
 if(r->mode==2){
  if(a->commands.stop_pending||a->commands.stop_inflight||m>=2||s->status.playing_section||s->phase!=BB_SECTION_FREE||
     a->rich.valid[m]||a->drum_patterns.family[m]||r->voice>=BB_VOICES||r->step>=a->core.drums[m].length||
     a->drum_patterns.status[m].state.applied!=r->source_ticket||a->drum_patterns.family_epoch[m]!=r->source_generation||
     a->core.drums[m].steps[r->step].ratchets!=r->source_edits||
     !(a->core.drums[m].steps[r->step].hits&(1u<<r->voice)))goto rejected;
  (void)bb_ratchet_set(&a->core.drums[m].steps[r->step].ratchets,r->voice,r->count);
  bb_commands_project_edit(&a->commands);r->phase=BB_SECTION_EDIT_APPLIED;r->error=0;return;
 }

 if(a->commands.stop_pending||a->commands.stop_inflight||m>=2||
    s->phase==BB_SECTION_WRITING||s->status.playing_section!=r->section||
    s->status.playing_revision!=r->revision||s->status.playing_project_revision!=r->project_revision||
    !a->rich.valid[m]||a->drum_patterns.family[m]||a->rich.status[m].applied!=r->source_ticket||
    a->rich.edit_revision[m]!=r->source_edits||a->rich.edit_revision[m]==UINT32_MAX||
    a->rich.player[m].generation!=r->source_generation)goto rejected;
 if(r->mode==3||r->mode==4){
  bb_phrase *pending=NULL;
  if(s->phase!=BB_SECTION_FREE){
   if(s->status.project_revision!=r->project_revision)goto rejected;
   if(s->status.section==r->edited_section){
    if(s->status.revision!=r->edited_revision||s->rich_slot[m]>=BB_PATTERN_SLOTS)goto rejected;
    bb_rich_slot *slot=&a->rich.inbox[s->rich_slot[m]];
    if(slot->ticket!=s->ticket||slot->state!=BB_SECTION_SLOT||
       memcmp(&slot->data.phrase,&s->ratchet_expected,sizeof s->ratchet_expected))goto rejected;
    pending=&slot->data.phrase;
   }
  }
  if(r->mode==3&&(r->edited_section!=r->section||r->edited_revision!=r->revision||
     memcmp(&a->rich.active[m].phrase,&s->ratchet_expected,sizeof s->ratchet_expected)))goto rejected;
  /* Sidecar was preflighted against this exact immutable ordinal payload by
   * the producer. No fallible operation remains; preserve phase/end/tails. */
  if(r->mode==3){
   memcpy(a->rich.active[m].phrase.ratchets,s->ratchet_restore_counts,sizeof s->ratchet_restore_counts);
   a->rich.edit_revision[m]++;
#if BB_WITH_LIVE_CAPTURE
   if(a->capture.valid&&a->capture.armed&&a->capture.clock.target==m){a->capture.armed=0;a->capture.error=BB_CAPTURE_CHANGED;}
#endif
   s->status.playing_revision=r->result_revision;
  }
  if(pending){memcpy(pending->ratchets,s->ratchet_restore_counts,sizeof s->ratchet_restore_counts);s->status.revision=r->result_revision;}
  else if(s->phase==BB_SECTION_FREE&&r->mode==3)s->status.revision=r->result_revision;
  s->status.project_revision=r->project_revision+1u;s->status.playing_project_revision=r->project_revision+1u;
  bb_commands_project_edit(&a->commands);r->phase=BB_SECTION_EDIT_APPLIED;r->error=0;return;
 }
 if(s->phase!=BB_SECTION_FREE){
  if(s->status.project_revision!=r->project_revision)goto rejected;
  if(s->status.section==r->section){
   if(s->status.revision!=r->revision||s->rich_slot[m]>=BB_PATTERN_SLOTS)goto rejected;
   bb_rich_slot *slot=&a->rich.inbox[s->rich_slot[m]];
   if(slot->ticket!=s->ticket||slot->state!=BB_SECTION_SLOT)goto rejected;
   next=&slot->data;
  }
 }
 error=BB_SECTION_ERROR_DATA;
 if(!bb_audio_phrase_ratchet_preflight(&a->rich.active[m],r->voice,r->step,r->count)||
    (next&&!bb_audio_phrase_ratchet_preflight(next,r->voice,r->step,r->count)))goto rejected;
 /* Complete checks precede the first mutation. Counts affect only future
  * frozen-step snapshots; cursors, delayed hits, tails and end tick survive. */
 (void)bb_phrase_ratchet_set(&a->rich.active[m].phrase,r->voice,r->step,r->count);
 if(next)(void)bb_phrase_ratchet_set(&next->phrase,r->voice,r->step,r->count);
 if(r->mode==0){
 uint32_t revision=r->project_revision+1u;
 s->status.playing_revision=revision;s->status.playing_project_revision=revision;
 if(s->phase!=BB_SECTION_FREE){s->status.project_revision=revision;if(next)s->status.revision=revision;}
 else{s->status.revision=revision;s->status.project_revision=revision;}
 }
 a->rich.edit_revision[m]++;bb_commands_project_edit(&a->commands);
#if BB_WITH_LIVE_CAPTURE
 if(a->capture.valid&&a->capture.armed&&a->capture.clock.target==m){a->capture.armed=0;a->capture.error=BB_CAPTURE_CHANGED;}
#endif
 r->phase=BB_SECTION_EDIT_APPLIED;r->error=0;return;
rejected:r->phase=BB_SECTION_EDIT_REJECTED;r->error=(uint8_t)error;a->rich.edit_refusals++;
}
static void apply(bb_audio *a){
    bb_section_handoff *s=&a->section;
    /* Every fallible check precedes the first musical mutation. From here
     * the single owner performs fixed-size copies and infallible restarts. */
    if(!identities_ok(a)){bb_audio_section_cancel_owned(a,BB_SECTION_ERROR_STALE);return;}
    if(!engines_ok(a)){bb_audio_section_cancel_owned(a,BB_SECTION_ERROR_ENGINE);return;}
    bb_audio_phrase_cancel(a);bb_audio_pattern_cancel(a,0);
    for(unsigned m=0;m<2;m++){
        a->rich.active[m]=a->rich.inbox[s->rich_slot[m]].data;a->rich.valid[m]=1;
        bb_audio_phrase_restart(a,m); /* cancels obsolete future hits, retains DSP tails */
        /* N02: a section can end on an odd sixteenth, so this hand-off may land on the second step of a swing
         * pair. A sixteenth phrase continues the core's pair (its position and the swing latched at the pair's
         * start) so drums stay on Dillo's grid. On an even tick this equals the restart's own values. Eighth and
         * triplet phrases keep their own grid from the hand-off, as before. */
        if(a->rich.active[m].phrase.lattice==BB_FDL_SIXTEENTHS){
            a->rich.player[m].odd=a->core.swing_odd;a->rich.player[m].swing=a->core.active_swing;
        }
        bb_pattern_status *r=&a->rich.status[m];r->seen=r->applied=s->ticket;r->queued=0;
        r->applied_frame=a->core.frame;r->launches++;r->result=BB_RICH_APPLIED;a->rich.error[m]=0;
    }
    bb_pattern_pending *p=&a->patterns.pending[0];
    p->data=a->patterns.inbox[s->synth_slot].data;p->ticket=s->ticket;p->ready=1;
    a->patterns.status[0].seen=s->ticket;bb_audio_pattern_apply_owned(a,0);
    s->duration_ticks=s->next_duration;s->end_armed=(uint8_t)(s->duration_ticks!=0);
    s->status.applied_tick=a->core.ticks;s->status.end_tick=a->core.ticks+s->duration_ticks;
    s->status.ended=0;
    s->status.playing_section=s->status.section;s->status.playing_revision=s->status.revision;
    s->status.playing_project_revision=s->status.project_revision;
    s->status.state.applied=s->ticket;s->status.state.applied_frame=a->core.frame;
    s->status.state.launches++;s->status.state.result=BB_PATTERN_APPLIED;
    release(a);
}
void bb_audio_section_command(bb_audio *a,const bb_command *c,bb_live_command_fn live,void *context){
    bb_section_handoff *s=&a->section;uint32_t t=token(c);
    if(!t||t!=s->ticket)return; /* stale command may not cancel a different reservation */
    if(c->type==BB_CMD_SECTION_CANCEL){bb_audio_section_cancel_owned(a,0);return;}
    if(s->phase!=BB_SECTION_QUEUED)return;
    s->status.state.seen=t;
    if(a->commands.stop_pending||a->commands.stop_inflight){bb_audio_section_cancel_owned(a,0);return;}
    if(!identities_ok(a)||!engines_ok(a)){bb_audio_section_cancel_owned(a,BB_SECTION_ERROR_STALE);return;}
    for(unsigned m=0;m<2;m++)if(!bb_audio_phrase_preflight_playable(
        &a->rich.inbox[s->rich_slot[m]].data,bb_audio_phrase_source_voices_owned(a,m))){
        bb_audio_section_cancel_owned(a,BB_SECTION_ERROR_DATA);return;
    }
    if(!bb_audio_pattern_data_valid(&a->patterns.inbox[s->synth_slot].data)){
        bb_audio_section_cancel_owned(a,BB_SECTION_ERROR_DATA);return;
    }
    if(s->restart){
        if(!live){bb_audio_section_cancel_owned(a,BB_SECTION_ERROR_DATA);return;}
        for(unsigned m=0;m<2;m++)if(a->rich.player[m].generation>UINT32_MAX-2u){
            bb_audio_section_cancel_owned(a,BB_SECTION_ERROR_ENGINE);return;
        }
        /* Complete validation precedes the first transport mutation. Reuse
         * existing Start logic once, with this reservation owner-protected.
         * This is a bounded owner call, never a renderer/queue re-entry. */
        s->restart_guard=1;bb_start(&a->core);
        bb_command start={0};start.type=BB_CMD_TRANSPORT_RESTART;live(context,&start);
        s->restart_guard=0;
        for(unsigned m=0;m<2;m++)s->generation[m]=a->rich.player[m].generation;
    }
    /* Last accepted complete section supersedes older independent launches. */
    bb_audio_phrase_cancel(a);bb_audio_pattern_cancel(a,0);
    s->phase=BB_SECTION_PENDING;s->status.phase=BB_SECTION_PENDING;
    s->status.state.queued=t;s->status.state.result=BB_PATTERN_QUEUED;
    if(s->restart||!a->core.running)apply(a);
}
int bb_audio_section_tick(bb_audio *a){
    bb_section_handoff *s=&a->section;
    if(s->phase==BB_SECTION_PENDING){
        uint32_t late=a->core.ticks-s->due_tick;
        if(late<=UINT32_MAX/2u){
            if(late)bb_audio_section_cancel_owned(a,BB_SECTION_ERROR_LATE);
            else apply(a);
        }
    }
    /* A successor accepted at this exact tick has replaced the old end.
     * Otherwise stop before emitting any out-of-arrangement primary note. */
    if(s->end_armed&&(uint32_t)(a->core.ticks-s->status.end_tick)<=UINT32_MAX/2u){
        if(a->core.ticks!=s->status.end_tick)s->status.error=BB_SECTION_ERROR_LATE;
        s->status.ended=1;return 1;
    }
    return 0;
}
int bb_audio_section_status_read(bb_audio *a,bb_section_status *out){
    if(!a||!a->patterns.ready||!outside(a,out,sizeof *out)||(!a->commands.enter!=!a->commands.leave))return 0;
    uintptr_t guard=enter(a);*out=a->section.status;out->running=a->core.running;out->current_tick=a->core.ticks;leave(a,guard);return 1;
}
int bb_audio_section_cancel_submit(bb_audio *a,uint32_t ticket){
    if(!a||!ticket)return 0;bb_command c={0};c.type=BB_CMD_SECTION_CANCEL;
    c.value=(uint16_t)ticket;c.voice=(uint8_t)(ticket>>16);c.note.note=(uint8_t)(ticket>>24);
    return bb_command_push(&a->commands,&c);
}
void bb_audio_section_external_edit_owned(bb_audio *a){
    bb_audio_section_cancel_owned(a,0);
    a->section.end_armed=0;a->section.duration_ticks=0;
    a->section.status.playing_section=0;a->section.status.playing_revision=0;
    a->section.status.playing_project_revision=0;
}
void bb_audio_section_observe_command(bb_audio *a,const bb_command *c){
    switch(c->type){
    case BB_CMD_TRANSPORT:case BB_CMD_TRANSPORT_RESTART:
        bb_audio_section_cancel_owned(a,0);
        if(c->type==BB_CMD_TRANSPORT_RESTART||c->value){
            bb_section_handoff *s=&a->section;
            if(s->status.transport_epoch==UINT32_MAX){bb_audio_section_external_edit_owned(a);s->status.error=BB_SECTION_ERROR_STALE;break;}
            s->status.transport_epoch++;
            s->status.applied_tick=a->core.ticks;s->status.end_tick=a->core.ticks+s->duration_ticks;s->status.ended=0;
        }
        break;
    case BB_CMD_NOTE:case BB_CMD_NOTE_EDIT:case BB_CMD_NOTE_PITCH:
    case BB_CMD_NOTE_SHIFT:case BB_CMD_SYNTH_LENGTH:case BB_CMD_DRUM_LENGTH:
    case BB_CMD_DRUM_STEP:case BB_CMD_DRUM_TOGGLE:case BB_CMD_DRUM_RATCHET:case BB_CMD_EVOLVE:
        bb_audio_section_external_edit_owned(a);break;
    default:break; /* live mix, sound, keyboard, tempo and Continue survive */
    }
}
#endif
