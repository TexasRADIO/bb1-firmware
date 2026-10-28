/* SPDX-License-Identifier: GPL-3.0-only */
#include "bb_audio.h"
#if BB_WITH_RICH_DRUM_RENDER
#include "bb_phrase_strike.h"
#include "bb_voice_fallback.h"
#include <string.h>
extern void bb_proposal_group_span(drum808_t *,gg_state *,float *,float *,float *,int);
extern int bb_audio_phrase_buses(bb_audio *,unsigned,gg_state *,float *,float *,float *,unsigned);
#ifndef BB_RICH_TRACE
#define BB_RICH_TRACE 0
#endif
#if BB_RICH_TRACE
extern void bb_rich_trace(const bb_audio *,unsigned,uint64_t,uint32_t);
#endif
#if BB_RICH_TEST_ORACLE
extern int bb_rich_one_frame(const bb_audio *);
#endif
unsigned bb_audio_phrase_playhead(const bb_audio *a,unsigned m){
 if(!a||m>=BB_RICH_DRUMS||!a->rich.valid[m])return UINT32_MAX;
 unsigned length=a->rich.active[m].phrase.length;
 unsigned step=a->rich.player[m].step;
 if(!length||step>=length)return UINT32_MAX;
 /* At an exact interval boundary, the next primary step has not sounded yet.
  * Before first playback (and immediately after restart) show the first step,
  * not the last. STOP retains the last sounded cursor while tails decay. */
 if(a->rich.player[m].pending&&a->rich.player[m].sounded)
  return step?step-1:length-1;
 return step;
}
int bb_audio_phrase_preflight_playable(const bb_phrase_index *p,uint32_t playable){
 if(!bb_phrase_index_valid(p,playable))return 0;
 uint8_t repeats[BB_STEPS]={0},flams[BB_STEPS]={0};
 for(unsigned step=1;step<=p->phrase.length;step++){
  unsigned r=0,f=0;
  for(unsigned at=p->start[step-1];at<p->start[step];at++){
   unsigned mods=p->phrase.events[p->ordinal[at]]>>13;
   f+=!!(mods&BB_FDM_FLAM);r+=2u*!!(mods&BB_FDM_RETRIGGER_RATE);
  }
  if(r+f>BB_DELAYED_CAPACITY)return 0;
  repeats[step-1]=(uint8_t)r;flams[step-1]=(uint8_t)f;
 }
 /* Absolute-frame repeats retain their original forecast after a tempo edit.
  * A30->300BPM jump can leave them pending across later steps. Across legal
  * 8..192kHz,30..300BPM,50..75%swing and2/4/6steps per quarter,22 source steps
  * conservatively cover the longest repeat horizon (including quantization);
  * three cover25ms flams. Count every cyclic overlap before publication.
  * This is stronger than a steady-tempo one-step carry bound. */
 /* The same cyclic22/3-step overlap bound, maintained as two rolling sums.
  * Repeated indices for short loops are intentional: different loop passes
  * can coexist after a tempo jump. Avoid recomputing22 entries for every step. */
 unsigned repeat_sum=0,flam_sum=0,index=0,old_repeat=0,old_flam=0;
 for(unsigned back=0;back<22;back++){
  repeat_sum+=repeats[index];if(back<3)flam_sum+=flams[index];
  if(back==21)old_repeat=index;if(back==2)old_flam=index;
  index=index?index-1:p->phrase.length-1;
 }
 for(unsigned step=0;step<p->phrase.length;step++){
  if(repeat_sum+flam_sum>BB_DELAYED_CAPACITY)return 0;
  if(step+1==p->phrase.length)break;
  repeat_sum=repeat_sum-repeats[old_repeat]+repeats[step+1];
  flam_sum=flam_sum-flams[old_flam]+flams[step+1];
  if(++old_repeat==p->phrase.length)old_repeat=0;
  if(++old_flam==p->phrase.length)old_flam=0;
 }
 return 1;
}
int bb_audio_phrase_preflight(const bb_phrase_index *p){
 return bb_audio_phrase_preflight_playable(p,UINT32_MAX);
}
void bb_audio_phrase_restart(bb_audio *a,unsigned m){
 uint32_t generation=a->rich.player[m].generation;
 if(generation==UINT32_MAX){a->rich.render_errors++;a->rich.valid[m]=0;return;}
 memset(&a->rich.player[m],0,sizeof a->rich.player[m]);
 a->rich.player[m].generation=generation+1;a->rich.player[m].pending=1;
 a->rich.player[m].swing=a->core.swing;
 if(bb_delayed_begin(&a->rich.player[m].queue,generation+1,a->core.frame)!=BB_DELAYED_OK){
  a->rich.render_errors++;a->rich.valid[m]=0;
 }
}
void bb_audio_phrase_transport(bb_audio *a,unsigned start){
 if(!start&&a->aux)bb_aux_voices_stop(a->aux);
 for(unsigned m=0;m<BB_RICH_DRUMS;m++)if(a->rich.valid[m]){
  if(start)bb_audio_phrase_restart(a,m);else bb_delayed_stop(&a->rich.player[m].queue);
 }
}
void bb_audio_phrase_legacy(bb_audio *a,unsigned m){
 if(m>=BB_RICH_DRUMS)return;
 if(a->aux)bb_aux_voices_stop_machine(a->aux,m);
 bb_audio_phrase_cancel_pending(a,m);a->rich.valid[m]=0;
 a->rich.status[m].result=BB_RICH_CANCELLED;
 bb_delayed_stop(&a->rich.player[m].queue);
}
int bb_audio_phrase_scene_install_reset(bb_audio *a,const bb_phrase p[2],unsigned mask){
 if(!a||!p||mask>3||a->core.running||a->core.frame||!a->groups||!bb_commands_idle(&a->commands)||
    !bb_audio_groups_outside(a,p,2*sizeof *p))return 0;
 for(unsigned m=0;m<BB_RICH_DRUMS;m++)if(a->rich.valid[m]||a->rich.pending[m].ready||a->rich.player[m].generation)return 0;
 for(unsigned i=0;i<BB_PATTERN_SLOTS;i++)if(a->rich.inbox[i].state!=BB_PATTERN_SLOT_FREE)return 0;
 for(unsigned m=0;m<BB_MACHINES;m++)if(drum808_active(&a->kit[m]))return 0;
 for(unsigned s=0;s<BB_SYNTHS;s++)if(a->keys[s].count)return 0;
 for(unsigned m=0;m<2;m++){
  bb_sample_audio_binding *s=bb_audio_sample_layer(a,m);
  if(s)for(unsigned i=0;i<s->count;i++)if(s->slots[i].voice.active)return 0;
  if(bb_aux_voices_active(a->aux,m))return 0;
 }
 for(unsigned m=0;m<2;m++)if(mask&(1u<<m)){
  bb_phrase_index scratch;
  uint32_t playable=BB_RICH_808_VOICES|bb_audio_aux_capabilities(a);
#if BB_SAMPLE_HOST_INTEGRATION
  if(bb_audio_sample_layer(a,m)&&bb_audio_sample_layer_id(a,m)!=BB_SAMPLE_808_ID)
      playable=bb_audio_sample_source_voices(a,bb_audio_sample_layer_id(a,m))&BB_RICH_REPRESENTABLE_VOICES;
#endif
  if(a->drum_patterns.family[m]||
     !bb_phrase_index_prepare(&p[m],playable,&scratch)||
     !bb_audio_phrase_preflight(&scratch))return 0;
  for(unsigned i=p[m].count;i<BB_FACTORY_DRUM_EVENTS;i++)if(p[m].events[i])return 0;
 }else{
  const uint8_t *bytes=(const uint8_t *)&p[m];for(unsigned i=0;i<sizeof p[m];i++)if(bytes[i])return 0;
 }
 /* Inputs remain immutable under the actual scene hold. Every prepare above
  * passed; failure here is an owner/reset fault and must keep audio held. */
 for(unsigned m=0;m<2;m++)if(mask&(1u<<m)){
  if(!bb_phrase_index_prepare(&p[m],BB_RICH_REPRESENTABLE_VOICES,&a->rich.active[m]))return 0;
  a->rich.valid[m]=1;a->rich.status[m].result=BB_RICH_APPLIED;bb_audio_phrase_restart(a,m);
  if(!a->rich.valid[m])return 0;
 }
 return 1;
}
int bb_audio_phrase_admit(void *context,const bb_command *c){
 bb_audio *a=context;int reject=0;
 if(c->type==BB_CMD_DRUM_SOURCE_PARAMETER){
  uint32_t epoch=(uint32_t)c->note.note|((uint32_t)c->note.gate<<8)|
      ((uint32_t)c->note.accent<<16)|((uint32_t)c->note.slide<<24);
  reject=c->target>=BB_RICH_DRUMS;
  if(!reject)reject=a->drum_patterns.family[c->target]||
      a->drum_patterns.family_epoch[c->target]!=epoch;
#if BB_SAMPLE_HOST_INTEGRATION
  if(!reject&&bb_audio_sample_layer(a,c->target)&&bb_audio_sample_layer_id(a,c->target)!=BB_SAMPLE_808_ID)reject=1;
#endif
 }
 if(c->target<BB_RICH_DRUMS&&a->rich.valid[c->target]){
  /* Until rich edits/save are integrated, refuse rather than edit an inaudible
   * legacy grid or let generic evolution mutate it behind the phrase. */
  reject|=c->type==BB_CMD_DRUM_STEP||c->type==BB_CMD_DRUM_LENGTH||c->type==BB_CMD_DRUM_TOGGLE;
 }
 if(c->type==BB_CMD_EVOLVE&&(a->rich.valid[0]||a->rich.valid[1]))reject=1;
 /* Sample selection is validated by its audio-owner handler. That handler
  * preserves the current binding on rejection and publishes a result serial.
  * Dropping it here drains the FIFO without completing the picker request. */
 if(reject)a->rich.edit_refusals++;
 return !reject;
}
static int strike(bb_audio *a,unsigned m,uint64_t frame,uint32_t payload){
 if(!a->core.drums[m].enabled||a->core.drums[m].muted)return 1;
 bb_phrase_index *p=&a->rich.active[m];
 int result;
#if BB_SAMPLE_HOST_INTEGRATION
 /* Release performance policy: drum B keeps the immutable source event but
  * maps auxiliary-only SY/ZAP to native 808 voices.  This is the same
  * bb_voice_fallback call used by the worker plan and by the UI preview. */
 if(m==1 && bb_audio_sample_layer_id(a,m)==BB_SAMPLE_808_ID &&
    (((payload&31u)==BB_AUX_SY)||((payload&31u)==BB_AUX_ZAP))){
  bb_phrase_hit hit;result=bb_phrase_hit_resolve(&p->phrase,&p->controls,payload,&hit);
  if(result==1){
   unsigned voice=bb_voice_fallback(hit.voice,BB_RICH_808_NATIVE_VOICES);
   if(voice==BB_FALLBACK_NONE||!bb_drum808_source_trigger(&a->kit[m],voice,hit.velocity))return 0;
  }
 /* The shared SY/ZAP bank belongs to the phrase, not the selected kit.
  * The 808 path must route these two voices to that bank as well. Inspecting
  * the encoded voice first avoids an extra resolve on ordinary 808 strikes. */
 }else if((((payload&31u)==BB_AUX_SY)||((payload&31u)==BB_AUX_ZAP)) && bb_audio_aux_capabilities(a)){
  bb_phrase_hit hit;result=bb_phrase_hit_resolve(&p->phrase,&p->controls,payload,&hit);
  if(result==1&&!bb_audio_sample_phrase_hit_layer(a,m,hit.voice,hit.velocity))return 0;
 }else if(bb_audio_sample_layer(a,m)&&bb_audio_sample_layer_id(a,m)!=BB_SAMPLE_808_ID){
  bb_phrase_hit hit;result=bb_phrase_hit_resolve(&p->phrase,&p->controls,payload,&hit);
  if(result==1&&!bb_audio_sample_phrase_hit_layer(a,m,hit.voice,hit.velocity))return 0;
 }else
#endif
 result=bb_phrase_strike(&a->kit[m],&p->phrase,&p->controls,payload);
 if(!result)return 0;
 if(result==1){
  a->hits[m]++;if((payload>>23)&3)a->rich.player[m].delayed++;else a->rich.player[m].primary++;
#if BB_RICH_TRACE
  bb_rich_trace(a,m,frame,payload);
#else
  (void)frame;
#endif
 }
 return 1;
}
int bb_audio_phrase_render(bb_audio *a,unsigned m,gg_state *groups,
 float *dry,float *rev,float *dly,unsigned frames,uint64_t first){
 if(m>=BB_RICH_DRUMS||!a->rich.valid[m]||UINT64_MAX-first<frames)return 0;
 if(a->drum_patterns.family[m]){
  a->rich.render_errors++;return 0;
 }
 if(!a->core.running)return bb_audio_phrase_buses(a,m,groups,dry,rev,dly,frames);
 bb_phrase_index *p=&a->rich.active[m];bb_delayed *q=&a->rich.player[m].queue;
 unsigned offset=0;
 while(offset<frames){
  uint64_t frame=first+offset;bb_delayed_event event;int status;
  /* Already pending equal-frame strikes precede the next primary step. A
   * transport/pattern change has already canceled obsolete generations. */
  while((status=bb_delayed_peek(q,&event))==BB_DELAYED_OK&&event.frame==frame){
   if(bb_delayed_pop(q,frame,&event)!=BB_DELAYED_OK||!strike(a,m,frame,event.payload))goto fail;
  }
  if(status!=BB_DELAYED_OK&&status!=BB_DELAYED_WAIT)goto fail;
  if(status==BB_DELAYED_OK&&event.frame<frame)goto fail;
  unsigned lattice=p->phrase.lattice;
  uint32_t increment=(uint32_t)a->core.bpm*(lattice==BB_FDL_EIGHTHS?200u:lattice==BB_FDL_TRIPLETS?600u:400u);
  if(a->rich.player[m].pending){
   if(!a->rich.player[m].odd)a->rich.player[m].swing=a->core.swing;
   uint32_t denom=bb_swing_denominator(a->core.sample_rate,a->rich.player[m].swing,a->rich.player[m].odd);
   uint32_t duration=(denom-a->rich.player[m].phase+increment-1)/increment;
   unsigned step=a->rich.player[m].step;
   for(unsigned at=p->start[step];at<p->start[step+1];at++){
    unsigned ordinal=p->ordinal[at];uint32_t payload=p->phrase.events[ordinal]|(ordinal<<16);
    if(!strike(a,m,frame,payload))goto fail;
   }
   if(bb_phrase_index_delays(p,step+1,frame,duration,a->core.sample_rate,q)!=BB_DELAYED_OK)goto fail;
   a->rich.player[m].sounded=1;
   a->rich.player[m].pending=0;
   /* Only a primary step can add delayed strikes after the draining peek.
    * Refresh then; otherwise its status/event still describe the queue head.
    * The audio owner is exclusive throughout this span. */
   status=bb_delayed_peek(q,&event);
  }
  uint32_t denom=bb_swing_denominator(a->core.sample_rate,a->rich.player[m].swing,a->rich.player[m].odd);
  unsigned n=(denom-a->rich.player[m].phase+increment-1)/increment;
  if(n>frames-offset)n=frames-offset;
  if(status==BB_DELAYED_OK&&event.frame<frame+n)n=(unsigned)(event.frame-frame);
#if BB_RICH_TEST_ORACLE
  if(n>1&&bb_rich_one_frame(a))n=1;
#endif
  if(!n)goto fail;
  if(!bb_audio_phrase_buses(a,m,groups,dry+offset,rev+offset,dly+offset,n))goto fail;
  a->rich.player[m].phase+=n*increment;
  if(a->rich.player[m].phase>=denom){
   a->rich.player[m].phase-=denom;a->rich.player[m].pending=1;a->rich.player[m].odd^=1;
   a->rich.player[m].step=(uint8_t)((a->rich.player[m].step+1u)%p->phrase.length);
  }
  offset+=n;q->floor=first+offset;
 }
 return 1;
fail:
 a->rich.render_errors++;return 0;
}
#endif
