/* SPDX-License-Identifier: GPL-3.0-only */
#include "bb_live_record.h"
#include <string.h>
static int apart(const void *a,size_t n,const void *b,size_t m){
 uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;
 return a&&b&&x<=UINTPTR_MAX-n&&y<=UINTPTR_MAX-m&&(x+n<=y||y+m<=x);
}
static int valid(const bb_phrase *p){
 if(!bb_phrase_ratchets_valid(p)||!p->length||p->length>BB_STEPS||p->count>BB_FACTORY_DRUM_EVENTS||
    p->lattice>BB_FDL_TRIPLETS||p->bars>4)return 0;
 unsigned per=p->lattice==BB_FDL_SIXTEENTHS?16:p->lattice==BB_FDL_EIGHTHS?8:24;
 if(p->bars&&p->length!=p->bars*per)return 0;
 for(unsigned i=0;i<p->count;i++){bb_phrase_event e;if(!bb_phrase_read(p,i,&e))return 0;}
 return 1;
}
int bb_live_record_begin_phase(bb_live_record *r,const bb_phrase *p,uint32_t revision,
                        uint64_t protected_steps,uint64_t start,uint32_t step,
                        uint32_t divisor,uint32_t phase,unsigned cell){
 if(!apart(r,sizeof *r,p,sizeof *p)||!valid(p)||!revision||!step||!divisor||phase>=step||cell>=p->length)return BB_REC_INVALID;
 if(r->state!=BB_REC_IDLE)return BB_REC_BUSY;
 memset(r,0,sizeof *r);r->before=r->take=*p;r->revision=revision;
 r->protected_steps=protected_steps;r->start_frame=start;r->step_frames=step;
 r->step_divisor=divisor;r->grid_phase=phase;r->start_cell=(uint8_t)cell;
 r->state=BB_REC_ARMED;return BB_REC_OK;
}
int bb_live_record_begin(bb_live_record *r,const bb_phrase *p,uint32_t revision,
                        uint64_t protected_steps,uint64_t start,uint32_t step){
 return bb_live_record_begin_phase(r,p,revision,protected_steps,start,step,1,0,0);
}
int bb_live_record_begin_pair_phase(bb_live_record *r,const bb_phrase *p,uint32_t revision,
                        uint64_t protected_steps,uint64_t start,uint32_t even,uint32_t odd,
                        uint32_t divisor,uint32_t phase,unsigned cell,unsigned parity){
 if(!apart(r,sizeof *r,p,sizeof *p)||!valid(p)||!revision||!even||!odd||!divisor||
    parity>1u||phase>=(parity?odd:even)||cell>=p->length||even>UINT32_MAX-odd)return BB_REC_INVALID;
 if(r->state!=BB_REC_IDLE)return BB_REC_BUSY;
 memset(r,0,sizeof *r);r->before=r->take=*p;r->revision=revision;
 r->protected_steps=protected_steps;r->start_frame=start;r->step_frames=even;
 r->odd_step_frames=even==odd?0u:odd;r->start_odd=(uint8_t)parity;
 r->step_divisor=divisor;r->grid_phase=phase;r->start_cell=(uint8_t)cell;
 r->state=BB_REC_ARMED;return BB_REC_OK;
}
static int clock_valid(const bb_live_record *r){
 if(!r->step_frames||!r->step_divisor||r->start_odd>1u)return 0;
 uint32_t odd=r->odd_step_frames?r->odd_step_frames:r->step_frames;
 if(r->odd_step_frames&&r->step_frames>UINT32_MAX-odd)return 0;
 return r->grid_phase<(r->start_odd?odd:r->step_frames);
}
int bb_live_record_wait_steps(const bb_live_record *r,unsigned n,uint64_t *wait){
 if(!apart(r,sizeof *r,wait,sizeof *wait)||!n||n>64u||!clock_valid(r)||
    (r->state!=BB_REC_ARMED&&r->state!=BB_REC_RECORDING))return BB_REC_INVALID;
 uint32_t odd=r->odd_step_frames?r->odd_step_frames:r->step_frames;
 uint64_t units=(uint64_t)(n/2u)*((uint64_t)r->step_frames+odd);
 if(n&1u)units+=r->start_odd?odd:r->step_frames;
 units-=r->grid_phase;
 *wait=(units+r->step_divisor-1u)/r->step_divisor;return BB_REC_OK;
}
int bb_live_record_hit(bb_live_record *r,uint64_t frame,unsigned voice,unsigned accent){
 if(!r||voice>=BB_PHRASE_VOICES||voice==BB_FDV_ACCENT||voice==BB_FDV_ACC||accent>1)return BB_REC_INVALID;
 if(r->state!=BB_REC_ARMED&&r->state!=BB_REC_RECORDING)return BB_REC_NO_PASS;
 if(!clock_valid(r)||
    !valid(&r->take)||r->start_cell>=r->take.length)return BB_REC_INVALID;
 if(frame<r->start_frame)return BB_REC_EARLY;
 uint64_t delta=frame-r->start_frame;
 /* Split before multiplying: delta may be UINT64_MAX. The residual
  * product is at most (UINT32_MAX-1)*UINT32_MAX + UINT32_MAX-1,
  * safely below UINT64_MAX. No floating point or per-step drift. */
 unsigned length=r->take.length,cell;
 if(!r->odd_step_frames){
  uint64_t scaled=(delta%r->step_frames)*r->step_divisor+r->grid_phase;
  cell=(unsigned)((r->start_cell+(delta/r->step_frames%length)*
      (r->step_divisor%length)+scaled/r->step_frames)%length);
  if(scaled%r->step_frames>=(r->step_frames/2u+r->step_frames%2u))cell=(cell+1u)%length;
 }else{
  /* A complete pair fits uint32. Split before multiplication, preserving
   * exact phase even at UINT64_MAX frame delta without 128-bit target code. */
  uint32_t pair=r->step_frames+r->odd_step_frames;
  uint64_t scaled=(delta%pair)*r->step_divisor+r->grid_phase+
      (r->start_odd?r->step_frames:0u);
  unsigned pairs=(unsigned)(((delta/pair%length)*(r->step_divisor%length)+scaled/pair)%length);
  uint32_t phase=(uint32_t)(scaled%pair);unsigned odd=phase>=r->step_frames;
  if(odd)phase-=r->step_frames;
  uint32_t duration=odd?r->odd_step_frames:r->step_frames;
  cell=(r->start_cell+length-r->start_odd+2u*pairs+odd)%length;
  if(phase>=duration/2u+duration%2u)cell=(cell+1u)%length;
 }
 if((r->protected_steps>>cell)&1u)return BB_REC_PROTECTED;
 for(unsigned i=0;i<r->take.count;i++){
  unsigned w=r->take.events[i];
  if((w&31u)==voice&&((w>>5)&63u)==cell){
   /* Re-striking a hit from this take with REC accents it. Original events never change: KEEP requires them
    * byte-identical (bb_live_record_copy). */
   if(accent&&i>=r->before.count&&((w>>11)&3u)!=BB_FDI_ACCENTED){
    r->take.events[i]=(uint16_t)((w&~(3u<<11))|((unsigned)BB_FDI_ACCENTED<<11));r->changed=1;return BB_REC_OK;
   }
   return BB_REC_DUPLICATE;
  }
 }
 if(r->take.count==BB_FACTORY_DRUM_EVENTS)return BB_REC_FULL;
 r->take.events[r->take.count++]=(uint16_t)(voice|(cell<<5)|
     ((accent?BB_FDI_ACCENTED:BB_FDI_UNACCENTED)<<11));
 r->changed=1;r->state=BB_REC_RECORDING;return BB_REC_OK;
}
int bb_live_record_finish(bb_live_record *r){
 if(!r||(r->state!=BB_REC_ARMED&&r->state!=BB_REC_RECORDING))return BB_REC_NO_PASS;
 r->state=BB_REC_REVIEW;return BB_REC_OK;
}
int bb_live_record_copy(const bb_live_record *r,uint32_t revision,bb_phrase *out){
 if(!apart(r,sizeof *r,out,sizeof *out))return BB_REC_INVALID;
 if(r->state!=BB_REC_REVIEW)return BB_REC_NO_PASS;
 if(revision!=r->revision)return BB_REC_CONFLICT;
 if(!clock_valid(r)||
    !valid(&r->before)||!valid(&r->take)||r->start_cell>=r->take.length||r->take.count<r->before.count||
    r->take.length!=r->before.length||r->take.lattice!=r->before.lattice||r->take.bars!=r->before.bars||
    memcmp(r->take.events,r->before.events,r->before.count*sizeof r->before.events[0]))return BB_REC_INVALID;
 /* Original source metadata is protected alongside original encoded hits.
  * New live hits start at x1; count edits belong to the separate step editor. */
 for(unsigned i=0;i<r->before.count;i++)
  if(bb_phrase_ratchet_count(&r->take,i)!=bb_phrase_ratchet_count(&r->before,i))return BB_REC_INVALID;
 for(unsigned i=r->before.count;i<r->take.count;i++){
  unsigned w=r->take.events[i],voice=w&31u,cell=(w>>5)&63u;
  if(bb_phrase_ratchet_count(&r->take,i)!=1u||voice==BB_FDV_ACCENT||voice==BB_FDV_ACC||(w>>13)||((r->protected_steps>>cell)&1u))return BB_REC_INVALID;
 }
 *out=r->take;return BB_REC_OK;
}
int bb_live_record_undo(bb_live_record *r){
 if(!r||r->state==BB_REC_IDLE)return BB_REC_NO_PASS;
 r->take=r->before;r->changed=0;r->state=BB_REC_IDLE;return BB_REC_OK;
}
