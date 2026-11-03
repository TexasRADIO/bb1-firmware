/* SPDX-License-Identifier: GPL-3.0-only */
/* D4: a section length (16th steps, 16 per bar) as bars: "4", "1.5" on a beat, else steps "17S". */
static void bb_arr_fmt_bars(char *o,unsigned t){
    static const char *const q[4]={"",".25",".5",".75"};
    unsigned bars=t/16u,rem=t%16u,n=0;char d[6];
    if(rem%4u){fmt_int(o,(int32_t)t);while(o[n])n++;o[n]='S';o[n+1]=0;return;}
    fmt_int(d,(int32_t)bars);for(unsigned i=0;d[i];i++)o[n++]=d[i];
    for(const char *x=q[rem/4u];*x;x++)o[n++]=*x;
    o[n]=0;
}
static uint8_t bb_arr_record_was;
static char bb_arr_rows[4][40],bb_arr_values[4][16],bb_arr_now[24],bb_arr_next[24],bb_arr_sub[48];
static void bb_arr_text(char *out,unsigned cap,const char *text){
    unsigned i=0;while(text&&text[i]&&i+1<cap){out[i]=text[i];i++;}out[i]=0;
}
static void bb_arr_row_name(unsigned row,char *out,unsigned cap){
    bb_arrangement *m=&bb_arr_workspace.model;
    const bb_arr_section *s=row<m->row_count?bb_arr_find(m,m->rows[row].section):NULL;
    bb_arr_text(out,cap,s?s->name:"NONE");
}
static int bb_arr_draw(void){
    if((!bb_arr_page&&!bb_record.phase)||!bb_arr_ready||bb_help_visible){   /* 2.0: a pattern take draws here too */
        if(bb_arr_record_was&&!bb_record.phase&&!bb_arr_page){bb_arr_record_was=0;bb_v3_invalidate(&bb_v3);}   /* back to PLAY/STEPS */
        return 0;
    }
    /* A named delete confirmation belongs to the live prompt only. Selecting,
     * cancelling, undoing or starting must not leave its old toast visible. */
    if(!bb_arr_remove&&bb_hint==bb_arr_prompt)bb_hint_until=0;
    bb_arr_controller *c=&bb_arr_workspace.controller;bb_arrangement *m=&bb_arr_workspace.model;
    bb_ui_in *in=&bb_v3_in;
    if(bb_v3_shown&&(int32_t)(fm1_ms-bb_v3_next)<0)return 1;
    bb_v3_next=fm1_ms+33u;
    if(ux.light||ux.mono){ux.light=0;ux.mono=0;ux.gen++;}
    if(!bb_v3_shown){bb_v3_init(&bb_v3,fm1_ms,bb_face.requested.running);bb_v3_invalidate(&bb_v3);}
    if(bb_arr_record_was!=!!bb_record.phase){bb_arr_record_was=(uint8_t)!!bb_record.phase;bb_v3_invalidate(&bb_v3);}
    memset(in,0,sizeof *in);bb_v3_common(in,BB_UI_ARRANGE);in->knob_turning=BB_UI_NO;in->legend=0;   /* D4: SONG's title line is its key legend */
    if(!bb_record.phase&&text_w(&AF_S,bb_v3_legend)>224)(void)bb_nav_legend(&bb_nav,bb_v3_legend,sizeof bb_v3_legend,2);
    if(!bb_record.phase&&text_w(&AF_S,bb_v3_legend)>224)(void)bb_nav_legend(&bb_nav,bb_v3_legend,sizeof bb_v3_legend,3);
    in->page_name=bb_arr_source_page?"SONG / ADD":BB_TX(TX_SONG_HOME);   /* N1 strip + N6 breadcrumb */
    bb_arr_row_name(c->playing_row,bb_arr_now,sizeof bb_arr_now);
    bb_arr_row_name(c->ticket?c->requested_row:BB_ARR_ROW_NONE,bb_arr_next,sizeof bb_arr_next);
    in->pattern_name=bb_arr_now;in->next_name=bb_arr_next;
#if BB_WITH_PROJECT_FILES
    const char *file_state=bb_project_owner_dirty()?BB_TX(TX_EDITED_RAM_PROJECT_FILE_TOOLS):BB_TX(TX_FILE_SNAPSHOT_NO_NEW_EDITS);
#else
    const char *file_state=bb_arr_dirty(m)?"UNSAVED RAM / SAVE NOT READY":"RAM PROJECT / ADD A SECTION";
#endif
    in->subtitle=bb_arr_remove?BB_TX(TX_REMOVE_THIS_ROW_EDIT_HOME):c->view.ended?BB_TX(TX_SONG_ENDED_PLAY_FROM_START):
        file_state;
    in->title=bb_v3_legend;   /* 2.1 P1: the keys' line is bb_nav's legend (the same table as the lights) */
    in->cursor=in->playing_item=in->queued_item=BB_UI_NO;
    const char *labels[4]={"ROW","REPEATS","BARS","LOOP"};   /* D4: lengths in bars; stored in 16th steps */
    unsigned values[4]={m->row_count?c->selected+1u:0,0,0,c->loop};
    if(c->selected<m->row_count){values[1]=m->rows[c->selected].repeats;
        const bb_arr_section *s=bb_arr_find(m,m->rows[c->selected].section);if(s)values[2]=s->duration;}
#if BB_WITH_LIVE_CAPTURE
    if(bb_record.phase){
        const bb_arr_section *section=bb_arr_find(m,bb_record.section);
        if(!section&&c->selected<m->row_count)section=bb_arr_find(m,m->rows[c->selected].section);
        in->page_name=bb_record.target?"RECORD B":"RECORD A";in->instr=bb_record.target?BB_UI_INST_B:BB_UI_INST_A;in->instr_name=bb_record.target?"KIT B":"KIT A";
        in->pattern_name=bb_record.pattern?BB_TX(TX_PLAYING_PATTERN):section?section->name:BB_TX(TX_SELECT_A_SECTION);in->next_name=bb_record.phase>=BB_RECORD_REVIEW?BB_TX(TX_KEEP_OR_DISCARD):"";
        in->subtitle=bb_record.clear_undo?BB_TX(TX_CLEAR_UNDO_HISTORY_TO_KEEP):bb_record.error?bb_record.error:bb_record.phase==BB_RECORD_SETUP?BB_TX(TX_RAM_TAKE_SOURCE_GRID):
          bb_record.phase==BB_RECORD_START?BB_TX(TX_WAITING_FOR_SOURCE_ACK):bb_record.phase==BB_RECORD_COUNT?BB_TX(TX_COUNT_IN_RECORD_NEXT_BAR):
          bb_record.phase==BB_RECORD_LIVE?BB_TX(TX_RECORDING_ORIGINAL_KEPT):bb_record.phase==BB_RECORD_STOP?BB_TX(TX_STOPPING_TAKE_IN_RAM):
          bb_record.pattern?BB_TX(TX_EDIT_KEEPS_AS_A_COPY):BB_TX(TX_REVIEW_TAKE_NOT_KEPT);
        if(bb_record.pattern&&bb_record.phase==BB_RECORD_START)in->subtitle=bb_record.error?bb_record.error:BB_TX(TX_STARTING_COUNT_IN_NEXT);
        in->title=bb_record.clear_undo?BB_TX(TX_EDIT_CLEAR_KEEP_REC_CANCEL):bb_record.phase==BB_RECORD_SETUP?BB_TX(TX_REC_STARTS_HOME_CANCELS):bb_record.phase>=BB_RECORD_REVIEW?BB_TX(TX_EDIT_KEEP_REC_DISCARD):BB_TX(TX_PADS_1_11_REC_FINISHES);
        in->small_items=1;in->items=3;in->item[0]=BB_TX(TX_K1_TARGET_A_B_K2_START);in->item[1]=BB_TX(TX_K3_ACCENT_GRID_OVERDUB);
        in->item[2]=bb_record.phase==BB_RECORD_SETUP?BB_TX(TX_K4_LAYER_ON_HOME_CANCEL):bb_record.phase>=BB_RECORD_REVIEW?
          (bb_record.pattern?BB_TX(TX_REC_EDIT_UNDOES_A_KEPT_TAKE):BB_TX(TX_UNDO_AFTER_KEEP_FILE_SAVE_LATER)):BB_TX(TX_PAD_HITS_OVERDUB_NO_ERASE);
        /* Reuse a row buffer while the recorder owns this page. The take's
         * frozen swing is authoritative after capture begins, including .5%. */
        unsigned record_swing=bb_record.phase>BB_RECORD_START&&bb_swing_valid(bb_record.clock.swing)?
            bb_record.clock.swing:bb_face.requested.swing;
        bb_arr_text(bb_arr_rows[0],sizeof bb_arr_rows[0],BB_TX(TX_K3_ACCENT_SWING));
        {unsigned n=(unsigned)str_len(bb_arr_rows[0]);
         if(n+5u<sizeof bb_arr_rows[0]){fmt_int(bb_arr_rows[0]+n,(int32_t)(bb_swing_x2(record_swing)/2u));
          if(bb_swing_x2(record_swing)&1u){n=(unsigned)str_len(bb_arr_rows[0]);
           bb_arr_text(bb_arr_rows[0]+n,sizeof bb_arr_rows[0]-n,".5");}}}
        in->item[1]=bb_arr_rows[0];
        if(bb_record.pattern)in->item[0]=bb_record.target?BB_TX(TX_KIT_B_NEAREST_STEP):BB_TX(TX_KIT_A_NEAREST_STEP);
        in->cursor=BB_UI_NO;labels[0]="TARGET";labels[1]="START";labels[2]="ACCENT";labels[3]="NEW HITS";
        values[0]=bb_record.target;values[1]=bb_record.countin;values[2]=bb_record.accent;
        values[3]=bb_record.take.take.count>=bb_record.take.before.count?bb_record.take.take.count-bb_record.take.before.count:0;
    }else
#endif
    if(bb_arr_source_page){
        bb_browser *b=&bb_arr_workspace.browse[bb_arr_source_target];bb_factory_choice choice;
        in->title=bb_v3_legend;
        in->subtitle=bb_arr_no_dillo?BB_TX(TX_DILLO_NONE_A_B_SOURCES_KEPT):BB_TX(TX_A_B_DILLO_PATTERNS_ONLY);
        labels[0]="TARGET";labels[1]="GROUP";labels[2]="PART";labels[3]="BARS";
        values[0]=bb_arr_source_target;values[1]=b->group;values[2]=b->part_index+1u;values[3]=bb_arr_ticks;
        if(bb_browser_current(b,&choice)){
            bb_v3_cat(bb_catalogue_find(&bb_factory_catalogue,choice.bank,b->group?"song":"name"),bb_arr_rows[0],sizeof bb_arr_rows[0]);
            bb_v3_cat(bb_catalogue_find(&bb_factory_catalogue,choice.part,"name"),bb_arr_rows[1],sizeof bb_arr_rows[1]);
            in->item[0]=bb_arr_rows[0];in->item[1]=bb_arr_rows[1];in->items=2;in->cursor=1;
        }
    }else{
        unsigned first=c->selected/4u*4u;
        for(unsigned i=0;i<4&&first+i<m->row_count;i++){
            unsigned row=first+i;char *out=bb_arr_rows[i];const bb_arr_section *part=bb_arr_find(m,m->rows[row].section);
            out[0]=(char)('0'+(row+1)/10);out[1]=(char)('0'+(row+1)%10);out[2]=' ';
            bb_arr_text(out+3,sizeof bb_arr_rows[i]-3,part?part->name:"MISSING");
            in->item[i]=out;in->items++;
            if(row==c->selected)in->cursor=(uint8_t)i;
            if(row==c->playing_row&&c->view.playing_section)in->playing_item=(uint8_t)i;
            if(c->ticket&&row==c->requested_row)in->queued_item=(uint8_t)i;
        }
    }
    for(unsigned i=0;i<4;i++){
        in->knob[i].label=labels[i];fmt_int(bb_arr_values[i],(int32_t)values[i]);in->knob[i].text=bb_arr_values[i];
        if(labels[i][0]=='B'&&labels[i][1]=='A'&&labels[i][2]=='R')bb_arr_fmt_bars(bb_arr_values[i],values[i]);
        in->knob[i].bar=(uint8_t)(values[i]>64?255:values[i]*255u/64u);
    }
#if BB_WITH_LIVE_CAPTURE
    if(bb_record.phase){in->knob[0].text=bb_record.target?"B":"A";in->knob[1].text=bb_record.countin?"NEXT":"NOW";in->knob[2].text=bb_record.accent?"ON":"OFF";if(bb_record.phase==BB_RECORD_SETUP){in->knob[3].label="LAYER";unsigned layer=bb_record_layer();in->knob[3].text=layer&2?"MUTED":layer&1?"ON":"OFF";}}
    else
#endif
    /* Keep the A/B identity visible in the fixed-width target value. */
    if(bb_arr_source_page)in->knob[0].text=bb_arr_source_target==0?"KIT A":bb_arr_source_target==1?"KIT B":"DILLO";
    else in->knob[3].text=c->loop?"ON":"OFF";
    /* 2.1 P1: the key map is bb_nav's LEDs (bb_v3_common), as on every page */
    bb_v3_turns(in,BB_UI_ARRANGE);   /* D4: ROW / REPEATS / BARS / LOOP show NAME + value when turned */
    bb_v3_frame(&bb_v3,in);bb_v3_shown=1;bb_dirty=0;return 1;
}
