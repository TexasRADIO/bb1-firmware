/* SPDX-License-Identifier: GPL-3.0-only */
#include "bb_arr_sources.h"
#include "bb_factory_drum_launch.h"
#include "bb_factory_pattern.h"
#include <string.h>
int bb_arr_separated(const void *a,size_t n,const void *b,size_t m){
    uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;
    return a&&b&&x<=UINTPTR_MAX-n&&y<=UINTPTR_MAX-m&&(x+n<=y||y+m<=x);
}
#define separate(a,n,b,m) bb_arr_separated(a,n,b,m)
static int ready(const bb_arr_sources *s){
    return s&&s->catalogue&&s->catalogue_crc&&s->count<=BB_ARR_SOURCE_REFS&&
        s->work_count<=BB_ARR_WORK_PARTS&&s->next_revision&&
        s->catalogue_crc==s->catalogue->container.crc&&
        s->catalogue_stamp==s->catalogue->container.stamp;
}
static bb_catalogue_id list(const bb_catalogue *c,unsigned group){
    static const char *const keys[]={"eras","songs","acid"};
    return group<3?bb_catalogue_find(c,(bb_catalogue_id)c->container.root,keys[group]):BB_CATALOGUE_NONE;
}
static int key(const bb_catalogue *c,bb_catalogue_id object,const char *fallback,char *out,uint8_t *used_name){
    bb_catalogue_id id=bb_catalogue_find(c,object,"id");*used_name=0;
    if(id==BB_CATALOGUE_NONE){id=bb_catalogue_find(c,object,fallback);*used_name=1;}
    const uint8_t *text;size_t n;
    if(!bb_catalogue_text(c,id,&text,&n)||!n)return BB_SOURCE_INVALID;
    if(n>=BB_ARR_SOURCE_KEY)return BB_SOURCE_KEY_LONG;
    for(size_t i=0;i<n;i++)if(!text[i])return BB_SOURCE_INVALID;
    memcpy(out,text,n);out[n]=0;return BB_SOURCE_OK;
}
static int same_key(const bb_catalogue *c,bb_catalogue_id object,const char *field,const char *wanted){
    const uint8_t *text;size_t n,length=0;
    while(length<BB_ARR_SOURCE_KEY&&wanted[length])length++;
    if(length==BB_ARR_SOURCE_KEY)return 0;
    return bb_catalogue_text(c,bb_catalogue_find(c,object,field),&text,&n)&&n==length&&!memcmp(text,wanted,n);
}
static int unique(const bb_catalogue *c,bb_catalogue_id array,const char *field,const char *wanted){
    unsigned found=0;
    for(size_t i=0;i<bb_catalogue_count(c,array);i++)if(same_key(c,bb_catalogue_at(c,array,i),field,wanted))found++;
    return found==1;
}
int bb_arr_sources_init(bb_arr_sources *out,const bb_catalogue *c){
    if(!out||!c||!c->validated||!c->container.crc||!bb_catalogue_private(c,out,sizeof *out))return BB_SOURCE_INVALID;
    memset(out,0,sizeof *out);out->catalogue=c;out->catalogue_crc=c->container.crc;
    out->catalogue_stamp=c->container.stamp;out->next_revision=1;return BB_SOURCE_OK;
}
int bb_arr_sources_reindex_staged(bb_arr_sources *s){
    if(!ready(s))return BB_SOURCE_REVISION;
    for(unsigned i=0;i<s->count;i++){
        bb_arr_source_entry *e=&s->entry[i];
        if(e->group>2||e->kind>BB_SOURCE_DILLO||e->bank_name>1||e->part_name>1||
           !e->bank[0]||!e->part[0])return BB_SOURCE_INVALID;
        bb_browser b;if(!bb_browser_init(&b,s->catalogue)||!bb_browser_group(&b,e->group))return BB_SOURCE_MISSING;
        unsigned found=0;uint16_t bank=0,part=0;
        for(unsigned n=0;n<bb_browser_bank_count(&b);n++){
            b.bank_index=(uint16_t)n;b.part_index=0;bb_factory_choice c;
            if(!bb_browser_current(&b,&c))continue;
            if(!same_key(s->catalogue,c.bank,e->bank_name?(e->group?"song":"name"):"id",e->bank))continue;
            for(unsigned k=0;k<bb_browser_part_count(&b);k++){
                b.part_index=(uint16_t)k;
                if(bb_browser_current(&b,&c)&&same_key(s->catalogue,c.part,e->part_name?"name":"id",e->part)){
                    bank=(uint16_t)n;part=(uint16_t)k;found++;
                }
            }
        }
        if(found!=1)return found?BB_SOURCE_AMBIGUOUS:BB_SOURCE_MISSING;
        e->bank_index=bank;e->part_index=part;
    }
    return BB_SOURCE_OK;
}
int bb_arr_source_bind(bb_arr_sources *s,const bb_factory_choice *choice,unsigned kind,bb_arr_part *out){
    if(!ready(s))return BB_SOURCE_REVISION;
    if(kind>BB_SOURCE_DILLO||!choice||!out||!bb_factory_choice_valid(s->catalogue,choice)||
       !separate(s,sizeof *s,out,sizeof *out)||!separate(choice,sizeof *choice,out,sizeof *out))return BB_SOURCE_INVALID;
    bb_arr_source_entry e;memset(&e,0,sizeof e);e.kind=(uint8_t)kind;e.group=choice->group;
    e.bank_index=choice->bank_index;e.part_index=choice->part_index;
    int r=key(s->catalogue,choice->bank,choice->group?"song":"name",e.bank,&e.bank_name);if(r)return r;
    r=key(s->catalogue,choice->part,"name",e.part,&e.part_name);if(r)return r;
    if(!unique(s->catalogue,list(s->catalogue,e.group),e.bank_name?(e.group?"song":"name"):"id",e.bank)||
       !unique(s->catalogue,bb_catalogue_find(s->catalogue,choice->bank,"parts"),e.part_name?"name":"id",e.part))return BB_SOURCE_AMBIGUOUS;
    for(unsigned i=0;i<s->count;i++)if(!memcmp(&e,&s->entry[i],sizeof e)){
        *out=(bb_arr_part){i+1,s->catalogue_crc,BB_ARR_CURATED};return BB_SOURCE_OK;
    }
    if(s->count==BB_ARR_SOURCE_REFS)return BB_SOURCE_FULL;
    s->entry[s->count]=e;s->count++;
    *out=(bb_arr_part){s->count,s->catalogue_crc,BB_ARR_CURATED};return BB_SOURCE_OK;
}
int bb_arr_source_choice(const bb_arr_sources *s,bb_arr_part ref,unsigned kind,bb_factory_choice *out){
    if(!ready(s))return BB_SOURCE_REVISION;
    if(!out||!separate(s,sizeof *s,out,sizeof *out)||!bb_catalogue_private(s->catalogue,out,sizeof *out))return BB_SOURCE_INVALID;
    if(ref.source!=BB_ARR_CURATED||!ref.id||ref.id>s->count)return BB_SOURCE_MISSING;
    if(ref.revision!=s->catalogue_crc)return BB_SOURCE_REVISION;
    const bb_arr_source_entry *e=&s->entry[ref.id-1];if(e->kind!=kind)return BB_SOURCE_KIND;
    bb_browser view;memset(&view,0,sizeof view);view.catalogue=s->catalogue;view.ready=1;view.group=e->group;
    view.bank_index=e->bank_index;view.part_index=e->part_index;
    view.bank_page=(uint16_t)(e->bank_index/BB_BROWSER_BANK_KEYS);view.part_page=(uint16_t)(e->part_index/BB_BROWSER_PART_KEYS);
    bb_factory_choice choice;if(!bb_browser_current(&view,&choice))return BB_SOURCE_MISSING;
    if(!same_key(s->catalogue,choice.bank,e->bank_name?(e->group?"song":"name"):"id",e->bank)||
       !same_key(s->catalogue,choice.part,e->part_name?"name":"id",e->part))return BB_SOURCE_MISSING;
    *out=choice;return BB_SOURCE_OK;
}
static int work(const bb_arr_sources *s,bb_arr_part ref,unsigned kind,const bb_arr_work_part **out){
    if(!ready(s))return BB_SOURCE_REVISION;
    if(ref.source!=BB_ARR_WORKING||!ref.id||ref.id>s->work_count)return BB_SOURCE_MISSING;
    const bb_arr_work_part *p=&s->work[ref.id-1];
    if(!ref.revision||!p->revision||p->revision!=ref.revision)return BB_SOURCE_REVISION;
    if(p->kind!=kind)return BB_SOURCE_KIND;*out=p;return BB_SOURCE_OK;
}
int bb_arr_source_drum(const bb_arr_sources *s,bb_arr_part ref,bb_phrase_index *out){
    if(!s||!out||!separate(s,sizeof *s,out,sizeof *out))return BB_SOURCE_INVALID;
    if(ref.source==BB_ARR_WORKING){const bb_arr_work_part *p;int r=work(s,ref,BB_SOURCE_DRUM,&p);
        if(r)return r;*out=p->data.drum;return BB_SOURCE_OK;}
    bb_factory_choice choice;int r=bb_arr_source_choice(s,ref,BB_SOURCE_DRUM,&choice);if(r)return r;
    return bb_factory_prepare_rich_drum(s->catalogue,&choice,out)==BB_FDRUM_OK?BB_SOURCE_OK:BB_SOURCE_CONTENT;
}
int bb_arr_source_dillo(const bb_arr_sources *s,bb_arr_part ref,bb_pattern_data *out){
    if(!s||!out||!separate(s,sizeof *s,out,sizeof *out))return BB_SOURCE_INVALID;
    if(ref.source==BB_ARR_WORKING){const bb_arr_work_part *p;int r=work(s,ref,BB_SOURCE_DILLO,&p);
        if(r)return r;*out=p->data.dillo;return BB_SOURCE_OK;}
    bb_factory_choice choice;int r=bb_arr_source_choice(s,ref,BB_SOURCE_DILLO,&choice);if(r)return r;
    return bb_factory_prepare_synth_data(s->catalogue,&choice,0,out)==BB_FACTORY_PATTERN_OK?BB_SOURCE_OK:BB_SOURCE_CONTENT;
}
static int parent_counts(const bb_arr_sources *s,uint8_t *refs){
    if(!ready(s)||(s->work_count<16u&&(s->reserved>>s->work_count)))return 0;
    memset(refs,0,BB_ARR_WORK_PARTS);
    for(unsigned i=0;i<s->work_count;i++){
        const bb_arr_work_part *w=&s->work[i];
        if(!w->revision){
            if((s->reserved>>i)&1u)return 0;
            const uint8_t *bytes=(const uint8_t*)w;
            for(unsigned j=0;j<sizeof *w;j++)if(bytes[j])return 0;
            continue;
        }
        if(w->kind>BB_SOURCE_DILLO||w->revision>=s->next_revision)return 0;
        for(unsigned j=0;j<i;j++)if(s->work[j].revision==w->revision)return 0;
        if(w->origin.source==BB_ARR_WORKING){
            bb_arr_part p=w->origin;
            if(!p.id||p.id>s->work_count||!p.revision||p.revision>=w->revision||
               s->work[p.id-1u].revision!=p.revision||s->work[p.id-1u].kind!=w->kind)return 0;
            refs[p.id-1u]++;
        }else if(w->origin.source!=BB_ARR_CURATED||!w->origin.id||
            w->origin.id>s->count||w->origin.revision!=s->catalogue_crc||
            s->entry[w->origin.id-1u].kind!=w->kind)return 0;
    }
    return 1;
}
int bb_arr_source_owners_valid(const bb_arr_sources *s){
    uint8_t refs[BB_ARR_WORK_PARTS];return parent_counts(s,refs);
}
int bb_arr_source_owners_rebuild(bb_arr_sources *s){
    uint8_t refs[BB_ARR_WORK_PARTS];if(!parent_counts(s,refs))return BB_SOURCE_INVALID;
    return BB_SOURCE_OK;
}
int bb_arr_source_release(bb_arr_sources *s,bb_arr_part p){
    if(!bb_arr_source_owners_valid(s)||p.source!=BB_ARR_WORKING||!p.id||p.id>s->work_count||
       p.revision!=s->work[p.id-1u].revision||!((s->reserved>>(p.id-1u))&1u))return BB_SOURCE_INVALID;
    s->reserved=(uint16_t)(s->reserved&~(1u<<(p.id-1u)));return BB_SOURCE_OK;
}
static int owner_section_valid(const bb_arr_sources *s,const bb_arr_section *section){
    if(!section->id)return 1;
    for(unsigned m=0;m<3;m++){
        bb_arr_part p=section->part[m];unsigned kind=m==2?BB_SOURCE_DILLO:BB_SOURCE_DRUM;
        if(m==2&&!p.id)continue;
        if(p.source==BB_ARR_WORKING){
            if(!p.id||p.id>s->work_count||p.revision!=s->work[p.id-1u].revision||s->work[p.id-1u].kind!=kind)return 0;
        }else if(p.source!=BB_ARR_CURATED||!p.id||p.id>s->count||p.revision!=s->catalogue_crc||s->entry[p.id-1u].kind!=kind)return 0;
    }
    return 1;
}
int bb_arr_source_reserve(const bb_arr_sources *s,const bb_arrangement *a,unsigned *slot,uint16_t *dead){
    uint8_t refs[BB_ARR_WORK_PARTS],parents[BB_ARR_WORK_PARTS];
    if(!s||!a||!slot||!dead||!separate(s,sizeof *s,a,sizeof *a)||
       !separate(slot,sizeof *slot,s,sizeof *s)||!separate(dead,sizeof *dead,s,sizeof *s)||
       !separate(slot,sizeof *slot,a,sizeof *a)||!separate(dead,sizeof *dead,a,sizeof *a)||
       !separate(slot,sizeof *slot,dead,sizeof *dead))return BB_SOURCE_INVALID;
    if(!parent_counts(s,parents)||bb_arr_next_work_refs(a,refs))return BB_SOURCE_INVALID;
    for(unsigned i=0;i<BB_ARR_SECTIONS;i++)if(!owner_section_valid(s,&a->sections[i]))return BB_SOURCE_INVALID;
    for(unsigned i=0;i<a->history_count;i++){
        const bb_arr_change *c=&a->history[i];
        if((c->kind==BB_ARR_SECTION_EDIT||c->kind==BB_ARR_APPEND)&&
           (!owner_section_valid(s,&c->before)||!owner_section_valid(s,&c->after)))return BB_SOURCE_INVALID;
    }
    /* At most192 direct +16 parent +1 caller references per physical slot. */
    for(unsigned i=0;i<BB_ARR_WORK_PARTS;i++)refs[i]=(uint8_t)(refs[i]+parents[i]+((s->reserved>>i)&1u));
    uint16_t mask=0;unsigned progress;
    do{progress=0;
        for(unsigned i=0;i<s->work_count;i++)if(s->work[i].revision&&!refs[i]&&!((mask>>i)&1u)){
            mask=(uint16_t)(mask|(1u<<i));progress=1;bb_arr_part p=s->work[i].origin;
            if(p.source==BB_ARR_WORKING){if(!refs[p.id-1u])return BB_SOURCE_INVALID;refs[p.id-1u]--;}
        }
    }while(progress);
    for(unsigned i=0;i<BB_ARR_WORK_PARTS;i++)if(!refs[i]&&(i>=s->work_count||!s->work[i].revision||((mask>>i)&1u))){
        *slot=i;*dead=mask;return BB_SOURCE_OK;
    }
    return BB_SOURCE_WORK_FULL;
}
void bb_arr_source_reclaim(bb_arr_sources *s,uint16_t dead){
    for(unsigned i=0;i<s->work_count;i++)if((dead>>i)&1u){
        memset(&s->work[i],0,sizeof s->work[i]);
    }
}
int bb_arr_source_fork(bb_arr_sources *s,bb_arr_part ref,unsigned kind,bb_arr_part *out){
    if(!ready(s)||!out||kind>BB_SOURCE_DILLO||!separate(s,sizeof *s,out,sizeof *out))return BB_SOURCE_INVALID;
    if(!bb_arr_source_owners_valid(s))return BB_SOURCE_INVALID;
    unsigned slot=0;while(slot<s->work_count&&s->work[slot].revision)slot++;
    if(slot==BB_ARR_WORK_PARTS)return BB_SOURCE_WORK_FULL;
    if(s->next_revision==UINT32_MAX)return BB_SOURCE_EXHAUSTED;
    bb_arr_work_part copy;memset(&copy,0,sizeof copy);
    int r=kind==BB_SOURCE_DRUM?bb_arr_source_drum(s,ref,&copy.data.drum):bb_arr_source_dillo(s,ref,&copy.data.dillo);
    if(r)return r;
    copy.origin=ref;copy.kind=(uint8_t)kind;copy.revision=s->next_revision;
    if(ref.source==BB_ARR_WORKING)copy.protected_steps=s->work[ref.id-1].protected_steps;
    s->work[slot]=copy;if(slot==s->work_count)s->work_count++;s->next_revision++;
    s->reserved=(uint16_t)(s->reserved|(1u<<slot));
    *out=(bb_arr_part){slot+1u,copy.revision,BB_ARR_WORKING};return BB_SOURCE_OK;
}
int bb_arr_prepare_section(const bb_arrangement *a,const bb_arr_sources *s,uint32_t id,
                           unsigned repeats,bb_section_bundle *out){
    if(!bb_arr_validate(a)||!s||!out||!repeats||repeats>BB_ARR_REPEAT_MAX||
       !separate(a,sizeof *a,out,sizeof *out)||!separate(s,sizeof *s,out,sizeof *out))return BB_SOURCE_INVALID;
    const bb_arr_section *section=bb_arr_find(a,id);if(!section)return BB_SOURCE_MISSING;
    int r=bb_arr_source_drum(s,section->part[0],&out->drum[0]);if(r)return r;
    r=bb_arr_source_drum(s,section->part[1],&out->drum[1]);if(r)return r;
    if(section->part[2].id){r=bb_arr_source_dillo(s,section->part[2],&out->dillo);if(r)return r;}
    else{memset(&out->dillo,0,sizeof out->dillo);out->dillo.length=16;}
    out->section=section->id;out->revision=section->revision;out->project_revision=a->revision;
    out->at_tick=0;out->target_tick=0;out->restart=0;out->wait_ticks=0;out->duration_ticks=(uint32_t)section->duration*repeats;return BB_SOURCE_OK;
}

/* D2 immutable SONG edit, sharing the recorder's journal-first admission tail. */
int bb_arr_source_ratchet(bb_arrangement *a,bb_arr_sources *s,uint32_t id,
                          uint32_t expected,unsigned target,unsigned voice,
                          unsigned step,unsigned count,uint32_t playable,
                          bb_phrase_index *scratch){
 if(!a||!s||!scratch||target>1u||!bb_arr_validate(a)||!ready(s)||
    !separate(a,sizeof *a,s,sizeof *s)||
    !separate(scratch,sizeof *scratch,a,sizeof *a)||
    !separate(scratch,sizeof *scratch,s,sizeof *s)||
    !bb_arr_source_owners_valid(s))return BB_SOURCE_INVALID;
 if(a->revision!=expected)return BB_SOURCE_REVISION;
 const bb_arr_section *found=bb_arr_find(a,id);if(!found)return BB_SOURCE_MISSING;
 int error=bb_arr_source_drum(s,found->part[target],scratch);if(error)return error;
 bb_phrase *phrase=&scratch->phrase;
 if(voice>=BB_PHRASE_VOICES||voice==BB_FDV_ACCENT||voice==BB_FDV_ACC||
    step>=phrase->length||count<1u||count>4u)return BB_SOURCE_INVALID;
 unsigned matches=0,changed=0;
 for(unsigned i=0;i<phrase->count;i++){
  unsigned w=phrase->events[i];if((w&31u)==voice&&((w>>5)&63u)==step){
   matches++;changed|=bb_phrase_ratchet_count(phrase,i)!=count;
  }
 }
 if(!matches)return BB_SOURCE_CONTENT;
 if(!changed)return BB_SOURCE_UNCHANGED;
 if(s->next_revision==UINT32_MAX)return BB_SOURCE_EXHAUSTED;
 if(!bb_audio_phrase_ratchet_preflight(scratch,voice,step,count)||
    !bb_phrase_ratchet_set(phrase,voice,step,count)||
    !bb_phrase_index_valid(scratch,playable))return BB_SOURCE_CONTENT;
 bb_arr_part origin=found->part[target];uint64_t protected_steps=0;
 if(origin.source==BB_ARR_WORKING)protected_steps=s->work[origin.id-1u].protected_steps;
 unsigned slot;uint16_t dead;error=bb_arr_source_reserve(s,a,&slot,&dead);if(error)return error;
 bb_arr_section edited=*found;
 edited.part[target]=(bb_arr_part){slot+1u,s->next_revision,BB_ARR_WORKING};
 error=bb_arr_section_put(a,&edited,NULL);
 if(error)return error==BB_ARR_HISTORY_FULL?BB_SOURCE_HISTORY:BB_SOURCE_INVALID;
 bb_arr_source_reclaim(s,dead);
 bb_arr_work_part *p=&s->work[slot];memset(p,0,sizeof *p);
 p->data.drum=*scratch;p->origin=origin;p->protected_steps=protected_steps;
 p->revision=s->next_revision++;p->kind=BB_SOURCE_DRUM;
 if(slot==s->work_count)s->work_count++;
 return BB_SOURCE_OK;
}
