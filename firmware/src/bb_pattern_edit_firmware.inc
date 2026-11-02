/* SPDX-License-Identifier: GPL-3.0-only
 * 2.0 editable copy (review M5). STEPS used to refuse a catalogue (rich) pattern with "SOURCE PATTERN
 * VIEW". Now the first step edit makes an editable copy: the edited phrase is published with the
 * generator's seamless path (same shape, applied at the loop wrap, phase and tails kept), so the screen
 * says QUEUED, then APPLIED. The catalogue is never written, so choosing the pattern again on PATTERN
 * gives the original back. REC+EDIT undoes, newest first (8 steps, both kits). Scenes store the copy in
 * their existing rich-phrase block (BBRP, bb_rich_scene.c): the format does not change and old scenes
 * load as before. Main loop only; the audio owner applies every change. */
#if BB_WITH_RICH_DRUM_BROWSER && BB_WITH_RICH_DRUM_RENDER && BB_WITH_EVOLVE
#define BB_WITH_PATTERN_COPY 1
#define BB_PATTERN_EDIT_LIBC_PROVIDED 1
#include "bb/bb_pattern_edit.h"
#undef BB_PATTERN_EDIT_LIBC_PROVIDED
static void bb_nav_ratchet_cancel(void);
static int bb_arr_count_pending(void);
static int bb_arr_count_request(unsigned,const bb_phrase_view *,unsigned,unsigned,unsigned);
static int bb_arr_count_undo(void);
static struct {
    bb_pattern_edit edit;
    bb_phrase_index index;
    bb_phrase work, before;
    uint32_t ticket[BB_RICH_DRUMS], seen[BB_RICH_DRUMS];
    const char *applied[BB_RICH_DRUMS];
    uint8_t pending[BB_RICH_DRUMS];
    uint8_t kind[BB_RICH_DRUMS];          /* what is pending: BB_PE_KIND_EDIT pushed an entry, _UNDO popped one */
    bb_phrase undone[BB_RICH_DRUMS];      /* the entry an undo popped, put back if the owner refuses it */
    bb_pe_entry evicted[BB_RICH_DRUMS];   /* oldest entry an accepted edit pushed out of a full history */
    struct {uint32_t ticket;bb_native_ratchet_view native;uint8_t machine,kind,undo,before_count,after_count;} count;
    struct {uint32_t ticket,section,revision;uint8_t machine,undo;} song;
    uint8_t has_evicted[BB_RICH_DRUMS];   /* restored at the front if the owner later rejects that edit */
} bb_pe __attribute__((section(".pool")));
static int bb_count_busy(void){return bb_pe.count.ticket||bb_arr_count_pending();}
enum { BB_PE_KIND_EDIT = 1, BB_PE_KIND_UNDO = 2 };
static void bb_pe_reset(void){bb_nav_ratchet_cancel();memset(&bb_pe,0,sizeof bb_pe);memset(bb_rich_draft_on,0,sizeof bb_rich_draft_on);}
static void bb_pe_show(unsigned m,const bb_phrase *p){
    bb_rich_drafts[m]=bb_rich_views[m];bb_rich_drafts[m].phrase=*p;
    if(!bb_phrase_controls_prepare(p,&bb_rich_drafts[m].controls)){bb_rich_draft_on[m]=0;return;}
    bb_rich_drafts[m].valid=1;bb_rich_draft_on[m]=1;bb_dirty=1;
}
static int bb_pe_send(unsigned m,const bb_phrase *p){
    uint32_t playable=bb_audio_phrase_source_voices(&bb_sound,m);
    if(!bb_phrase_index_prepare(p,playable,&bb_pe.index)||!bb_audio_phrase_preflight_playable(&bb_pe.index,playable))return 0;
    return bb_audio_phrase_submit_seamless(&bb_sound,m,&bb_pe.index,&bb_pe.ticket[m]);
}
/* Publish `after` on layer m, remembering `before` for Undo (push=1). The history changes only once the
 * owner has accepted the request: a busy or refused send leaves every entry, including the oldest, intact. */
static int bb_pe_publish(unsigned m,const bb_phrase *before,const bb_phrase *after,int push,
                         const char *queued,const char *applied){
    if(m>=BB_RICH_DRUMS||(push&&!before)){bb_say(BB_TX(TX_EDIT_REFUSED));return 0;}
    if(!bb_pe_send(m,after)){bb_say(BB_TX(TX_BUSY_OR_SOUND_NOT_IN_KIT_RETRY));return 0;}
    bb_pe.has_evicted[m]=0;
    if(push){
        if(bb_pe.edit.count==BB_PE_HISTORY){bb_pe.evicted[m]=bb_pe.edit.history[0];bb_pe.has_evicted[m]=1;}
        (void)bb_pe_push(&bb_pe.edit,m,before);       /* cannot fail: m and before checked above */
    }
    bb_pe.pending[m]=1;bb_pe.kind[m]=(uint8_t)(push?BB_PE_KIND_EDIT:BB_PE_KIND_UNDO);
    bb_pe.applied[m]=applied;bb_pe_show(m,after);bb_say(queued);return 1;
}
/* A count request never pushes history until its matching owner ACK. */
static int bb_pe_count_request(unsigned m,unsigned voice,unsigned step,unsigned count){
 if(m>=2||bb_count_busy()||bb_pe.pending[0]||bb_pe.pending[1])return 0;
 bb_phrase_view *view=&bb_rich_views[m];
 (void)bb_audio_phrase_view_read(&bb_sound,m,view);
 if(view->valid)bb_pe.before=view->phrase;
 int song=bb_arr_count_request(m,view->valid?view:NULL,voice,step,count);
 if(song)return song>0;
 uint32_t ticket;
 if(view->valid){
  bb_section_status section;if(!bb_audio_section_status_read(&bb_sound,&section))return 0;
  bb_pe.before=view->phrase;
  if(!bb_audio_section_ratchet_submit(&bb_sound,&section,view,m,voice,step,count,&ticket))return 0;
  bb_pe.count.kind=BB_PE_ENTRY_PHRASE;bb_pe.seen[m]=view->applied;
 }else{
  if(!bb_audio_native_ratchet_view(&bb_sound,m,voice,step,&bb_pe.count.native)||
     !bb_audio_native_ratchet_submit(&bb_sound,&bb_pe.count.native,count,&ticket))return 0;
  bb_pe.count.kind=BB_PE_ENTRY_NATIVE_COUNT;
 }
 bb_pe.count.machine=(uint8_t)m;bb_pe.count.undo=0;bb_pe.count.ticket=ticket;return 1;
}
static void bb_pe_count_poll(void){
 if(!bb_pe.count.ticket)return;
 bb_section_ratchet status;
 if(!bb_audio_section_ratchet_read(&bb_sound,&status)||status.ticket!=bb_pe.count.ticket)return;
 if(status.phase==BB_SECTION_EDIT_QUEUED)return;
 unsigned m=bb_pe.count.machine;
 if(status.phase==BB_SECTION_EDIT_APPLIED){
  int history_changed=0;
  if(bb_pe.count.undo){
   const bb_native_ratchet_view *v=&bb_pe.count.native;
   int ack=bb_pe.count.kind==BB_PE_ENTRY_NATIVE_COUNT&&status.mode==2&&status.target==m&&
       status.voice==v->voice&&status.step==v->step&&status.source_ticket==v->source_ticket&&
       status.source_generation==v->epoch&&status.source_edits==v->packed&&
       status.old_count==bb_pe.count.after_count&&status.count==bb_pe.count.before_count;
   history_changed=!ack||bb_pe_pop_count_match(&bb_pe.edit,m,v->source_ticket,v->epoch,v->voice,v->step,
       bb_pe.count.before_count,bb_pe.count.after_count)!=BB_PE_OK;
  }
  else if(bb_pe.count.kind==BB_PE_ENTRY_PHRASE)(void)bb_pe_push(&bb_pe.edit,m,&bb_pe.before);
  else (void)bb_pe_push_count(&bb_pe.edit,m,bb_pe.count.native.source_ticket,bb_pe.count.native.epoch,
       bb_pe.count.native.voice,bb_pe.count.native.step,status.old_count,status.count);
  bb_say(history_changed?BB_TX(TX_UNDO_APPLIED_HISTORY_CHANGED):
      bb_pe.count.undo?BB_TX(TX_RATCHET_UNDONE):BB_TX(TX_RATCHET_APPLIED_REC_EDIT_UNDO));
 }else bb_say(BB_TX(TX_RATCHET_REFUSED_PATTERN_KEPT));
 if(bb_audio_section_ratchet_ack(&bb_sound,bb_pe.count.ticket))bb_pe.count.ticket=0;
 bb_dirty=1;
}
/* STEPS key on a rich drum pattern (bb_nav_do_keys, BB_NAV_K_STEP). */
static void bb_pe_step(unsigned key,unsigned accent){
    unsigned m=bb_face.machine;
    if(m>=BB_RICH_DRUMS||!bb_rich_active())return;
    const bb_phrase_view *v=bb_rich_view(m);
    unsigned step=bb_face.bank*16u+key,voice=bb_rich_sources[bb_rich_voices[m]];
    if(step>=v->phrase.length)return;
    bb_pe.before=v->phrase;
    int first=!bb_pe_entries(&bb_pe.edit,m);
    int r=bb_pe_toggle(&bb_pe.before,voice,step,accent,&bb_pe.work);
    if(r==BB_PE_FULL){bb_say(BB_TX(TX_PATTERN_FULL_128_HITS));return;}
    if(r!=BB_PE_OK){bb_say(BB_TX(TX_STEP_REFUSED));return;}
    (void)bb_pe_publish(m,&bb_pe.before,&bb_pe.work,1,
        first?BB_TX(TX_EDIT_COPY_STEP_QUEUED):BB_TX(TX_STEP_QUEUED),BB_TX(TX_STEP_APPLIED_REC_EDIT_UNDO));
}
/* REC+EDIT on PLAY/STEPS (BB_NAV_A_UNDO): newest edit or kept take first. */
static void bb_pe_undo_last(void){
    bb_nav_ratchet_cancel();
    if(bb_count_busy()||bb_pe.pending[0]||bb_pe.pending[1]){bb_say(BB_TX(TX_WAIT_FOR_EDIT_RETRY));return;}
    if(bb_arr_count_undo())return;
    if(bb_pe.edit.count&&bb_pe.edit.history[bb_pe.edit.count-1u].kind==BB_PE_ENTRY_NATIVE_COUNT){
        const bb_pe_entry *v=&bb_pe.edit.history[bb_pe.edit.count-1u];uint32_t ticket;
        if(!bb_audio_native_ratchet_view(&bb_sound,v->machine,v->voice,v->step,&bb_pe.count.native)||
           bb_pe.count.native.source_ticket!=v->source_ticket||bb_pe.count.native.epoch!=v->source_epoch||
           bb_ratchet_bits_get(bb_pe.count.native.packed,v->voice)!=v->after_count||
           !bb_audio_native_ratchet_submit(&bb_sound,&bb_pe.count.native,v->before_count,&ticket)){
            bb_say(BB_TX(TX_UNDO_REFUSED_PATTERN_KEPT));return;
        }
        bb_pe.count.ticket=ticket;bb_pe.count.machine=v->machine;bb_pe.count.kind=BB_PE_ENTRY_NATIVE_COUNT;bb_pe.count.undo=1;
        bb_pe.count.before_count=v->before_count;bb_pe.count.after_count=v->after_count;
        bb_say(BB_TX(TX_RATCHET_UNDO_QUEUED));return;
    }
    unsigned m;
    if(bb_pe_undo(&bb_pe.edit,&m,&bb_pe.work)!=BB_PE_OK){bb_say(BB_TX(TX_NOTHING_TO_UNDO));return;}
    if(m<BB_RICH_DRUMS)bb_pe.undone[m]=bb_pe.work;             /* restored if the owner refuses the undo */
    if(!bb_pe_publish(m,NULL,&bb_pe.work,0,BB_TX(TX_UNDO_QUEUED),
                      bb_pe_entries(&bb_pe.edit,m)?"UNDONE":BB_TX(TX_UNDONE_SOURCE_PATTERN))){
        (void)bb_pe_push(&bb_pe.edit,m,&bb_pe.work);   /* nothing changed: keep it for the next try */
    }
}
/* QUEUED -> APPLIED, refusal, or another owner replacing the pattern (bb_gen_poll's rule). */
static void bb_pe_poll(void){
    bb_pe_count_poll();
    for(unsigned m=0;m<BB_RICH_DRUMS;m++){
        bb_audio_phrase_status st;
        if(!bb_audio_phrase_status_read(&bb_sound,m,&st))continue;
        if(!st.valid){uint32_t identity[2]={0};int valid=bb_audio_native_pattern_identity(&bb_sound,m,identity);
            bb_pe_filter_native(&bb_pe.edit,m,valid,identity[0],identity[1]);
            if(bb_pe_entries(&bb_pe.edit,m)||bb_pe.pending[m]){bb_pe_forget_phrases(&bb_pe.edit,m);bb_pe.pending[m]=0;}
            bb_rich_draft_on[m]=0;bb_pe.seen[m]=0;continue;}
        bb_pe_filter_native(&bb_pe.edit,m,0,0,0);
        if(bb_pe.pending[m]){
            uint32_t t=bb_pe.ticket[m];
            if(st.state.applied==t){
                bb_pe.pending[m]=0;bb_pe.has_evicted[m]=0;bb_pe.seen[m]=t;bb_rich_draft_on[m]=0;bb_dirty=1;
                if(bb_pe.applied[m])bb_say(bb_pe.applied[m]);
            }else if(st.state.seen==t&&st.state.result==BB_RICH_REJECTED){
                bb_pe.pending[m]=0;bb_rich_draft_on[m]=0;bb_dirty=1;
                if(bb_pe.kind[m]==BB_PE_KIND_EDIT){
                    bb_pe_drop_newest(&bb_pe.edit,m);                                       /* only this layer's */
                    if(bb_pe.has_evicted[m]&&bb_pe.edit.count<BB_PE_HISTORY){              /* the oldest it pushed out */
                        for(unsigned i=bb_pe.edit.count;i;i--)bb_pe.edit.history[i]=bb_pe.edit.history[i-1u];
                        bb_pe.edit.history[0]=bb_pe.evicted[m];bb_pe.edit.count++;
                    }
                }
                else if(bb_pe.kind[m]==BB_PE_KIND_UNDO)(void)bb_pe_push(&bb_pe.edit,m,&bb_pe.undone[m]);   /* undo didn't happen */
                bb_pe.kind[m]=0;bb_pe.has_evicted[m]=0;
                bb_say(BB_TX(TX_EDIT_REFUSED_PATTERN_KEPT));
            }else if(st.state.seen&&st.state.seen!=t&&(int32_t)(st.state.seen-t)>0){
                bb_pe.pending[m]=0;bb_rich_draft_on[m]=0;bb_pe_forget_standalone(&bb_pe.edit,m);bb_dirty=1;   /* superseded */
            }
            continue;
        }
        if(st.state.applied!=bb_pe.seen[m]){
            if(bb_pe.seen[m])bb_pe_forget_standalone(&bb_pe.edit,m);
            bb_pe.seen[m]=st.state.applied;
        }
    }
}
#else
#define BB_WITH_PATTERN_COPY 0
static int bb_count_busy(void){return 0;}
static int bb_pe_count_request(unsigned m,unsigned v,unsigned s,unsigned c){return bb_audio_ratchet_request(&bb_sound,m,v,s,c);}
static void bb_pe_reset(void){}
static void bb_pe_step(unsigned key,unsigned accent){(void)key;(void)accent;bb_say("SOURCE PATTERN VIEW");}
static void bb_pe_undo_last(void){bb_say(BB_TX(TX_NOTHING_TO_UNDO));}
static void bb_pe_poll(void){}
#endif
