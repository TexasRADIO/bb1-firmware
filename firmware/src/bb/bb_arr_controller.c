/* SPDX-License-Identifier: GPL-3.0-only */
#include "bb_arr_controller.h"
#include "bb_audio.h"
#include <string.h>
#define separate(a,n,b,m) bb_arr_separated(a,n,b,m)
static unsigned next_row(const bb_arr_controller *c){
    if(c->playing_cue)return c->return_row;
    if((unsigned)c->playing_row+1u<c->model->row_count)return c->playing_row+1u;
    return c->loop&&c->model->row_count?0u:BB_ARR_ROW_NONE;
}
static int ready(const bb_arr_controller *c){return c&&c->model&&c->sources&&bb_arr_validate(c->model);}
int bb_arr_controller_init(bb_arr_controller *c,bb_arrangement *model,bb_arr_sources *sources){
    if(!c||!model||!sources||!bb_arr_validate(model)||
       !separate(c,sizeof *c,model,sizeof *model)||!separate(c,sizeof *c,sources,sizeof *sources))return BB_ARR_CTL_INVALID;
    memset(c,0,sizeof *c);c->model=model;c->sources=sources;
    c->playing_row=c->requested_row=c->prepared_row=c->cue_row=c->return_row=c->cue_return=BB_ARR_ROW_NONE;
    return BB_ARR_CTL_OK;
}
static int prepare(bb_arr_controller *c,unsigned row){
    if(row>=c->model->row_count)return BB_ARR_CTL_INVALID;
    bb_arr_row part=c->model->rows[row];
    int result=bb_arr_prepare_section(c->model,c->sources,part.section,part.repeats,&c->preparation);
    c->source_error=(uint8_t)result;if(result)return BB_ARR_CTL_SOURCE;
    c->prepared=1;c->prepared_row=(uint8_t)row;c->prepared_revision=c->model->revision;
    return BB_ARR_CTL_OK;
}
static int publish(bb_arr_controller *c,bb_audio *a,int restart){
    if(!c->prepared||c->prepared_revision!=c->model->revision)return BB_ARR_CTL_CHANGED;
    c->preparation.restart=(uint8_t)restart;c->preparation.at_tick=(uint8_t)(!restart&&c->view.running);
    c->preparation.target_tick=c->target_tick;
    uint32_t ticket;
    if(!bb_audio_section_submit(a,&c->preparation,&ticket))return BB_ARR_CTL_BUSY;
    c->ticket=ticket;c->requested_row=c->prepared_row;c->request_cue=(uint8_t)(c->cue_row==c->requested_row&&!restart);
    c->accepted_revision=c->model->revision;c->prepared=0;
    return BB_ARR_CTL_OK;
}
int bb_arr_controller_start(bb_arr_controller *c,bb_audio *a){
    if(!ready(c)||!a||c->edit_ticket||!c->model->row_count)return BB_ARR_CTL_INVALID;
    if(!bb_audio_section_status_read(a,&c->view))return BB_ARR_CTL_OWNER;
    if(c->ticket){
        if(!c->cancel_wait&&!bb_audio_section_cancel_submit(a,c->ticket))return BB_ARR_CTL_BUSY;
        c->cancel_wait=1;c->restart_wait=1;c->prepared=0;return BB_ARR_CTL_OK;
    }
    if(c->view.phase!=BB_SECTION_FREE)return BB_ARR_CTL_BUSY;
    int result=prepare(c,0);if(result){c->error=(uint8_t)result;return result;}
    result=publish(c,a,1);if(result){c->error=(uint8_t)result;return result;}
    c->armed=1;c->restart_wait=0;c->playing_cue=0;c->cue_row=c->return_row=BB_ARR_ROW_NONE;c->error=0;
    return BB_ARR_CTL_OK;
}
int bb_arr_controller_load(bb_arr_controller *c,bb_audio *a,unsigned row){
    if(!ready(c)||!a||c->edit_ticket||row>=c->model->row_count)return BB_ARR_CTL_INVALID;
    if(!bb_audio_section_status_read(a,&c->view))return BB_ARR_CTL_OWNER;
    if(c->view.running||c->ticket||c->view.phase!=BB_SECTION_FREE)return BB_ARR_CTL_BUSY;
    int r=prepare(c,row);if(r)return r;
    r=publish(c,a,0);if(r)return r;
    c->armed=0;c->playing_cue=0;c->error=0;return BB_ARR_CTL_OK;
}
int bb_arr_controller_set_loop(bb_arr_controller *c,bb_audio *a,int on){
    if(!ready(c)||!a||c->edit_ticket)return BB_ARR_CTL_INVALID;
    on=!!on;if((unsigned)on==c->loop)return BB_ARR_CTL_OK;
    if(!on&&c->ticket&&!c->playing_cue&&c->playing_row+1u==c->model->row_count){
        if(!c->cancel_wait&&!bb_audio_section_cancel_submit(a,c->ticket))return BB_ARR_CTL_BUSY;
        c->cancel_wait=1;c->prepared=0;
    }
    c->loop=(uint8_t)on;return BB_ARR_CTL_OK;
}
int bb_arr_controller_stop(bb_arr_controller *c,bb_audio *a){
    if(!ready(c)||!a)return BB_ARR_CTL_INVALID;
    bb_command command={0};command.type=BB_CMD_TRANSPORT;
    return bb_command_push(&a->commands,&command)?BB_ARR_CTL_OK:BB_ARR_CTL_BUSY;
}
int bb_arr_controller_cue(bb_arr_controller *c,bb_audio *a,unsigned row){
    if(!ready(c)||!a||c->edit_ticket||row>=c->model->row_count)return BB_ARR_CTL_INVALID;
    if(!bb_audio_section_status_read(a,&c->view)||!c->armed||!c->view.running)return BB_ARR_CTL_OWNER;
    if(c->playing_row==BB_ARR_ROW_NONE||c->cancel_wait)return BB_ARR_CTL_BUSY;
    if(c->ticket&&!bb_audio_section_cancel_submit(a,c->ticket))return BB_ARR_CTL_BUSY;
    c->cue_return=(uint8_t)next_row(c);c->cue_row=(uint8_t)row;c->prepared=0;
    c->cancel_wait=(uint8_t)(c->ticket!=0);return BB_ARR_CTL_OK;
}
int bb_arr_controller_ratchet(bb_arr_controller *c,bb_audio *a,const bb_phrase_view *view,
                               unsigned m,unsigned voice,unsigned step,unsigned count){
 if(!ready(c)||!a||!view||!view->valid||m>1||c->edit_ticket||
    !separate(c,sizeof *c,view,sizeof *view)||!separate(c->model,sizeof *c->model,view,sizeof *view)||
    !separate(c->sources,sizeof *c->sources,view,sizeof *view))return BB_ARR_CTL_INVALID;
 bb_section_status now;if(!bb_audio_section_status_read(a,&now))return BB_ARR_CTL_OWNER;
 const bb_arr_section *part=bb_arr_find(c->model,now.playing_section);
 if(!part||part->revision!=now.playing_revision||c->model->revision!=now.playing_project_revision||
    c->cancel_wait||c->restart_wait||c->model->revision==UINT32_MAX)return BB_ARR_CTL_CHANGED;
 if(c->sources->next_revision==UINT32_MAX){c->source_error=BB_SOURCE_EXHAUSTED;return BB_ARR_CTL_SOURCE;}
 /* The existing staging buffer is borrowed; a future unsubmitted successor
  * must be regenerated. Accepted successor slots are already immutable copies. */
 c->prepared=0;bb_phrase_index *scratch=&c->preparation.drum[m];
 int error=bb_arr_source_drum(c->sources,part->part[m],scratch);
 if(error||memcmp(&scratch->phrase,&view->phrase,sizeof view->phrase))return BB_ARR_CTL_CHANGED;
 unsigned matches=0,changed=0;
 if(voice>=BB_PHRASE_VOICES||voice==BB_FDV_ACCENT||voice==BB_FDV_ACC||step>=view->phrase.length||count<1||count>4)return BB_ARR_CTL_INVALID;
 for(unsigned i=0;i<view->phrase.count;i++){unsigned w=view->phrase.events[i];
  if((w&31u)==voice&&((w>>5)&63u)==step){matches++;changed|=bb_phrase_ratchet_count(&view->phrase,i)!=count;}
 }
 if(!matches)return BB_ARR_CTL_INVALID;if(!changed)return BB_ARR_CTL_UNCHANGED;
 uint32_t playable=bb_audio_phrase_source_voices(a,m);
 if(!bb_audio_phrase_ratchet_preflight(scratch,voice,step,count)||
    (scratch->voices&~playable))return BB_ARR_CTL_SOURCE;
 unsigned slot;uint16_t dead;error=bb_arr_source_reserve(c->sources,c->model,&slot,&dead);
 if(error){c->source_error=(uint8_t)error;return BB_ARR_CTL_SOURCE;}
 uint32_t ticket;if(!bb_audio_section_ratchet_submit(a,&now,view,m,voice,step,count,&ticket))return BB_ARR_CTL_BUSY;
 c->edit_ticket=ticket;c->edit_section=part->id;c->edit_revision=c->model->revision;
 c->edit_source_revision=c->sources->next_revision;
 c->edit_undo=0;c->edit_cursor=c->model->cursor;c->edit_history_count=c->model->history_count;
 c->edit_target=(uint8_t)m;c->edit_voice=(uint8_t)voice;c->edit_step=(uint8_t)step;c->edit_count=(uint8_t)count;
 return BB_ARR_CTL_OK;
}
static int same_part(bb_arr_part x,bb_arr_part y){return x.id==y.id&&x.revision==y.revision&&x.source==y.source;}
int bb_arr_controller_ratchet_undo(bb_arr_controller *c,bb_audio *a,const bb_phrase_view *view,
    unsigned m,uint32_t section,uint32_t commit_revision){
 if(!ready(c)||!a||!view||!view->valid||m>1||c->edit_ticket||!c->model->cursor||
    !separate(c,sizeof *c,view,sizeof *view)||!separate(c->model,sizeof *c->model,view,sizeof *view)||
    !separate(c->sources,sizeof *c->sources,view,sizeof *view)||!bb_arr_source_owners_valid(c->sources))return BB_ARR_CTL_INVALID;
 const bb_arr_change *h=&c->model->history[c->model->cursor-1u];
 if(h->kind!=BB_ARR_SECTION_EDIT||h->after.id!=section||h->after.revision!=commit_revision||
    h->before.id!=section||!h->before.revision||h->before.duration!=h->after.duration||
    memcmp(h->before.name,h->after.name,sizeof h->before.name)||same_part(h->before.part[m],h->after.part[m])||
    !same_part(h->before.part[1u-m],h->after.part[1u-m])||!same_part(h->before.part[2],h->after.part[2]))return BB_ARR_CTL_CHANGED;
 const bb_arr_section *edited=bb_arr_find(c->model,section);
 if(!edited||edited->revision!=h->after.revision||!same_part(edited->part[m],h->after.part[m]))return BB_ARR_CTL_CHANGED;
 bb_section_status now;if(!bb_audio_section_status_read(a,&now))return BB_ARR_CTL_OWNER;
 const bb_arr_section *playing=bb_arr_find(c->model,now.playing_section);
 if(!playing||playing->revision!=now.playing_revision||c->model->revision!=now.playing_project_revision||
    c->model->revision==UINT32_MAX||c->cancel_wait||c->restart_wait)return BB_ARR_CTL_CHANGED;
 c->prepared=0;
 if(bb_arr_source_drum(c->sources,playing->part[m],&c->preparation.drum[0])||
    memcmp(&c->preparation.drum[0].phrase,&view->phrase,sizeof view->phrase))return BB_ARR_CTL_CHANGED;
 if(bb_arr_source_drum(c->sources,h->before.part[m],&c->preparation.drum[0])||
    bb_arr_source_drum(c->sources,h->after.part[m],&c->preparation.drum[1]))return BB_ARR_CTL_SOURCE;
 const bb_phrase *x=&c->preparation.drum[0].phrase,*y=&c->preparation.drum[1].phrase;
 if(x->length!=y->length||x->count!=y->count||x->lattice!=y->lattice||x->bars!=y->bars||
    memcmp(x->events,y->events,sizeof x->events)||!memcmp(x->ratchets,y->ratchets,sizeof x->ratchets))return BB_ARR_CTL_CHANGED;
 uint32_t ticket;
 if(!bb_audio_section_counts_restore_submit(a,&now,view,m,section,h->after.revision,h->before.revision,
      &c->preparation.drum[1],&c->preparation.drum[0],&ticket))return BB_ARR_CTL_BUSY;
 c->edit_ticket=ticket;c->edit_section=section;c->edit_revision=c->model->revision;
 c->edit_source_revision=c->sources->next_revision;c->edit_target=(uint8_t)m;c->edit_undo=1;
 c->edit_cursor=c->model->cursor;c->edit_history_count=c->model->history_count;
 return BB_ARR_CTL_OK;
}
static int ratchet_poll(bb_arr_controller *c,bb_audio *a){
 bb_section_ratchet view;if(!bb_audio_section_ratchet_read(a,&view))return BB_ARR_CTL_OWNER;
 if(view.ticket!=c->edit_ticket)return BB_ARR_CTL_OWNER;
 if(view.phase==BB_SECTION_EDIT_QUEUED)return BB_ARR_CTL_BUSY;
 int error=BB_ARR_CTL_REJECTED;
 if(view.phase==BB_SECTION_EDIT_APPLIED){
  if(c->model->revision!=c->edit_revision||c->sources->next_revision!=c->edit_source_revision||
     c->model->cursor!=c->edit_cursor||c->model->history_count!=c->edit_history_count)error=BB_ARR_CTL_CHANGED;
  else{
   int result=c->edit_undo?bb_arr_undo(c->model):bb_arr_source_ratchet(c->model,c->sources,c->edit_section,c->edit_revision,
      c->edit_target,c->edit_voice,c->edit_step,c->edit_count,
      UINT32_MAX,&c->preparation.drum[c->edit_target]); /* capability checked before owner admission */
   c->source_error=(uint8_t)result;error=result?BB_ARR_CTL_SOURCE:BB_ARR_CTL_OK;
   if(!result){c->accepted_revision=c->model->revision;c->prepared=0;}
  }
 }
 if(!bb_audio_section_ratchet_ack(a,c->edit_ticket))return BB_ARR_CTL_OWNER;
 c->edit_completed=c->edit_ticket;c->edit_result=(uint8_t)error;
 c->edit_commit_revision=error?0:c->model->revision;c->edit_ticket=0;
 if(error&&view.phase==BB_SECTION_EDIT_APPLIED){c->armed=0;(void)bb_arr_controller_stop(c,a);}
 return error;
}
int bb_arr_controller_can_edit(bb_arr_controller *c,bb_audio *a){
    if(!ready(c)||!a||c->edit_ticket||(!a->commands.enter!=!a->commands.leave))return 0;
    uintptr_t token=a->commands.enter?a->commands.enter(a->commands.lock_context):0;
    int allowed=!a->core.running&&bb_commands_idle(&a->commands)&&a->section.phase==BB_SECTION_FREE;
    if(a->commands.leave)a->commands.leave(a->commands.lock_context,token);
    return allowed;
}
int bb_arr_controller_poll(bb_arr_controller *c,bb_audio *a){
    if(!ready(c)||!a)return BB_ARR_CTL_INVALID;
    if(c->edit_ticket){int r=ratchet_poll(c,a);if(r==BB_ARR_CTL_BUSY)return BB_ARR_CTL_OK;if(r){c->error=(uint8_t)r;return r;}}
    if(!bb_audio_section_status_read(a,&c->view))return BB_ARR_CTL_OWNER;
    if(c->ticket&&c->accepted_revision!=c->model->revision){
        if(c->view.phase==BB_SECTION_FREE){c->ticket=0;c->cancel_wait=0;c->playing_row=BB_ARR_ROW_NONE;}
        else{
            if(!c->cancel_wait&&!bb_audio_section_cancel_submit(a,c->ticket))return BB_ARR_CTL_BUSY;
            c->cancel_wait=1;
        }
        c->armed=0;c->error=BB_ARR_CTL_CHANGED;
        if(c->view.running)(void)bb_arr_controller_stop(c,a);
        if(c->ticket)return c->error;
    }
    if(c->ticket){
        if(c->view.state.applied==c->ticket&&c->view.playing_project_revision==c->accepted_revision){
            c->playing_row=c->requested_row;c->playing_cue=c->request_cue;
            if(c->request_cue){c->return_row=c->cue_return;c->cue_row=BB_ARR_ROW_NONE;}
            c->seen_applied=c->ticket;c->ticket=0;c->cancel_wait=0;c->error=0;
        }else if(c->view.phase==BB_SECTION_FREE){
            if(c->cancel_wait||c->view.state.result==BB_PATTERN_CANCELLED){
                c->ticket=0;c->cancel_wait=0;
            }else{
                c->ticket=0;c->prepared=0;c->armed=0;c->error=BB_ARR_CTL_REJECTED;return c->error;
            }
        }else return BB_ARR_CTL_OK;
    }
    if(c->restart_wait&&!c->ticket){c->restart_wait=0;return bb_arr_controller_start(c,a);}
    if(c->accepted_revision&&c->accepted_revision!=c->model->revision){
        c->armed=0;c->prepared=0;c->playing_row=BB_ARR_ROW_NONE;c->seen_applied=0;c->error=BB_ARR_CTL_CHANGED;
        /* An unexpected concurrent authoring edit cannot silently change the
         * program being performed. Ordinary UI edits are stopped-only. */
        if(c->view.running)(void)bb_arr_controller_stop(c,a);
        return c->error;
    }
    if(c->seen_applied&&(!c->view.playing_section||c->view.state.applied!=c->seen_applied)){
        c->armed=0;c->prepared=0;c->playing_row=BB_ARR_ROW_NONE;c->seen_applied=0;
        c->error=BB_ARR_CTL_OWNER;return c->error;
    }
    if(!c->armed)return BB_ARR_CTL_OK;
    if(!c->view.playing_section){c->armed=0;c->playing_row=BB_ARR_ROW_NONE;c->error=BB_ARR_CTL_OWNER;return c->error;}
    if(!c->view.running)return BB_ARR_CTL_OK; /* STOP pauses; Continue retains its end */
    unsigned row=c->cue_row!=BB_ARR_ROW_NONE?c->cue_row:next_row(c);
    if(row==BB_ARR_ROW_NONE)return BB_ARR_CTL_OK; /* owner ends last row exactly */
    c->target_tick=c->view.end_tick;
    uint32_t ahead=c->target_tick-c->view.current_tick;
    if(ahead>UINT32_MAX/2u){c->error=BB_ARR_CTL_LATE;c->armed=0;return c->error;}
    if(!c->prepared){int result=prepare(c,row);if(result){c->error=(uint8_t)result;c->armed=0;return result;}}
    if(ahead>4095u)return BB_ARR_CTL_OK; /* keep prepared scratch outside owner until its bounded horizon */
    /* Decoding may have taken time. The absolute owner target is retained;
     * submission refuses if this preparation has actually missed it. */
    int result=publish(c,a,0);
    if(result){
        bb_section_status latest;
        if(bb_audio_section_status_read(a,&latest)&&(uint32_t)(c->target_tick-latest.current_tick)>UINT32_MAX/2u){
            c->error=BB_ARR_CTL_LATE;c->armed=0;return c->error;
        }
        return result;
    }
    return BB_ARR_CTL_OK;
}
