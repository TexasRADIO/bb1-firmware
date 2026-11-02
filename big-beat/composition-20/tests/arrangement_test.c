/* SPDX-License-Identifier: GPL-3.0-only */
#include "bb_arrangement.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static unsigned checks;
#define CHECK(x) do {checks++;assert(x);} while(0)
static bb_arrangement a, saved;
static bb_arr_section make(const char *name){
    bb_arr_section s={0};snprintf(s.name,sizeof s.name,"%s",name);s.duration=32;
    for(unsigned i=0;i<3;i++){s.part[i].id=100+i;s.part[i].revision=7;s.part[i].source=BB_ARR_CURATED;}
    return s;
}
static uint32_t add(const char *name){bb_arr_section s=make(name);uint32_t id=0;
    CHECK(bb_arr_section_put(&a,&s,&id)==BB_ARR_OK);return id;}
static void failed(int expected,int result){CHECK(expected==result);CHECK(!memcmp(&a,&saved,sizeof a));}
static void content_equal(const bb_arrangement *x,const bb_arrangement *y){
    CHECK(!memcmp(x->sections,y->sections,sizeof x->sections));
    CHECK(x->row_count==y->row_count);CHECK(!memcmp(x->rows,y->rows,sizeof x->rows));
}
static void journey(void){
    bb_arr_init(&a);CHECK(bb_arr_validate(&a));CHECK(!bb_arr_dirty(&a));
    const char *names[]={"INTRO","VERSE","CHORUS","BREAK","ENDING"};uint32_t id[5];
    for(unsigned i=0;i<5;i++)id[i]=add(names[i]);
    unsigned seq[]={0,1,2,3,2,4};unsigned repeats[]={1,2,1,1,1,1};
    for(unsigned i=0;i<6;i++)CHECK(!bb_arr_row_insert(&a,i,(bb_arr_row){id[seq[i]],repeats[i]}));
    saved=a;failed(BB_ARR_REFERENCED,bb_arr_section_remove(&a,id[2]));
    CHECK(bb_arr_dirty(&a));CHECK(!bb_arr_saved(&a,a.revision));CHECK(!bb_arr_dirty(&a));
    uint32_t captured=a.revision;CHECK(!bb_arr_row_move(&a,5,0));
    saved=a;failed(BB_ARR_INVALID,bb_arr_saved(&a,captured));
    CHECK(!bb_arr_undo(&a));CHECK(a.rows[5].section==id[4]);CHECK(bb_arr_dirty(&a));
    CHECK(!bb_arr_redo(&a));CHECK(a.rows[0].section==id[4]);
    CHECK(!bb_arr_undo(&a));
    bb_arr_section chorus=*bb_arr_find(&a,id[2]);chorus.duration=64;
    CHECK(!bb_arr_section_put(&a,&chorus,NULL));
    CHECK(bb_arr_find(&a,a.rows[2].section)->duration==64);
    CHECK(bb_arr_find(&a,a.rows[4].section)->duration==64); /* deliberate shared reference */
    saved=a;failed(BB_ARR_INVALID,bb_arr_section_put(&a,&chorus,NULL)); /* stale revision */
    CHECK(bb_arr_redo(&a)==BB_ARR_NO_REDO);CHECK(bb_arr_validate(&a));
    CHECK(!bb_arr_undo(&a));CHECK(bb_arr_find(&a,id[2])->duration==32);
}
static void refusals(void){
    bb_arr_init(&a);uint32_t id=add("VERSE");bb_arr_section s=make("NEW");
    s.part[1].revision=0;saved=a;failed(BB_ARR_INVALID,bb_arr_section_put(&a,&s,NULL));
    s=make("NEW");memset(s.name,'X',sizeof s.name);saved=a;failed(BB_ARR_INVALID,bb_arr_section_put(&a,&s,NULL));
    s=make("NEW");saved=a;failed(BB_ARR_INVALID,bb_arr_section_put(&a,&s,&a.next_id));
    saved=a;failed(BB_ARR_INVALID,bb_arr_row_insert(&a,0,(bb_arr_row){id,0}));
    saved=a;failed(BB_ARR_INVALID,bb_arr_row_insert(&a,0,(bb_arr_row){id,65}));
    saved=a;failed(BB_ARR_INVALID,bb_arr_row_insert(&a,0,(bb_arr_row){id+1,1}));
    saved=a;failed(BB_ARR_INVALID,bb_arr_row_remove(&a,0));
    for(unsigned i=0;i<15;i++)CHECK(!bb_arr_row_insert(&a,i,(bb_arr_row){id,1}));
    CHECK(!bb_arr_row_remove(&a,0));CHECK(a.row_count==14);
    for(unsigned i=0;i<16;i++)CHECK(!bb_arr_undo(&a));
    CHECK(a.row_count==0);CHECK(bb_arr_find(&a,id));
    CHECK(bb_arr_undo(&a)==BB_ARR_NO_UNDO);
    for(unsigned i=0;i<16;i++)CHECK(!bb_arr_redo(&a));
    CHECK(a.row_count==14);CHECK(bb_arr_validate(&a));
    a.revision=UINT32_MAX;saved=a;failed(BB_ARR_EXHAUSTED,bb_arr_row_remove(&a,0));
    saved=a;failed(BB_ARR_EXHAUSTED,bb_arr_undo(&a));
    bb_arr_init(&a);a.next_id=UINT32_MAX;s=make("END");saved=a;
    failed(BB_ARR_EXHAUSTED,bb_arr_section_put(&a,&s,NULL));
}
static void append_action(void){
 bb_arr_init(&a);bb_arr_section part=make("INTRO");
 CHECK(!bb_arr_section_append(&a,&part,2));CHECK(a.row_count==1&&a.cursor==1);
 uint32_t id=a.rows[0].section;CHECK(a.rows[0].repeats==2);CHECK(bb_arr_find(&a,id));
 CHECK(!bb_arr_undo(&a));CHECK(a.row_count==0&&!bb_arr_find(&a,id));
 CHECK(!bb_arr_redo(&a));CHECK(a.row_count==1&&bb_arr_find(&a,id));CHECK(bb_arr_validate(&a));
 saved=a;failed(BB_ARR_INVALID,bb_arr_section_append(&a,&part,0));
}
static void corrupt_history(void){
    bb_arr_init(&a);uint32_t id=add("SOURCE");
    CHECK(!bb_arr_row_insert(&a,0,(bb_arr_row){id,1}));
    a.history[1].index=255;saved=a;failed(BB_ARR_INVALID,bb_arr_undo(&a));
    a.history[1].index=0;a.history[1].new_row.section=900;
    saved=a;failed(BB_ARR_INVALID,bb_arr_undo(&a));
    a.history[1].new_row.section=id;a.history[1].kind=255;
    saved=a;failed(BB_ARR_INVALID,bb_arr_undo(&a));
    a.history[1].kind=BB_ARR_INSERT;CHECK(bb_arr_validate(&a));
    saved=a;failed(BB_ARR_INVALID,bb_arr_history_discard(&a,a.revision-1));
    CHECK(!bb_arr_history_discard(&a,a.revision));CHECK(a.row_count==1);
    CHECK(bb_arr_dirty(&a));CHECK(bb_arr_undo(&a)==BB_ARR_NO_UNDO);
    CHECK(!bb_arr_row_remove(&a,0));CHECK(!bb_arr_undo(&a));CHECK(a.row_count==1);
}
static void capacities(void){
    /* Build an imported project image: import codec is a later, separate gate. */
    bb_arr_init(&a);bb_arr_section s=make("IMPORTED");a.revision=100;a.next_id=33;
    for(unsigned i=0;i<32;i++){s.id=i+1;s.revision=2;a.sections[i]=s;}
    CHECK(bb_arr_validate(&a));s.id=s.revision=0;saved=a;
    failed(BB_ARR_SECTION_FULL,bb_arr_section_put(&a,&s,NULL));
    a.row_count=64;for(unsigned i=0;i<64;i++)a.rows[i]=(bb_arr_row){1,1};
    CHECK(bb_arr_validate(&a));saved=a;
    failed(BB_ARR_ROW_FULL,bb_arr_row_insert(&a,64,(bb_arr_row){1,1}));
    CHECK(!bb_arr_row_remove(&a,0));CHECK(a.row_count==63);
    CHECK(!bb_arr_undo(&a));CHECK(a.row_count==64);
    CHECK(!bb_arr_row_move(&a,0,63));CHECK(!bb_arr_undo(&a));CHECK(bb_arr_validate(&a));
}
static void identity(void){
    bb_arr_init(&a);uint32_t first=add("FIRST");CHECK(!bb_arr_undo(&a));uint32_t second=add("SECOND");
    CHECK(second>first);CHECK(!bb_arr_find(&a,first));
    CHECK(!bb_arr_section_remove(&a,second));CHECK(!bb_arr_undo(&a));CHECK(bb_arr_find(&a,second));
    /* In-model input is safe, but result may not alias it. */
    CHECK(!bb_arr_section_put(&a,bb_arr_find(&a,second),NULL));CHECK(bb_arr_validate(&a));
}
static uint32_t rng=1;
static unsigned random_value(void){rng=rng*1664525u+1013904223u;return rng;}
static void inverse_trials(void){
    static bb_arrangement before,after;
    for(unsigned trial=0;trial<2000;trial++){
        bb_arr_init(&a);uint32_t id=add("SOURCE");
        for(unsigned i=0;i<5;i++)CHECK(!bb_arr_row_insert(&a,i,(bb_arr_row){id,(uint8_t)(i+1)}));
        before=a;unsigned steps=0;
        for(unsigned i=0;i<8;i++){
            unsigned op=random_value()%4,n=random_value()%a.row_count;int r;unsigned cursor=a.cursor;
            if(op==0)r=bb_arr_row_insert(&a,n,(bb_arr_row){id,(uint8_t)(1+random_value()%64)});
            else if(op==1)r=bb_arr_row_remove(&a,n);
            else if(op==2)r=bb_arr_row_replace(&a,n,(bb_arr_row){id,(uint8_t)(1+random_value()%64)});
            else r=bb_arr_row_move(&a,n,(n+1)%a.row_count);
            CHECK(r==BB_ARR_OK);steps+=a.cursor!=cursor;CHECK(bb_arr_validate(&a));
        }
        after=a;
        for(unsigned i=0;i<steps;i++){CHECK(!bb_arr_undo(&a));CHECK(bb_arr_validate(&a));}
        content_equal(&a,&before);
        for(unsigned i=0;i<steps;i++){CHECK(!bb_arr_redo(&a));CHECK(bb_arr_validate(&a));}
        content_equal(&a,&after);
    }
}
static void unchanged_edits(void){
    bb_arr_init(&a);bb_arr_section input=make("UNCHANGED");
    CHECK(!bb_arr_section_append(&a,&input,1));uint32_t id=a.rows[0].section;
    CHECK(!bb_arr_saved(&a,a.revision));saved=a;
    for(unsigned i=0;i<100;i++){
        CHECK(!bb_arr_row_replace(&a,0,a.rows[0]));
        input=*bb_arr_find(&a,id);uint32_t result=0;
        CHECK(!bb_arr_section_put(&a,&input,&result));CHECK(result==id);
        CHECK(!memcmp(&a,&saved,sizeof a));CHECK(!bb_arr_dirty(&a));
    }
    /* An unchanged knob input must retain an existing Redo branch. */
    CHECK(!bb_arr_row_replace(&a,0,(bb_arr_row){id,2}));CHECK(!bb_arr_undo(&a));saved=a;
    CHECK(!bb_arr_row_replace(&a,0,a.rows[0]));
    CHECK(!bb_arr_section_put(&a,bb_arr_find(&a,id),NULL));CHECK(!memcmp(&a,&saved,sizeof a));
    CHECK(!bb_arr_redo(&a));CHECK(a.rows[0].repeats==2);
    while(a.cursor<BB_ARR_HISTORY){unsigned repeats=a.rows[0].repeats==1?2:1;
        CHECK(!bb_arr_row_replace(&a,0,(bb_arr_row){id,(uint8_t)repeats}));}
    saved=a;CHECK(!bb_arr_row_replace(&a,0,a.rows[0]));
    CHECK(!bb_arr_section_put(&a,bb_arr_find(&a,id),NULL));CHECK(!memcmp(&a,&saved,sizeof a));
    input=*bb_arr_find(&a,id);input.duration++;
    CHECK(!bb_arr_section_put(&a,&input,NULL));CHECK(a.history_count==BB_ARR_HISTORY);CHECK(bb_arr_validate(&a));
    /* Even revision exhaustion does not make a non-edit consume history. */
    a.revision=UINT32_MAX;saved=a;CHECK(!bb_arr_row_replace(&a,0,a.rows[0]));
    CHECK(!bb_arr_section_put(&a,bb_arr_find(&a,id),NULL));CHECK(!memcmp(&a,&saved,sizeof a));
    input=*bb_arr_find(&a,id);input.revision--;failed(BB_ARR_INVALID,bb_arr_section_put(&a,&input,NULL));
    /* Storage padding and bytes after the visible name are not musical edits. */
    bb_arr_init(&a);input=make("NAME");CHECK(!bb_arr_section_append(&a,&input,1));id=a.rows[0].section;
    input=*bb_arr_find(&a,id);input.name[10]='X';saved=a;
    CHECK(!bb_arr_section_put(&a,&input,NULL));CHECK(!memcmp(&a,&saved,sizeof a));
}
int main(void){unchanged_edits();journey();refusals();append_action();corrupt_history();capacities();identity();inverse_trials();
    printf("PASS %u checks; model=%zu bytes; history=%zu bytes; no audio-owner integration claimed\n",checks,sizeof a,sizeof a.history);
    return 0;
}
