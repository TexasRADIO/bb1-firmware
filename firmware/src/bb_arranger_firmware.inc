/* SPDX-License-Identifier: GPL-3.0-only */
/* Main-loop arrangement page and source resolver. No stock-track storage or
 * source arrays are edited. This RAM project is explicitly UNSAVED until a
 * versioned project codec/restore path is implemented. */
#include "bb/bb_arr_controller.h"
static struct {
    bb_arrangement model;
    bb_arr_sources sources;
    bb_arr_controller controller;
    bb_browser browse[3];
    bb_section_bundle preview;
} bb_arr_workspace __attribute__((section(".pool")));
static uint8_t bb_arr_ready,bb_arr_page,bb_arr_source_page,bb_arr_source_target,bb_arr_no_dillo,bb_arr_remove;
static uint16_t bb_arr_ticks=16;
static uint32_t bb_arr_remove_revision;
static char bb_arr_prompt[48];
static void bb_record_owner_reset(void),bb_record_boot(void);
static void bb_arr_reset_owner(void){
    bb_record_owner_reset();
    if(bb_arr_ready)(void)bb_arr_controller_init(&bb_arr_workspace.controller,&bb_arr_workspace.model,&bb_arr_workspace.sources);
    bb_arr_page=bb_arr_source_page=bb_arr_remove=0;
}
static int bb_arr_midi_start(void);
static void bb_arr_boot(void){
#if BB_WITH_PROJECT_FILES
    bb_project_owner_scene_replaced();
#endif
    bb_record_boot();
    bb_arr_ready=0;bb_arr_page=bb_arr_source_page=bb_arr_source_target=bb_arr_no_dillo=bb_arr_remove=0;bb_arr_ticks=16;
    if(!bb_factory_ready)return;
    bb_arr_init(&bb_arr_workspace.model);
    if(bb_arr_sources_init(&bb_arr_workspace.sources,&bb_factory_catalogue))return;
    for(unsigned i=0;i<3;i++)bb_arr_workspace.browse[i]=bb_factory_browser;
    if(bb_arr_controller_init(&bb_arr_workspace.controller,&bb_arr_workspace.model,&bb_arr_workspace.sources))return;
    bb_arr_ready=1;bb_midi_program_start=bb_arr_midi_start;
}
static const char *bb_arr_result(int result){
    return result==BB_ARR_HISTORY_FULL?BB_TX(TX_UNDO_FULL_PROJECT_RETAINED):
        result==BB_ARR_SECTION_FULL?BB_TX(TX_32_SECTIONS_FULL):result==BB_ARR_ROW_FULL?BB_TX(TX_64_ROWS_FULL):
        result==BB_ARR_EXHAUSTED?BB_TX(TX_PROJECT_ID_EXHAUSTED):result==BB_ARR_NO_UNDO?"NO UNDO":
        result==BB_ARR_NO_REDO?"NO REDO":BB_TX(TX_ARRANGEMENT_EDIT_REFUSED);
}
static const char *bb_arr_ctl_result(int result){
    return result==BB_ARR_CTL_BUSY?BB_TX(TX_BUSY_MUSIC_UNCHANGED):result==BB_ARR_CTL_LATE?BB_TX(TX_MISSED_BOUNDARY_STOPPED):
        result==BB_ARR_CTL_SOURCE?BB_TX(TX_SOURCE_NOT_SUPPORTED):result==BB_ARR_CTL_CHANGED?BB_TX(TX_PROJECT_CHANGED_START_AGAIN):
        result==BB_ARR_CTL_OWNER?BB_TX(TX_INSTRUMENT_EDIT_SONG_RELEASED):BB_TX(TX_ARRANGEMENT_REQUEST_REFUSED);
}
static int bb_arr_edit_allowed(void){
    if(bb_arr_controller_can_edit(&bb_arr_workspace.controller,&bb_sound))return 1;
    bb_say(BB_TX(TX_STOP_WAIT_TO_EDIT_SONG));return 0;
}
static int bb_arr_transport(void){
    if(!bb_arr_ready||!bb_arr_workspace.model.row_count||(!bb_arr_page&&!bb_arr_workspace.controller.armed))return 0;
    bb_arr_controller *c=&bb_arr_workspace.controller;
    int r=bb_face.requested.running?bb_arr_controller_stop(c,&bb_sound):bb_arr_controller_start(c,&bb_sound);
    if(r)bb_say(bb_arr_ctl_result(r));else {
        bb_arr_remove=0;
        bb_say(bb_face.requested.running?BB_TX(TX_SONG_STOP_REQUESTED):BB_TX(TX_SONG_START_REQUESTED));
    }
    return 1;
}
static int bb_arr_midi_start(void){
    if(!bb_arr_ready||(!bb_arr_page&&!bb_arr_workspace.controller.armed))return 0;
    int r=bb_arr_controller_start(&bb_arr_workspace.controller,&bb_sound);
    if(r){bb_say(bb_arr_ctl_result(r));return r==BB_ARR_CTL_BUSY?-1:1;}
    bb_arr_remove=0;bb_say(BB_TX(TX_SONG_START_REQUESTED));return 1;
}
#include "bb_record_firmware.inc"
static int bb_arr_count_pending(void){return bb_arr_ready&&bb_arr_workspace.controller.edit_ticket;}
static int bb_arr_count_request(unsigned m,const bb_phrase_view *view,unsigned voice,unsigned step,unsigned count){
 if(bb_record.phase){bb_say(BB_TX(TX_FINISH_TAKE_FIRST));return -1;}
 bb_section_status status;if(!bb_arr_ready||!bb_audio_section_status_read(&bb_sound,&status)||!status.playing_section)return 0;
 if(!view){bb_say(BB_TX(TX_SONG_SOURCE_CHANGED));return -1;}
 int result=bb_arr_controller_ratchet(&bb_arr_workspace.controller,&bb_sound,view,m,voice,step,count);
 if(result){bb_say(result==BB_ARR_CTL_SOURCE&&bb_arr_workspace.controller.source_error==BB_SOURCE_WORK_FULL?BB_TX(TX_16_WORK_PARTS_FULL_SONG_KEPT):bb_arr_ctl_result(result));return -1;}
 bb_pe.song.ticket=bb_arr_workspace.controller.edit_ticket;bb_pe.song.machine=(uint8_t)m;
 bb_pe.song.undo=0;return 1;
}
/* SONG entries occupy the same eight-entry journal as native and rich edits.
 * The arrangement history supplies the immutable before/after transaction,
 * but cannot expose a ninth step Undo after journal ageing. */
static int bb_arr_count_undo(void){
 bb_section_status status;
 const bb_pe_entry *e=bb_pe.edit.count?&bb_pe.edit.history[bb_pe.edit.count-1u]:NULL;
 if(!e||e->kind!=BB_PE_ENTRY_SONG_COUNT){
  if(bb_arr_ready&&bb_audio_section_status_read(&bb_sound,&status)&&status.playing_section){bb_say(BB_TX(TX_NOTHING_TO_UNDO_IN_SONG));return 1;}
  return 0;
 }
 if(!bb_arr_ready||!bb_audio_phrase_view_read(&bb_sound,e->machine,&bb_rich_views[e->machine])||
    !bb_rich_views[e->machine].valid){bb_say(BB_TX(TX_UNDO_SOURCE_CHANGED_KEPT));return 1;}
 int result=bb_arr_controller_ratchet_undo(&bb_arr_workspace.controller,&bb_sound,&bb_rich_views[e->machine],
    e->machine,e->source_ticket,e->source_epoch);
 if(result){bb_say(bb_arr_ctl_result(result));return 1;}
 bb_pe.song.ticket=bb_arr_workspace.controller.edit_ticket;bb_pe.song.machine=e->machine;
 bb_pe.song.section=e->source_ticket;bb_pe.song.revision=e->source_epoch;bb_pe.song.undo=1;
 bb_say(BB_TX(TX_SONG_RATCHET_UNDO_QUEUED));return 1;
}
static void bb_arr_count_journal(bb_arr_controller *c){
 if(!bb_pe.song.ticket||c->edit_completed!=bb_pe.song.ticket)return;
 if(c->edit_result){bb_say(BB_TX(TX_SONG_RATCHET_REFUSED_KEPT));bb_pe.song.ticket=0;return;}
 int changed=0;
 if(bb_pe.song.undo){
  const bb_pe_entry *e=bb_pe.edit.count?&bb_pe.edit.history[bb_pe.edit.count-1u]:NULL;
  changed=!e||e->kind!=BB_PE_ENTRY_SONG_COUNT||e->machine!=bb_pe.song.machine||
      e->source_ticket!=bb_pe.song.section||e->source_epoch!=bb_pe.song.revision;
  if(!changed)bb_pe.edit.count--;
 }else{
  (void)bb_pe_push(&bb_pe.edit,bb_pe.song.machine,&bb_pe.before);
  bb_pe_entry *e=&bb_pe.edit.history[bb_pe.edit.count-1u];e->kind=BB_PE_ENTRY_SONG_COUNT;
  e->source_ticket=c->edit_section;e->source_epoch=c->edit_commit_revision;
 }
 bb_say(changed?BB_TX(TX_UNDO_APPLIED_HISTORY_CHANGED):bb_pe.song.undo?BB_TX(TX_SONG_RATCHET_UNDONE):BB_TX(TX_RATCHET_APPLIED_REC_EDIT_UNDO));
 bb_pe.song.ticket=0;bb_dirty=1;
}
/* 2.0 D4: a cued SONG section reads QUEUED until the audio owner acknowledges it as playing, then APPLIED. Row + 1
 * (0 = none) and the controller's last acknowledged ticket at the cue; in the pool, not ordinary RAM. */
static struct { uint32_t seen; uint8_t row; } bb_arr_cue_fb __attribute__((section(".pool")));
static void bb_arr_cue_feedback(const bb_arr_controller *c){
    unsigned r=bb_arr_cue_fb.row-1u;
    if(!bb_arr_cue_fb.row||c->cue_row==r)return;                /* none, or still waiting for its boundary */
    /* the cue left the controller: APPLIED only on the owner's matched acknowledgement (bb_arr_controller_poll sets
     * playing_row / playing_cue / seen_applied together); a cancel, stop or refusal ends it without a word */
    if(c->playing_cue&&c->playing_row==r&&c->seen_applied&&c->seen_applied!=bb_arr_cue_fb.seen)
        bb_say(BB_TX(TX_SECTION_APPLIED));
    bb_arr_cue_fb.row=0;
}
static void bb_arr_poll(void){
    if(bb_record.phase){bb_record_poll();return;}
    if(!bb_arr_ready)return;
    bb_arr_controller *c=&bb_arr_workspace.controller;bb_section_status old=c->view;unsigned error=c->error;
    (void)bb_arr_controller_poll(c,&bb_sound);
    bb_arr_cue_feedback(c);
    bb_arr_count_journal(c);
    if(memcmp(&old,&c->view,sizeof old)){bb_face.requested.running=c->view.running;bb_dirty=1;}
    if(error!=c->error&&c->error)bb_say(bb_arr_ctl_result(c->error));
}
static void bb_arr_source_move(int banks,int parts){
    bb_browser *b=&bb_arr_workspace.browse[bb_arr_source_target];
    if(banks)(void)bb_browser_move_bank(b,banks);
    if(parts){unsigned count=(unsigned)bb_browser_part_count(b);
        if(count){int step=bb_limit(parts,-4096,4096);int n=((int)b->part_index+step)%(int)count;if(n<0)n+=(int)count;
            b->part_index=(uint16_t)n;b->part_page=(uint16_t)((unsigned)n/BB_BROWSER_PART_KEYS);}}
    bb_dirty=1;
}
static void bb_arr_add(void){
    if(!bb_arr_edit_allowed())return;
    bb_arrangement *m=&bb_arr_workspace.model;bb_arr_sources *s=&bb_arr_workspace.sources;
    if(m->row_count==BB_ARR_ROWS){bb_say(BB_TX(TX_64_ROWS_FULL));return;}
    bb_arr_section part;memset(&part,0,sizeof part);part.duration=bb_arr_ticks;
    unsigned mark=s->count;int error=0;
    for(unsigned target=0;target<3;target++){
        if(target==2&&bb_arr_no_dillo)continue;
        bb_factory_choice choice;
        if(!bb_browser_current(&bb_arr_workspace.browse[target],&choice)){error=BB_SOURCE_MISSING;break;}
        error=bb_arr_source_bind(s,&choice,target==2?BB_SOURCE_DILLO:BB_SOURCE_DRUM,&part.part[target]);
        if(error)break;
        if(!target){const uint8_t *text;size_t size;
            if(!bb_catalogue_text(&bb_factory_catalogue,bb_catalogue_find(&bb_factory_catalogue,choice.part,"name"),&text,&size)){error=BB_SOURCE_MISSING;break;}
            if(size>=sizeof part.name)size=sizeof part.name-1;
            memcpy(part.name,text,size);part.name[size]=0;}
    }
    if(!error){
        for(unsigned target=0;target<2;target++){
            error=bb_arr_source_drum(s,part.part[target],&bb_arr_workspace.preview.drum[target]);
            if(error||!bb_audio_phrase_preflight_playable(&bb_arr_workspace.preview.drum[target],bb_audio_phrase_source_voices(&bb_sound,target))){error=BB_SOURCE_CONTENT;break;}
        }
        if(!error&&!bb_arr_no_dillo)error=bb_arr_source_dillo(s,part.part[2],&bb_arr_workspace.preview.dillo);
    }
    int result=error?BB_ARR_INVALID:bb_arr_section_append(m,&part,1);
    if(result){
        memset(&s->entry[mark],0,(s->count-mark)*sizeof s->entry[0]);s->count=(uint8_t)mark;
        bb_say(error==BB_SOURCE_FULL?BB_TX(TX_96_SOURCE_REFERENCES_FULL):error==BB_SOURCE_KEY_LONG?BB_TX(TX_SOURCE_NAME_TOO_LONG):
            error==BB_SOURCE_AMBIGUOUS?BB_TX(TX_AMBIGUOUS_SOURCE_ID):error?BB_TX(TX_SOURCE_NOT_AVAILABLE):bb_arr_result(result));return;
    }
    bb_arr_workspace.controller.selected=(uint8_t)(m->row_count-1);bb_arr_source_page=0;bb_dirty=1;bb_say("SECTION ADDED / RAM UNSAVED");
}
/* 2.0 N4: SONG opens from a HOME tap on STUDIO (the HOME toggle); GLOBAL white 4 still works. */
static void bb_arr_open(void){
    if(!bb_arr_ready||bb_arr_page)return;
    bb_arr_page=1;bb_arr_source_page=0;bb_arr_remove=0;bb_dirty=1;
    bb_say(BB_TX(TX_SONG_HOME_BACK_TO_STUDIO));
}
static int bb_arr_input(uint32_t pressed,uint32_t notes){
    if(!bb_arr_ready)return 0;
    uint32_t keys=bb_nav_keys_logical(notes);
    if(bb_record_input(pressed,keys))return 1;
    if(!bb_arr_page){
        if(bb_nav.page!=BB_NAV_GLOBAL||bb_midi_page||!(keys&(1u<<3)))return 0;
        bb_arr_page=1;bb_arr_source_page=0;bb_arr_remove=0;bb_dirty=1;
        bb_say(BB_TX(TX_ARRANGE_RAM_PROJECT));return 1;
    }
    if(pressed&((1u<<panel.btn[B_HOME])|(1u<<panel.btn[B_GLO]))){
        if(bb_arr_source_page&&(pressed&(1u<<panel.btn[B_HOME]))){bb_arr_source_page=0;bb_dirty=1;return 1;}  /* HOME: one step back */
        bb_arr_page=bb_arr_source_page=bb_arr_remove=0;bb_nav_force(BB_NAV_STUDIO);bb_dirty=1;return 1;
    }
    bb_arr_controller *c=&bb_arr_workspace.controller;bb_arrangement *m=&bb_arr_workspace.model;
#if BB_WITH_LIVE_CAPTURE
    if((pressed&(1u<<panel.btn[B_REC]))&&!bb_arr_source_page){
        if(!m->row_count){bb_say(BB_TX(TX_ADD_A_SECTION_FIRST));return 1;}
        if(!bb_arr_edit_allowed())return 1;
        bb_record.section=m->rows[c->selected].section;bb_record.phase=BB_RECORD_SETUP;bb_record.target=(uint8_t)(bb_face.machine==1);
        bb_record.countin=1;bb_record.accent=0;bb_record.error=NULL;bb_hint_until=0;bb_dirty=1;return 1;
    }
#endif
    int knob[4];for(unsigned i=0;i<4;i++)knob[i]=bb_read_encoder(EN_K1+i);
    int select=bb_read_encoder(EN_SELECT),banks=bb_read_encoder(EN_ALGO),parts=bb_read_encoder(EN_PRESET);
    if(pressed&(1u<<panel.btn[B_PLAY])){(void)bb_arr_transport();return 1;}
    if(pressed&(1u<<panel.btn[B_SAVE])){
#if BB_WITH_PROJECT_FILES
        bb_say(BB_TX(TX_PROJECT_FILES_HOST_TOOLS));
#else
        bb_say("RAM PROJECT / FILE SAVE NOT READY");
#endif
        return 1;}
    (void)select;(void)parts;   /* drained: SONG's knobs are labelled K1-K4 */
    if(bb_arr_source_page){
        if(knob[0])bb_arr_source_target=(uint8_t)bb_limit((int)bb_arr_source_target+bb_limit(knob[0],-2,2),0,2);
        if(knob[1]){
            bb_browser *b=&bb_arr_workspace.browse[bb_arr_source_target];
            int group=bb_limit((int)b->group+bb_limit(knob[1],-2,2),0,bb_arr_source_target==2?2:1);
            (void)bb_browser_group(b,(unsigned)group);bb_dirty=1;
        }
        bb_arr_source_move(banks,bb_limit(knob[2],-4096,4096));   /* K3 PART only; ALGO keeps bank (as on PATTERN) */
        if(knob[3]){bb_arr_ticks=(uint16_t)bb_limit((int)bb_arr_ticks+4*bb_limit(knob[3],-1023,1023),4,4096);bb_dirty=1;}   /* D4: one beat per detent */
        if(keys&(1u<<2)){bb_arr_no_dillo^=1;bb_dirty=1;}
        if(keys&1u){bb_arr_source_page=0;bb_dirty=1;return 1;}
        if(pressed&(1u<<panel.btn[B_EDIT]))bb_arr_add();
        return 1;
    }
    int move=bb_limit(knob[0],-64,64);   /* K1 ROW only: one control per job, as labelled (SELECT/PRESETS drained) */
    if(move&&m->row_count){c->selected=(uint8_t)bb_limit((int)c->selected+move,0,m->row_count-1);bb_arr_remove=0;bb_dirty=1;}
    if(knob[3]){int r=bb_arr_controller_set_loop(c,&bb_sound,knob[3]>0);if(r)bb_say(bb_arr_ctl_result(r));bb_dirty=1;}
    if((knob[1]||knob[2])&&m->row_count&&bb_arr_edit_allowed()){
        bb_arr_remove=0;
        if(knob[1]){bb_arr_row row=m->rows[c->selected];row.repeats=(uint8_t)bb_limit((int)row.repeats+bb_limit(knob[1],-63,63),1,64);
            int r=bb_arr_row_replace(m,c->selected,row);if(r)bb_say(bb_arr_result(r));}
        if(knob[2]){bb_arr_section section=*bb_arr_find(m,m->rows[c->selected].section);
            section.duration=(uint16_t)bb_limit((int)section.duration+bb_limit(knob[2],-4095,4095),1,4096);
            int r=bb_arr_section_put(m,&section,NULL);if(r)bb_say(bb_arr_result(r));}
        bb_dirty=1;
    }
    if(keys&1u){bb_arr_remove=0;bb_arr_source_page=1;bb_dirty=1;return 1;}
    if(keys&(1u<<1)){bb_arr_remove=0;bb_arr_source_page=1;bb_dirty=1;bb_say(BB_TX(TX_SELECT_A_B_DILLO_EDIT_ADDS));return 1;}
    if((keys&(1u<<2))&&m->row_count&&bb_arr_edit_allowed()){
        int r=bb_arr_row_insert(m,c->selected+1,m->rows[c->selected]);if(r)bb_say(bb_arr_result(r));else{c->selected++;bb_say(BB_TX(TX_ROW_COPIED_SHARED_SECTION));}bb_dirty=1;
    }
    if((keys&(1u<<3))&&m->row_count&&bb_arr_edit_allowed()){
        bb_arr_remove=1;bb_arr_remove_revision=m->revision;bb_dirty=1;
        const bb_arr_section *s=bb_arr_find(m,m->rows[c->selected].section);unsigned n=0;
        const char *word="DELETE ";while(*word)bb_arr_prompt[n++]=*word++;
        word=s?s->name:"ROW";while(*word&&n+2<sizeof bb_arr_prompt)bb_arr_prompt[n++]=*word++;
        bb_arr_prompt[n++]='?';bb_arr_prompt[n]=0;bb_say(bb_arr_prompt);return 1;
    }
    if(keys&(1u<<16)){bb_arr_remove=0;if(bb_arr_edit_allowed()){int r=bb_arr_undo(m);bb_say(r?bb_arr_result(r):BB_TX(TX_UNDO_PROJECT_RESTORED));}}
    if(keys&(1u<<17)){bb_arr_remove=0;if(bb_arr_edit_allowed()){int r=bb_arr_redo(m);bb_say(r?bb_arr_result(r):BB_TX(TX_REDO_PROJECT_RESTORED));}}
    if(c->selected>=m->row_count)c->selected=m->row_count?(uint8_t)(m->row_count-1):0;
    if(pressed&(1u<<panel.btn[B_EDIT])){
        if(bb_arr_remove){bb_arr_remove=0;if(m->revision!=bb_arr_remove_revision){bb_say(BB_TX(TX_ROW_CHANGED_CONFIRM_AGAIN));return 1;}if(bb_arr_edit_allowed()){int r=bb_arr_row_remove(m,c->selected);bb_say(r?bb_arr_result(r):BB_TX(TX_ROW_DELETED_UNDO_AVAILABLE));if(c->selected>=m->row_count)c->selected=m->row_count?(uint8_t)(m->row_count-1):0;bb_dirty=1;}}
        else if(m->row_count){int r=c->view.running?bb_arr_controller_cue(c,&bb_sound,c->selected):bb_arr_controller_load(c,&bb_sound,c->selected);if(r)bb_say(bb_arr_ctl_result(r));else{bb_say(c->view.running?BB_TX(TX_NEXT_SECTION_QUEUED):BB_TX(TX_SECTION_LOAD_REQUESTED));
            if(c->view.running){bb_arr_cue_fb.row=(uint8_t)(c->selected+1u);bb_arr_cue_fb.seen=c->seen_applied;}}}
    }
    return 1;
}
