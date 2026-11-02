/* SPDX-License-Identifier: GPL-3.0-only */
#include "bb_arrangement.h"
#include <stddef.h>
#include <string.h>
static int part_ok(const bb_arr_part *p,int optional){
    if(!p->id)return optional&&!p->revision&&!p->source;
    return p->revision&&(p->source==BB_ARR_CURATED||(p->source==BB_ARR_WORKING&&p->id<=BB_ARR_WORK_LIMIT));
}
static int bounded_name(const char *s){for(unsigned i=0;i<BB_ARR_NAME;i++)if(!s[i])return 1;return 0;}
static int same_name(const char *a,const char *b){
    for(unsigned i=0;i<BB_ARR_NAME;i++){if(a[i]!=b[i])return 0;if(!a[i])return 1;}
    return 0;
}
static int section_ok(const bb_arr_section *s){
    if(!s->id||!s->revision||!s->name[0]||!bounded_name(s->name)||
       !s->duration||s->duration>BB_ARR_DURATION_MAX)return 0;
    for(unsigned i=0;i<3;i++)if(!part_ok(&s->part[i],i==2))return 0;
    return 1;
}
const bb_arr_section *bb_arr_find(const bb_arrangement *a,uint32_t id){
    if(!a||!id)return NULL;
    for(unsigned i=0;i<BB_ARR_SECTIONS;i++)if(a->sections[i].id==id)return &a->sections[i];
    return NULL;
}
static int row_ok(const bb_arrangement *a,bb_arr_row row){
    return row.repeats&&row.repeats<=BB_ARR_REPEAT_MAX&&bb_arr_find(a,row.section);
}
/* History is RAM-only, never accepted from project files. Still bound every
 * record before Undo/Redo so corrupt kinds or indexes cannot become writes. */
static int change_shape(const bb_arr_change *c){
    switch(c->kind){
    case BB_ARR_SECTION_EDIT:
        return c->index<BB_ARR_SECTIONS&&
            (!c->before.id||section_ok(&c->before))&&
            (!c->after.id||section_ok(&c->after))&&
            (c->before.id||c->after.id)&&
            (!c->before.id||!c->after.id||c->before.id==c->after.id);
    case BB_ARR_INSERT:case BB_ARR_REMOVE:case BB_ARR_REPLACE:
        return c->index<BB_ARR_ROWS;
    case BB_ARR_MOVE:return c->index<BB_ARR_ROWS&&c->destination<BB_ARR_ROWS;
    case BB_ARR_APPEND:return c->index<BB_ARR_SECTIONS&&c->destination<BB_ARR_ROWS&&
        !c->before.id&&section_ok(&c->after)&&c->new_row.section==c->after.id&&
        c->new_row.repeats&&c->new_row.repeats<=BB_ARR_REPEAT_MAX;
    default:return 0;
    }
}
static void section_refs(uint8_t *refs,const bb_arr_section *s,int delta){
    if(!s->id)return;
    for(unsigned i=0;i<3;i++)if(s->part[i].source==BB_ARR_WORKING)
        refs[s->part[i].id-1u]=(uint8_t)(refs[s->part[i].id-1u]+delta);
}
static void change_refs(uint8_t *refs,const bb_arr_change *c,int delta){
    if(c->kind==BB_ARR_SECTION_EDIT||c->kind==BB_ARR_APPEND){
        section_refs(refs,&c->before,delta);section_refs(refs,&c->after,delta);
    }
}
static void collect_refs(const bb_arrangement *a,uint8_t *refs){
    memset(refs,0,BB_ARR_WORK_LIMIT);
    for(unsigned i=0;i<BB_ARR_SECTIONS;i++)section_refs(refs,&a->sections[i],1);
    for(unsigned i=0;i<a->history_count;i++)change_refs(refs,&a->history[i],1);
}
/* Maximum192 direct owner references (32x3 sections +16x2x3 history),
 * so maintained uint8 counters cannot overflow for a validated model. */
_Static_assert(BB_ARR_SECTIONS*3+BB_ARR_HISTORY*6<=UINT8_MAX,"owner count width");
static int shape_valid(const bb_arrangement *a){
    if(!a||!a->revision||!a->next_id||a->saved_revision>a->revision||
       a->row_count>BB_ARR_ROWS||a->history_count>BB_ARR_HISTORY||a->cursor>a->history_count)return 0;
    for(unsigned i=0;i<a->history_count;i++)if(!change_shape(&a->history[i]))return 0;
    for(unsigned i=0;i<BB_ARR_SECTIONS;i++)if(a->sections[i].id){
        const bb_arr_section *s=&a->sections[i];
        if(!section_ok(s)||s->revision>a->revision||s->id>=a->next_id)return 0;
        for(unsigned j=0;j<i;j++)if(a->sections[j].id==s->id)return 0;
    }
    for(unsigned i=0;i<a->row_count;i++)if(!row_ok(a,a->rows[i]))return 0;
    return 1;
}
int bb_arr_validate(const bb_arrangement *a){
    uint8_t refs[BB_ARR_WORK_LIMIT];if(!shape_valid(a))return 0;
    collect_refs(a,refs);return !memcmp(refs,a->work_refs,sizeof refs);
}
int bb_arr_work_rebuild(bb_arrangement *a){
    if(!shape_valid(a))return BB_ARR_INVALID;
    collect_refs(a,a->work_refs);return BB_ARR_OK;
}
int bb_arr_next_work_refs(const bb_arrangement *a,uint8_t refs[BB_ARR_WORK_LIMIT]){
    uintptr_t p=(uintptr_t)refs,b=(uintptr_t)a;
    if(!refs||!bb_arr_validate(a)||p>UINTPTR_MAX-BB_ARR_WORK_LIMIT||b>UINTPTR_MAX-sizeof *a||
       (p<b+sizeof *a&&b<p+BB_ARR_WORK_LIMIT))return BB_ARR_INVALID;
    memcpy(refs,a->work_refs,BB_ARR_WORK_LIMIT);
    for(unsigned i=a->cursor;i<a->history_count;i++)change_refs(refs,&a->history[i],-1);
    if(a->cursor==BB_ARR_HISTORY)change_refs(refs,&a->history[0],-1);
    return BB_ARR_OK;
}
void bb_arr_init(bb_arrangement *a){
    if(a){memset(a,0,sizeof *a);a->revision=a->saved_revision=a->next_id=1;}
}
int bb_arr_dirty(const bb_arrangement *a){return a&&a->revision!=a->saved_revision;}
int bb_arr_saved(bb_arrangement *a,uint32_t revision){
    if(!bb_arr_validate(a)||revision!=a->revision)return BB_ARR_INVALID;
    a->saved_revision=revision;return BB_ARR_OK;
}
static int ready(const bb_arrangement *a){
    if(!bb_arr_validate(a))return BB_ARR_INVALID;
    if(a->revision==UINT32_MAX)return BB_ARR_EXHAUSTED;
    return BB_ARR_OK;
}
static void insert(bb_arrangement *a,unsigned n,bb_arr_row row){
    /* At most63 copied rows; target SDK intentionally has no memmove. */
    for(unsigned i=a->row_count;i>n;i--)a->rows[i]=a->rows[i-1];
    a->rows[n]=row;a->row_count++;
}
static void remove_row(bb_arrangement *a,unsigned n){
    for(unsigned i=n+1u;i<a->row_count;i++)a->rows[i-1u]=a->rows[i];
    memset(&a->rows[--a->row_count],0,sizeof *a->rows);
}
static void move(bb_arrangement *a,unsigned from,unsigned to){
    bb_arr_row row=a->rows[from];remove_row(a,from);insert(a,to,row);
}
static void apply(bb_arrangement *a,const bb_arr_change *c,int forward){
    if(c->kind==BB_ARR_SECTION_EDIT||c->kind==BB_ARR_APPEND){
        section_refs(a->work_refs,&a->sections[c->index],-1);
        section_refs(a->work_refs,forward?&c->after:&c->before,1);
    }
    switch(c->kind){
    case BB_ARR_SECTION_EDIT:a->sections[c->index]=forward?c->after:c->before;break;
    case BB_ARR_INSERT:if(forward)insert(a,c->index,c->new_row);else remove_row(a,c->index);break;
    case BB_ARR_REMOVE:if(forward)remove_row(a,c->index);else insert(a,c->index,c->old_row);break;
    case BB_ARR_REPLACE:a->rows[c->index]=forward?c->new_row:c->old_row;break;
    case BB_ARR_MOVE:move(a,forward?c->index:c->destination,forward?c->destination:c->index);break;
    case BB_ARR_APPEND:
        if(forward){a->sections[c->index]=c->after;insert(a,c->destination,c->new_row);}
        else{remove_row(a,c->destination);a->sections[c->index]=c->before;}break;
    }
    a->revision++;
}
static int commit(bb_arrangement *a,const bb_arr_change *c){
    for(unsigned i=a->cursor;i<a->history_count;i++)change_refs(a->work_refs,&a->history[i],-1);
    a->history_count=a->cursor;
    if(a->cursor==BB_ARR_HISTORY){
        change_refs(a->work_refs,&a->history[0],-1);
        for(unsigned i=1;i<BB_ARR_HISTORY;i++)a->history[i-1u]=a->history[i];
        a->cursor--;a->history_count--;
    }
    apply(a,c,1);a->history[a->cursor++]=*c;a->history_count=a->cursor;
    change_refs(a->work_refs,c,1);
    for(unsigned i=a->history_count;i<BB_ARR_HISTORY;i++)memset(&a->history[i],0,sizeof a->history[i]);
    return BB_ARR_OK;
}
int bb_arr_section_put(bb_arrangement *a,const bb_arr_section *input,uint32_t *id){
    if(!input)return BB_ARR_INVALID;
    /* Snapshot before output/mutation: supports editing a section in place. */
    bb_arr_section s=*input;if(!bb_arr_validate(a))return BB_ARR_INVALID;
    int result;
    /* Output must be independent of the entire live model and input object. */
    if(id){uintptr_t p=(uintptr_t)id,b=(uintptr_t)a,q=(uintptr_t)input;
        if(p>UINTPTR_MAX-sizeof *id||b>UINTPTR_MAX-sizeof *a||q>UINTPTR_MAX-sizeof *input||
           (p<b+sizeof *a&&b<p+sizeof *id)||(p<q+sizeof *input&&q<p+sizeof *id))return BB_ARR_INVALID;
    }
    unsigned slot=BB_ARR_SECTIONS;
    if(s.id){
        const bb_arr_section *old=bb_arr_find(a,s.id);
        if(!old)return BB_ARR_NOT_FOUND;
        if(old->revision!=s.revision)return BB_ARR_INVALID;
        slot=(unsigned)(old-a->sections);
        int same=s.duration==old->duration&&same_name(s.name,old->name);
        for(unsigned i=0;i<3;i++)same=same&&s.part[i].id==old->part[i].id&&
            s.part[i].revision==old->part[i].revision&&s.part[i].source==old->part[i].source;
        /* Repeated clamped knob input is not an edit. Preserve saved state,
         * Redo, revisions and bounded history, including at capacity. */
        if(same){if(id)*id=s.id;return BB_ARR_OK;}
    }else{
        if(s.revision)return BB_ARR_INVALID;
        if(a->next_id==UINT32_MAX)return BB_ARR_EXHAUSTED;
        for(unsigned i=0;i<BB_ARR_SECTIONS;i++)if(!a->sections[i].id){slot=i;break;}
        if(slot==BB_ARR_SECTIONS)return BB_ARR_SECTION_FULL;
        s.id=a->next_id;
    }
    result=ready(a);if(result)return result;
    s.revision=a->revision+1;if(!section_ok(&s))return BB_ARR_INVALID;
    bb_arr_change c={0};c.kind=BB_ARR_SECTION_EDIT;c.index=(uint8_t)slot;
    c.before=a->sections[slot];c.after=s;
    if(!input->id)a->next_id++; /* IDs never reused after Undo or deletion. */
    result=commit(a,&c);if(id)*id=s.id;return result;
}
int bb_arr_section_append(bb_arrangement *a,const bb_arr_section *input,unsigned repeats){
    if(!input||input->id||input->revision||!repeats||repeats>BB_ARR_REPEAT_MAX)return BB_ARR_INVALID;
    bb_arr_section section=*input;int r=ready(a);if(r)return r;
    if(a->row_count==BB_ARR_ROWS)return BB_ARR_ROW_FULL;
    if(a->next_id==UINT32_MAX)return BB_ARR_EXHAUSTED;
    unsigned slot=BB_ARR_SECTIONS;
    for(unsigned i=0;i<BB_ARR_SECTIONS;i++)if(!a->sections[i].id){slot=i;break;}
    if(slot==BB_ARR_SECTIONS)return BB_ARR_SECTION_FULL;
    section.id=a->next_id;section.revision=a->revision+1;if(!section_ok(&section))return BB_ARR_INVALID;
    bb_arr_change c={0};c.kind=BB_ARR_APPEND;c.index=(uint8_t)slot;c.destination=a->row_count;
    c.before=a->sections[slot];c.after=section;c.new_row.section=section.id;c.new_row.repeats=(uint8_t)repeats;
    a->next_id++;return commit(a,&c);
}
int bb_arr_section_remove(bb_arrangement *a,uint32_t id){
    int r=ready(a);if(r)return r;
    const bb_arr_section *s=bb_arr_find(a,id);if(!s)return BB_ARR_NOT_FOUND;
    for(unsigned i=0;i<a->row_count;i++)if(a->rows[i].section==id)return BB_ARR_REFERENCED;
    bb_arr_change c={0};c.kind=BB_ARR_SECTION_EDIT;c.index=(uint8_t)(s-a->sections);c.before=*s;
    return commit(a,&c);
}
int bb_arr_row_insert(bb_arrangement *a,unsigned n,bb_arr_row row){
    int r=ready(a);if(r)return r;
    if(n>a->row_count||!row_ok(a,row))return BB_ARR_INVALID;
    if(a->row_count==BB_ARR_ROWS)return BB_ARR_ROW_FULL;
    bb_arr_change c={0};c.kind=BB_ARR_INSERT;c.index=(uint8_t)n;c.new_row=row;return commit(a,&c);
}
int bb_arr_row_remove(bb_arrangement *a,unsigned n){
    int r=ready(a);if(r)return r;if(n>=a->row_count)return BB_ARR_INVALID;
    bb_arr_change c={0};c.kind=BB_ARR_REMOVE;c.index=(uint8_t)n;c.old_row=a->rows[n];return commit(a,&c);
}
int bb_arr_row_replace(bb_arrangement *a,unsigned n,bb_arr_row row){
    if(!bb_arr_validate(a)||n>=a->row_count||!row_ok(a,row))return BB_ARR_INVALID;
    if(a->rows[n].section==row.section&&a->rows[n].repeats==row.repeats)return BB_ARR_OK;
    int r=ready(a);if(r)return r;
    bb_arr_change c={0};c.kind=BB_ARR_REPLACE;c.index=(uint8_t)n;
    c.old_row=a->rows[n];c.new_row=row;return commit(a,&c);
}
int bb_arr_row_move(bb_arrangement *a,unsigned from,unsigned to){
    int r=ready(a);if(r)return r;if(from>=a->row_count||to>=a->row_count||from==to)return BB_ARR_INVALID;
    bb_arr_change c={0};c.kind=BB_ARR_MOVE;c.index=(uint8_t)from;c.destination=(uint8_t)to;return commit(a,&c);
}
static int can_apply(const bb_arrangement *a,const bb_arr_change *c,int forward){
    if(!change_shape(c))return 0;
    switch(c->kind){
    case BB_ARR_SECTION_EDIT:{
        const bb_arr_section *next=forward?&c->after:&c->before;
        const bb_arr_section *old=forward?&c->before:&c->after;
        if(a->sections[c->index].id!=old->id||a->sections[c->index].revision!=old->revision)return 0;
        if(!next->id){
            for(unsigned i=0;i<a->row_count;i++)if(a->rows[i].section==old->id)return 0;
        }else{
            if(next->id>=a->next_id||next->revision>a->revision)return 0;
            const bb_arr_section *found=bb_arr_find(a,next->id);
            if(found&&found!=&a->sections[c->index])return 0;
        }
        return 1;
    }
    case BB_ARR_INSERT:case BB_ARR_REMOVE:{
        int adding=(c->kind==BB_ARR_INSERT)==forward;
        bb_arr_row row=c->kind==BB_ARR_INSERT?c->new_row:c->old_row;
        if(!row_ok(a,row))return 0;
        return adding?(a->row_count<BB_ARR_ROWS&&c->index<=a->row_count):
            (c->index<a->row_count&&a->rows[c->index].section==row.section&&a->rows[c->index].repeats==row.repeats);
    }
    case BB_ARR_REPLACE:{
        bb_arr_row next=forward?c->new_row:c->old_row,old=forward?c->old_row:c->new_row;
        return c->index<a->row_count&&row_ok(a,next)&&
            a->rows[c->index].section==old.section&&a->rows[c->index].repeats==old.repeats;
    }
    case BB_ARR_APPEND:
        if(forward)return !a->sections[c->index].id&&a->row_count<BB_ARR_ROWS&&
            c->destination<=a->row_count&&!bb_arr_find(a,c->after.id)&&c->after.id<a->next_id;
        if(a->sections[c->index].id!=c->after.id||a->sections[c->index].revision!=c->after.revision||
           c->destination>=a->row_count||a->rows[c->destination].section!=c->after.id||
           a->rows[c->destination].repeats!=c->new_row.repeats)return 0;
        for(unsigned i=0;i<a->row_count;i++)if(i!=c->destination&&a->rows[i].section==c->after.id)return 0;
        return 1;
    case BB_ARR_MOVE:return c->index<a->row_count&&c->destination<a->row_count;
    default:return 0;
    }
}
int bb_arr_history_discard(bb_arrangement *a,uint32_t revision){
    if(!bb_arr_validate(a)||revision!=a->revision)return BB_ARR_INVALID;
    for(unsigned i=0;i<a->history_count;i++)change_refs(a->work_refs,&a->history[i],-1);
    memset(a->history,0,sizeof a->history);a->cursor=a->history_count=0;
    return BB_ARR_OK; /* No music mutation; does not mark unsaved edits saved. */
}
int bb_arr_undo(bb_arrangement *a){
    if(!bb_arr_validate(a))return BB_ARR_INVALID;
    if(a->revision==UINT32_MAX)return BB_ARR_EXHAUSTED;
    if(!a->cursor)return BB_ARR_NO_UNDO;
    if(!can_apply(a,&a->history[a->cursor-1u],0))return BB_ARR_INVALID;
    apply(a,&a->history[--a->cursor],0);return BB_ARR_OK;
}
int bb_arr_redo(bb_arrangement *a){
    if(!bb_arr_validate(a))return BB_ARR_INVALID;
    if(a->revision==UINT32_MAX)return BB_ARR_EXHAUSTED;
    if(a->cursor==a->history_count)return BB_ARR_NO_REDO;
    if(!can_apply(a,&a->history[a->cursor],1))return BB_ARR_INVALID;
    apply(a,&a->history[a->cursor++],1);return BB_ARR_OK;
}
