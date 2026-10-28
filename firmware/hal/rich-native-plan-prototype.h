/* SPDX-License-Identifier: GPL-3.0-only */
/* Offline composition only. Staging is private and discarded on failure.
 * SY/ZAP must retain their auxiliary backend; sampled kits are not eligible.
 * Caller commits next scheduler state and DSP together only after success. */
#ifndef BB_RICH_NATIVE_PLAN_PROTOTYPE_H
#define BB_RICH_NATIVE_PLAN_PROTOTYPE_H
#include "rich-schedule-plan-prototype.h"
#include "rich-drum-job-prototype.h"
#include "bb_phrase_strike.h"
#include "bb_voice_fallback.h"
static int bb_rnp_resolve(const bb_phrase_index *p,bb_rsp_plan *plan,
                         unsigned machine,unsigned frames,unsigned audible,bb_rj_job *job){
 if(!p||!plan||!job||machine>=2||!frames||frames>128||plan->count>192)return 0;
 job->frames=frames;job->count=0;
 if(!audible)return 1;
 for(unsigned i=0;i<plan->count;i++){
  bb_rsp_event *event=&plan->events[i];bb_phrase_hit hit;
  if(event->offset>=frames||(i&&event->offset<plan->events[i-1].offset))return 0;
  int result=bb_phrase_hit_resolve(&p->phrase,&p->controls,event->payload,&hit);
  if(!result)return 0;
  if(result==2)continue;
  /* Drum B's release policy substitutes the two auxiliary-only sources on
   * the native 808.  The serial owner applies the same mapping before it
   * triggers the kit, so worker eligibility never changes the sound. */
  if((hit.voice==BB_FDV_SY||hit.voice==BB_FDV_ZAP) && machine!=1)return 0;
  unsigned voice=bb_voice_fallback(hit.voice,65535u|(1u<<BB_PHRASE_SHAKER));
  if(voice==BB_FALLBACK_NONE)return 0;
  job->hits[job->count++]=(bb_rj_hit){event->offset,(uint8_t)voice,0,hit.velocity};
  if((event->payload>>23)&3)plan->next.delayed++;else plan->next.primary++;
 }
 return bb_rj_plan_valid(job);
}
#endif
