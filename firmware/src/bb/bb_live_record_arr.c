/* SPDX-License-Identifier: GPL-3.0-only */
#include "bb_live_record_arr.h"
#include <string.h>
#define apart(a,n,b,m) bb_arr_separated(a,n,b,m)
int bb_arr_record_commit(bb_arrangement *a,bb_arr_sources *s,uint32_t id,unsigned target,
                        const bb_live_record *r,uint32_t playable,bb_phrase_index *scratch){
 if(!a||!s||!r||!scratch||target>1||!bb_arr_validate(a)||
    !apart(a,sizeof *a,s,sizeof *s)||!apart(a,sizeof *a,r,sizeof *r)||
    !apart(s,sizeof *s,r,sizeof *r)||!apart(scratch,sizeof *scratch,a,sizeof *a)||
    !apart(scratch,sizeof *scratch,s,sizeof *s)||!apart(scratch,sizeof *scratch,r,sizeof *r))return BB_REC_INVALID;
 const bb_arr_section *found=bb_arr_find(a,id);if(!found)return BB_REC_INVALID;
 if(r->state!=BB_REC_REVIEW)return BB_REC_NO_PASS;
 if(a->revision!=r->revision)return BB_REC_CONFLICT;
 if(!r->changed)return BB_REC_UNCHANGED;
 if(!bb_arr_source_owners_valid(s))return BB_REC_INVALID;
 if(s->next_revision==UINT32_MAX)return BB_REC_EXHAUSTED;
 int error=bb_arr_source_drum(s,found->part[target],scratch);if(error)return BB_REC_SOURCE;
 if(memcmp(&scratch->phrase,&r->before,sizeof r->before))return BB_REC_CONFLICT;
 uint64_t protected_steps=0;
 if(found->part[target].source==BB_ARR_WORKING)protected_steps=s->work[found->part[target].id-1].protected_steps;
 if(protected_steps!=r->protected_steps)return BB_REC_CONFLICT;
 if(bb_live_record_copy(r,a->revision,&scratch->phrase))return BB_REC_INVALID;
 if(!bb_phrase_index_prepare(&r->take,playable,scratch))return BB_REC_INVALID;
 bb_arr_part origin=found->part[target];
 unsigned slot;uint16_t dead;error=bb_arr_source_reserve(s,a,&slot,&dead);
 if(error)return error==BB_SOURCE_WORK_FULL?BB_REC_WORK_FULL:BB_REC_INVALID;
 bb_arr_section changed=*found;
 changed.part[target]=(bb_arr_part){slot+1,s->next_revision,BB_ARR_WORKING};
 /* The journal must admit the complete edit before a source slot is used.
  * Once admitted, all remaining operations are bounded infallible copies. */
 error=bb_arr_section_put(a,&changed,NULL);if(error)return error==BB_ARR_HISTORY_FULL?BB_REC_HISTORY_FULL:BB_REC_SOURCE;
 bb_arr_source_reclaim(s,dead);
 bb_arr_work_part *p=&s->work[slot];memset(p,0,sizeof *p);p->data.drum=*scratch;
 p->origin=origin;
 p->protected_steps=protected_steps;p->revision=s->next_revision++;p->kind=BB_SOURCE_DRUM;if(slot==s->work_count)s->work_count++;
 return BB_REC_OK;
}
