/* SPDX-License-Identifier: GPL-3.0-only */
#include "bb_audio.h"
#if BB_WITH_PATTERN && BB_WITH_RICH_DRUM_HANDOFF
#include <string.h>
static int phrase_valid(unsigned n){return n==16||n==32||n==48||n==64;}
static uint32_t capabilities(void){return BB_RICH_REPRESENTABLE_VOICES;}
static int data_valid(const bb_phrase_index *p){
#if BB_WITH_RICH_DRUM_RENDER
 return bb_audio_phrase_preflight_playable(p,capabilities());
#else
 return bb_phrase_index_valid(p,capabilities());
#endif
}
static uintptr_t enter(bb_audio *a){return a->commands.enter?a->commands.enter(a->commands.lock_context):0;}
static void leave(bb_audio *a,uintptr_t token){if(a->commands.leave)a->commands.leave(a->commands.lock_context,token);}
/* D7 (reliability r1): ticket of the one reservation the main-loop producer
 * holds between its slot write and its FIFO push. Main writes (single word),
 * the audio owner reads; 0 = none. */
static volatile uint32_t submit_inflight;
uint32_t bb_audio_phrase_source_voices_owned(const bb_audio *a,unsigned m){
 if(!a||m>=BB_RICH_DRUMS||a->drum_patterns.family[m])return 0;
#if BB_SAMPLE_HOST_INTEGRATION
 if(bb_audio_sample_layer(a,m)
#if defined(BB_SAMPLE_HYBRID_TRIAL)
    &&bb_audio_sample_layer_id(a,m)!=BB_SAMPLE_808_ID
#endif
 ){
#if BB_WITH_RICH_DRUM_RENDER
  return bb_audio_sample_source_voices(a,bb_audio_sample_layer_id(a,m))&BB_RICH_REPRESENTABLE_VOICES;
#else
  return 0;
#endif
 }
#endif
 return BB_RICH_808_VOICES
#if BB_SAMPLE_HOST_INTEGRATION
     |bb_audio_aux_capabilities(a)
#endif
     ;
}
uint32_t bb_audio_phrase_source_voices(bb_audio *a,unsigned m){
 if(!a||m>=BB_RICH_DRUMS||(!a->commands.enter!=!a->commands.leave))return 0;
 uintptr_t token=enter(a);uint32_t voices=bb_audio_phrase_source_voices_owned(a,m);
 leave(a,token);return voices;
}
static int engine_ok(const bb_audio *a,unsigned m,uint32_t epoch,const bb_phrase_index *p){
 if(a->drum_patterns.family[m]||a->drum_patterns.family_epoch[m]!=epoch)return 0;
 return !(p->voices&~bb_audio_phrase_source_voices_owned(a,m));
}
static void cancel_one(bb_audio *a,unsigned m){
 if(a->rich.pending[m].ready){
  unsigned s=a->rich.pending[m].slot;
  if(s<BB_PATTERN_SLOTS&&a->rich.inbox[s].state==BB_RICH_PINNED&&
     a->rich.inbox[s].ticket==a->rich.pending[m].ticket)a->rich.inbox[s].state=BB_PATTERN_SLOT_FREE;
  a->rich.status[m].result=BB_RICH_CANCELLED;
 }
 a->rich.pending[m].ready=0;a->rich.status[m].queued=0;
}
void bb_audio_phrase_cancel_pending(bb_audio *a,unsigned m){if(m<BB_RICH_DRUMS)cancel_one(a,m);}
int bb_audio_phrase_status_read(bb_audio *a,unsigned m,bb_audio_phrase_status *out){
 if(!a||!a->patterns.ready||m>=BB_RICH_DRUMS||
    (!a->commands.enter!=!a->commands.leave)||!bb_audio_groups_outside(a,out,sizeof *out))return 0;
 uintptr_t token=enter(a);out->state=a->rich.status[m];out->valid=a->rich.valid[m];out->error=a->rich.error[m];
 leave(a,token);return 1;
}
#if BB_WITH_RICH_DRUM_RENDER
int bb_audio_source_tone_view_read(bb_audio *a,unsigned m,bb_source_tone_view *out){
 if(!a||!a->patterns.ready||m>=BB_RICH_DRUMS||
    (!a->commands.enter!=!a->commands.leave)||!bb_audio_groups_outside(a,out,sizeof *out))return 0;
 uintptr_t token=enter(a);
 if(!bb_commands_idle(&a->commands)){out->pending=1;leave(a,token);return 1;}
 unsigned supported=!a->drum_patterns.family[m];
#if BB_SAMPLE_HOST_INTEGRATION
 if(bb_audio_sample_layer(a,m)&&bb_audio_sample_layer_id(a,m)!=BB_SAMPLE_808_ID)supported=0;
#endif
 const bb_machine *machine=&a->core.drums[m];
 int changed=!out->valid||out->pending||out->revision!=machine->sound_revision||
     out->epoch!=a->drum_patterns.family_epoch[m]||out->supported!=supported||
     memcmp(&out->sound,&machine->sound,sizeof out->sound);
 if(changed){
  out->sound=machine->sound;out->revision=machine->sound_revision;
  out->epoch=a->drum_patterns.family_epoch[m];out->supported=(uint8_t)supported;
  out->valid=1;out->pending=0;
 }
 leave(a,token);return changed?2:3;
}
int bb_audio_phrase_view_read(bb_audio *a,unsigned m,bb_phrase_view *out){
 if(!a||!a->patterns.ready||m>=BB_RICH_DRUMS||
    (!a->commands.enter!=!a->commands.leave)||!bb_audio_groups_outside(a,out,sizeof *out))return 0;
 uintptr_t token=enter(a);int result=1;
 if(!a->rich.valid[m]){
  if(out->valid){out->valid=0;out->applied=out->generation=0;result=2;}
 }else if(!out->valid||out->applied!=a->rich.status[m].applied||
           out->generation!=a->rich.player[m].generation){
  out->phrase=a->rich.active[m].phrase;out->controls=a->rich.active[m].controls;
  out->applied=a->rich.status[m].applied;out->generation=a->rich.player[m].generation;
  out->valid=1;result=2;
 }
 leave(a,token);return result;
}
int bb_phrase_view_cell(const bb_phrase_view *view,unsigned voice,unsigned step,bb_phrase_cell *out){
 if(!view||!out||view->valid!=1||voice>=BB_PHRASE_VOICES||!view->phrase.length||
    view->phrase.length>BB_STEPS||step>=view->phrase.length||
    view->phrase.count>BB_FACTORY_DRUM_EVENTS||view->phrase.lattice>BB_FDL_TRIPLETS||view->phrase.bars>4)return 0;
 unsigned per=view->phrase.lattice==BB_FDL_EIGHTHS?8:view->phrase.lattice==BB_FDL_TRIPLETS?24:16;
 if(view->phrase.bars&&view->phrase.length!=view->phrase.bars*per)return 0;
 uintptr_t x=(uintptr_t)view,y=(uintptr_t)out;
 if(x>UINTPTR_MAX-sizeof *view||y>UINTPTR_MAX-sizeof *out||
    !(x+sizeof *view<=y||y+sizeof *out<=x))return 0;
 bb_phrase_cell cell={0};
 for(unsigned i=0;i<view->phrase.count;i++){
  bb_phrase_event event;int kind=bb_phrase_resolve(&view->phrase,&view->controls,i,&event);
  if(!kind)return 0;
  if(kind!=1||event.voice!=voice||event.step!=step+1)continue;
  cell.hits++;cell.accents+=event.intensity==BB_FDI_ACCENTED;
  cell.soft+=event.intensity==BB_FDI_UNACCENTED;
  cell.flams+=!!(event.modifiers&BB_FDM_FLAM);
  cell.repeats+=!!(event.modifiers&BB_FDM_RETRIGGER_RATE);
 }
 *out=cell;return 1;
}
#endif
int bb_audio_drum_selection_status(bb_audio *a,unsigned m,unsigned rich_request,bb_drum_pattern_status *out){
 if(!a||!a->patterns.ready||m>=BB_MACHINES||rich_request>1||
    (!a->commands.enter!=!a->commands.leave)||!bb_audio_groups_outside(a,out,sizeof *out))return 0;
 if(m>=BB_RICH_DRUMS)return !rich_request&&bb_audio_drum_pattern_status(a,m,out);
 uintptr_t token=enter(a);
 *out=a->drum_patterns.status[m];
 if(rich_request){
  out->state=a->rich.status[m];
  switch(out->state.result){
   case BB_RICH_QUEUED:out->state.result=BB_PATTERN_QUEUED;break;
   case BB_RICH_STAGED:case BB_RICH_APPLIED:out->state.result=BB_PATTERN_APPLIED;break;
   case BB_RICH_CANCELLED:out->state.result=BB_PATTERN_CANCELLED;break;
   case BB_RICH_REJECTED:out->state.result=BB_PATTERN_REJECTED;break;
   default:out->state.result=BB_PATTERN_NONE;break;
  }
  out->error=a->rich.error[m]==BB_RICH_ERROR_ENGINE?BB_DRUM_PATTERN_ERROR_MODE:
             a->rich.error[m]==BB_RICH_ERROR_FAULT?BB_DRUM_PATTERN_ERROR_FAULT:
             a->rich.error[m]?BB_DRUM_PATTERN_ERROR_DATA:BB_DRUM_PATTERN_ERROR_NONE;
 }
 const bb_pattern_status *active=a->rich.valid[m]?&a->rich.status[m]:&a->drum_patterns.status[m].state;
 out->state.applied=active->applied;out->state.applied_frame=active->applied_frame;
 out->state.launches=active->launches;
 leave(a,token);return 1;
}
static int submit_phrase(bb_audio *a,unsigned m,unsigned kit,const bb_phrase_index *p,
                         unsigned phrase,uint32_t *out){
 if(!a||!a->patterns.ready||m>=BB_RICH_DRUMS||!phrase_valid(phrase)||
    (!a->commands.enter!=!a->commands.leave)||
    !bb_audio_groups_outside(a,p,sizeof *p)||(out&&!bb_audio_groups_outside(a,out,sizeof *out))||
    (kit==255u&&!data_valid(p)))return 0;
 if(out){uintptr_t x=(uintptr_t)p,y=(uintptr_t)out;
  if(!(x+sizeof *p<=y||y+sizeof *out<=x))return 0;}
#if BB_SAMPLE_HOST_INTEGRATION
 /* Catalogue descriptors are immutable after cold registration. Perform the
  * complete rich preflight outside the short command lock. */
 if(kit!=255u){
#if !defined(BB_SAMPLE_HYBRID_TRIAL)
  if(kit==BB_SAMPLE_808_ID)return 0;
#endif
  uint32_t supported=kit==BB_SAMPLE_808_ID?(BB_RICH_808_VOICES|bb_audio_aux_capabilities(a)):
      bb_audio_sample_source_voices(a,kit);
  if((kit!=BB_SAMPLE_808_ID&&kit>=a->sample_catalogue_count)||
     !bb_audio_phrase_preflight_playable(p,supported))return 0;
 }
#endif
 uintptr_t token=enter(a);unsigned slot;
 uint32_t epoch=a->drum_patterns.family_epoch[m];
 if(kit==255u){
  if(!engine_ok(a,m,epoch,p)){leave(a,token);return 0;}
 }else{
#if BB_SAMPLE_HOST_INTEGRATION
  #if !defined(BB_SAMPLE_HYBRID_TRIAL)
  if(kit==BB_SAMPLE_808_ID){leave(a,token);return 0;}
  #endif
  if(a->drum_patterns.family[m]||!bb_audio_sample_layer(a,m)||
     (kit!=BB_SAMPLE_808_ID&&kit>=a->sample_catalogue_count)){
   leave(a,token);return 0;
  }
#else
  leave(a,token);return 0;
#endif
 }
 for(slot=0;slot<BB_PATTERN_SLOTS;slot++)if(a->rich.inbox[slot].state==BB_PATTERN_SLOT_FREE)break;
 if(slot==BB_PATTERN_SLOTS){leave(a,token);return 0;}
 bb_rich_slot *payload=&a->rich.inbox[slot];uint32_t ticket=++a->patterns.next_ticket;
 if(!ticket)ticket=++a->patterns.next_ticket;
 payload->state=BB_PATTERN_SLOT_WRITING;payload->ticket=ticket;submit_inflight=ticket;leave(a,token);
 payload->data=*p;payload->epoch=epoch;payload->machine=(uint8_t)m;
 payload->phrase=(uint8_t)phrase;payload->kit=(uint8_t)kit;
 token=enter(a);payload->state=BB_PATTERN_SLOT_QUEUED;leave(a,token);
 bb_command c={0};c.type=kit==255u?BB_CMD_DRUM_PHRASE:BB_CMD_KIT_PHRASE;
 c.target=(uint8_t)m;c.step=(uint8_t)slot;
 c.value=(uint16_t)ticket;c.voice=(uint8_t)(ticket>>16);c.note.note=(uint8_t)(ticket>>24);
 if(kit!=255u)c.note.gate=(uint8_t)kit;
 if(!bb_command_push(&a->commands,&c)){
  token=enter(a);payload->state=BB_PATTERN_SLOT_FREE;submit_inflight=0;leave(a,token);return 0;}
 submit_inflight=0;
 if(out)*out=ticket;return 1;
}
int bb_audio_phrase_submit(bb_audio *a,unsigned m,const bb_phrase_index *p,
                           unsigned phrase,uint32_t *out){
 return submit_phrase(a,m,255u,p,phrase,out);
}
#if BB_SAMPLE_HOST_INTEGRATION
int bb_audio_kit_phrase_submit(bb_audio *a,unsigned m,unsigned kit,const bb_phrase_index *p,
                               unsigned phrase,uint32_t *out){
 if(kit==255u)return 0;
 return submit_phrase(a,m,kit,p,phrase,out);
}
#endif
static int pending_engine_ok(const bb_audio *a,unsigned m,const bb_rich_slot *p,unsigned kit){
 if(kit==255u)return engine_ok(a,m,p->epoch,&p->data);
#if BB_SAMPLE_HOST_INTEGRATION
#if !defined(BB_SAMPLE_HYBRID_TRIAL)
 if(kit==BB_SAMPLE_808_ID)return 0;
#endif
 if(a->drum_patterns.family[m]||a->drum_patterns.family_epoch[m]!=p->epoch||
    !bb_audio_sample_layer(a,m)||
    (kit!=BB_SAMPLE_808_ID&&kit>=a->sample_catalogue_count))return 0;
 uint32_t available=kit==BB_SAMPLE_808_ID?(BB_RICH_808_VOICES|bb_audio_aux_capabilities(a)):
     bb_audio_sample_source_voices(a,kit);
 return !(p->data.voices&~available);
#else
 return 0;
#endif
}
static void stage(bb_audio *a,unsigned m){
 unsigned s=a->rich.pending[m].slot;bb_rich_slot *p=&a->rich.inbox[s];
 bb_pattern_status *status=&a->rich.status[m];
 unsigned kit=a->rich.pending[m].kit;
 if(!pending_engine_ok(a,m,p,kit)){
  cancel_one(a,m);status->result=BB_RICH_REJECTED;a->rich.error[m]=BB_RICH_ERROR_ENGINE;return;}
#if BB_WITH_RICH_DRUM_RENDER
 if(a->rich.player[m].generation==UINT32_MAX){
  cancel_one(a,m);status->result=BB_RICH_REJECTED;a->rich.error[m]=BB_RICH_ERROR_DATA;return;}
 if(kit!=255u){
  const bb_delayed *q=&a->rich.player[m].queue;
  if(q->count>BB_DELAYED_CAPACITY||q->active>1||
     a->rich.player[m].generation+1<=q->generation){
   cancel_one(a,m);status->result=BB_RICH_REJECTED;a->rich.error[m]=BB_RICH_ERROR_DATA;return;}
 }
#endif
#if BB_SAMPLE_HOST_INTEGRATION
 if(kit!=255u&&!bb_audio_sample_pair_commit_owned(a,m,kit,p->data.voices)){
  cancel_one(a,m);status->result=BB_RICH_REJECTED;a->rich.error[m]=BB_RICH_ERROR_ENGINE;return;}
#endif
 a->rich.active[m]=p->data;a->rich.valid[m]=1;
 status->applied=p->ticket;status->applied_frame=a->core.frame;status->launches++;
 status->queued=0;status->result=BB_RICH_STAGED;a->rich.error[m]=BB_RICH_ERROR_NONE;
#if BB_WITH_RICH_DRUM_RENDER
 bb_audio_phrase_restart(a,m);status->result=BB_RICH_APPLIED;
#endif
 a->rich.pending[m].ready=0;p->state=BB_PATTERN_SLOT_FREE;
}
/* D7 (reliability r1): a command whose token does not match consumed no
 * slot, so the reservation it was sent for can stay QUEUED with no command
 * left to claim it; every such leak removes one of BB_PATTERN_SLOTS for good
 * and blocks session save. Audio owner only. A QUEUED slot is legitimately
 * unclaimed only while the single main-loop producer is between reservation
 * and push (submit_inflight below). Any other QUEUED slot that no FIFO
 * entry still addresses is released. */
static int slot_referenced(const bb_audio *a,unsigned s){
 const bb_commands *q=&a->commands;unsigned r=q->read;
 for(unsigned i=0;i<q->count;i++,r=(r+1u)%BB_COMMAND_CAPACITY){
  const bb_command *c=&q->items[r];
  if((c->type==BB_CMD_DRUM_PHRASE
#if BB_SAMPLE_HOST_INTEGRATION
      ||c->type==BB_CMD_KIT_PHRASE
#endif
     )&&c->step==s)return 1;
 }
 return 0;
}
static void release_unclaimed(bb_audio *a){
 for(unsigned s=0;s<BB_PATTERN_SLOTS;s++){
  bb_rich_slot *p=&a->rich.inbox[s];
  if(p->state==BB_PATTERN_SLOT_QUEUED&&p->ticket!=submit_inflight&&!slot_referenced(a,s))
   p->state=BB_PATTERN_SLOT_FREE;
 }
}
void bb_audio_phrase_command(bb_audio *a,const bb_command *c){
 uint32_t ticket=(uint32_t)c->value|((uint32_t)c->voice<<16)|((uint32_t)c->note.note<<24);
 if(c->target>=BB_RICH_DRUMS||c->step>=BB_PATTERN_SLOTS||!ticket){release_unclaimed(a);return;}
 unsigned m=c->target;bb_rich_slot *p=&a->rich.inbox[c->step];bb_pattern_status *status=&a->rich.status[m];
 if(p->state!=BB_PATTERN_SLOT_QUEUED||p->ticket!=ticket||p->machine!=m){
  release_unclaimed(a);
  status->seen=ticket;status->result=BB_RICH_REJECTED;a->rich.error[m]=BB_RICH_ERROR_TOKEN;return;}
 if(!phrase_valid(p->phrase)||!data_valid(&p->data)){
  p->state=BB_PATTERN_SLOT_FREE;status->seen=ticket;status->result=BB_RICH_REJECTED;
  a->rich.error[m]=BB_RICH_ERROR_DATA;return;}
 unsigned kit=c->type==BB_CMD_KIT_PHRASE?c->note.gate:255u;
 if(p->kit!=kit||!pending_engine_ok(a,m,p,kit)){
  p->state=BB_PATTERN_SLOT_FREE;status->seen=ticket;status->result=BB_RICH_REJECTED;
  a->rich.error[m]=BB_RICH_ERROR_ENGINE;return;}
 cancel_one(a,m);
#if BB_WITH_RICH_DRUM_RENDER
 if(a->drum_patterns.pending[m].ready){
  a->drum_patterns.pending[m].ready=0;a->drum_patterns.status[m].state.queued=0;
  a->drum_patterns.status[m].state.result=BB_PATTERN_CANCELLED;
 }
#endif
 a->rich.pending[m].ticket=ticket;a->rich.pending[m].slot=c->step;
 a->rich.pending[m].phrase=p->phrase;a->rich.pending[m].kit=(uint8_t)kit;
 a->rich.pending[m].ready=1;p->state=BB_RICH_PINNED;
 status->seen=ticket;status->queued=ticket;status->result=BB_RICH_QUEUED;a->rich.error[m]=BB_RICH_ERROR_NONE;
 if(!a->core.running)stage(a,m);
}
void bb_audio_phrase_cancel(bb_audio *a){for(unsigned m=0;m<BB_RICH_DRUMS;m++)cancel_one(a,m);}
void bb_audio_phrase_fault(bb_audio *a){
 for(unsigned m=0;m<BB_RICH_DRUMS;m++)if(a->rich.pending[m].ready){
  cancel_one(a,m);a->rich.status[m].result=BB_RICH_REJECTED;a->rich.error[m]=BB_RICH_ERROR_FAULT;
 }
}
void bb_audio_phrase_fault_command(bb_audio *a,const bb_command *c){
 uint32_t ticket=(uint32_t)c->value|((uint32_t)c->voice<<16)|((uint32_t)c->note.note<<24);
 if(c->target>=BB_RICH_DRUMS||c->step>=BB_PATTERN_SLOTS||!ticket){release_unclaimed(a);return;}
 unsigned m=c->target;bb_rich_slot *p=&a->rich.inbox[c->step];
 if(p->state==BB_PATTERN_SLOT_QUEUED&&p->ticket==ticket&&p->machine==m)p->state=BB_PATTERN_SLOT_FREE;
 else release_unclaimed(a);
 a->rich.status[m].seen=ticket;a->rich.status[m].result=BB_RICH_REJECTED;a->rich.error[m]=BB_RICH_ERROR_FAULT;
}
void bb_audio_phrase_tick(bb_audio *a,unsigned position){
 for(unsigned m=0;m<BB_RICH_DRUMS;m++)if(a->rich.pending[m].ready){
  unsigned n=a->rich.pending[m].phrase;
  int boundary=n==48?position%48==0:!(position&(n-1u));
  if(boundary)stage(a,m);
 }
}
#endif
