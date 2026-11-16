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
#if BB_WITH_PHRASE_NUDGE
/* Felucca's target UI is one translation unit.  Keep this feature-only helper
 * in that unit so a disabled experiment adds no object, padding or binary
 * drift to the release-default composition.  Host tests still compile the
 * same source as its normal standalone unit. */
#define BB_PHRASE_NUDGE_GESTURE_LIBC_PROVIDED 1
#include "bb/bb_phrase_nudge_gesture.c"
#undef BB_PHRASE_NUDGE_GESTURE_LIBC_PROVIDED
#endif
static void bb_nav_ratchet_cancel(void);
static int bb_arr_count_pending(void);
static int bb_arr_count_request(unsigned,const bb_phrase_view *,unsigned,unsigned,unsigned);
static int bb_arr_count_undo(void);
typedef struct {
    uint32_t source[2];
    uint8_t machine,voice,step,accent;
} bb_native_step_input;
#if BB_WITH_PHRASE_NUDGE
typedef union {
    bb_pe_entry evicted;
    struct { bb_phrase phrase; bb_phrase_nudges nudges; } undo;
} bb_pe_pending_backup;
#else
typedef union { bb_pe_entry evicted; bb_phrase undo; } bb_pe_pending_backup;
#endif
enum { BB_PE_NATIVE_INPUTS = 16 }; /* white step keys in one bank */
static struct {
    bb_pattern_edit edit;
    bb_phrase work, before;
#if BB_WITH_PHRASE_NUDGE
    bb_phrase_nudges work_nudges;
    union {
        bb_phrase_index index;
        bb_phrase_nudge_gesture nudge_gesture;
        bb_phrase_nudge_transaction nudge_tx;
    } scratch;
    uint32_t nudge_ticket;
    uint8_t nudge_kind;            /* 1 forward gesture, 2 Undo */
#else
    bb_phrase_index index;
#endif
    uint32_t ticket[BB_RICH_DRUMS], seen[BB_RICH_DRUMS];
    const char *applied[BB_RICH_DRUMS];
    uint8_t pending[BB_RICH_DRUMS];
    uint8_t kind[BB_RICH_DRUMS];          /* what is pending: BB_PE_KIND_EDIT pushed an entry, _UNDO popped one */
    /* A layer has one pending phrase operation. Its rollback is either an
     * Undo snapshot or an evicted oldest entry, never both. */
    bb_pe_pending_backup backup[BB_RICH_DRUMS];
    struct {uint32_t ticket;bb_native_ratchet_view native;uint8_t machine,kind,undo,before_count,after_count;} count;
    struct {uint32_t ticket,section,revision;uint8_t machine,undo;} song;
    /* One scan can emit several immediate steps. Keep their physical order
     * while the single owner edit slot is acknowledged. */
    bb_native_step_input native_inputs[BB_PE_NATIVE_INPUTS];
    uint8_t native_input_count;

    uint8_t has_evicted[BB_RICH_DRUMS];   /* restored at the front if the owner later rejects that edit */
#if BB_WITH_RETRO_TAKE
    uint32_t retro_revision;
#endif
} bb_pe __attribute__((section(".pool")));
static int bb_count_busy(void){return bb_pe.count.ticket||bb_arr_count_pending()
#if BB_WITH_PHRASE_NUDGE
    ||bb_pe.nudge_ticket||bb_pe.nudge_kind==3u
#endif
    ;}
/* Snapshot and capture admission must include steps accepted by the UI but
 * not yet submitted to the owner. The scan still runs to drain those steps. */
static int bb_pe_authoring_busy(void){return bb_count_busy()||bb_pe.native_input_count;}
enum { BB_PE_KIND_EDIT = 1, BB_PE_KIND_UNDO = 2, BB_PE_KIND_RETRO = 3 };
static void bb_pe_reset(void){bb_nav_ratchet_cancel();memset(&bb_pe,0,sizeof bb_pe);memset(bb_rich_draft_on,0,sizeof bb_rich_draft_on);}
static void bb_pe_show(unsigned m,const bb_phrase *p){
    bb_rich_drafts[m]=bb_rich_views[m];bb_rich_drafts[m].phrase=*p;
#if BB_WITH_PHRASE_NUDGE
    bb_rich_drafts[m].nudges=bb_pe.work_nudges;
#endif
    if(!bb_phrase_controls_prepare(p,&bb_rich_drafts[m].controls)){bb_rich_draft_on[m]=0;return;}
    bb_rich_drafts[m].valid=1;bb_rich_draft_on[m]=1;bb_dirty=1;
}
static int bb_pe_send(unsigned m,const bb_phrase *p,const bb_phrase_view *source){
    uint32_t playable=bb_audio_phrase_source_voices(&bb_sound,m);
#if BB_WITH_PHRASE_NUDGE
    bb_phrase_index *index=&bb_pe.scratch.index;
#else
    bb_phrase_index *index=&bb_pe.index;
#endif
    if(!bb_phrase_index_prepare(p,playable,index))return 0;
#if BB_WITH_PHRASE_NUDGE
    if(!source||!source->valid||source->applied==0u||
       !bb_phrase_nudges_valid(&source->phrase,&source->nudges)||
       !bb_phrase_nudges_valid(p,&bb_pe.work_nudges))return 0;
    index->nudges=bb_pe.work_nudges;
#else
    (void)source;
#endif
    if(!bb_audio_phrase_preflight_playable(index,playable))return 0;
#if BB_WITH_PHRASE_NUDGE
    return bb_audio_phrase_nudge_submit_exact(&bb_sound,m,index,&source->nudges,
                                               source->applied,source->edits,&bb_pe.ticket[m]);
#else
    return bb_audio_phrase_submit_seamless(&bb_sound,m,index,&bb_pe.ticket[m]);
#endif
}
#if BB_WITH_PHRASE_NUDGE
enum { BB_PE_NUDGE_END_COMMIT = 1 };
static void bb_pe_nudge_cancel(const char *notice){
    int active=bb_pe.nudge_kind==3u;
    unsigned m=bb_pe.scratch.nudge_gesture.transaction.machine;
    bb_phrase_nudge_gesture_cancel(&bb_pe.scratch.nudge_gesture);
    if(active)bb_pe.nudge_kind=0;
    if(active&&m<BB_RICH_DRUMS)bb_rich_draft_on[m]=0;
    if(notice)bb_say(notice);
    if(active||notice)bb_dirty=1;
}
static void bb_pe_nudge_value(int value){
    static char text[]="NUDGE +0/16";
    text[6]=(char)(value<0?'-':value>0?'+':' ');
    text[7]=(char)('0'+(value<0?-value:value));
    bb_say(text);
}
/* One held-step K2 gesture stays private until release.  Every detent edits
 * the same exact immutable owner snapshot; no command, journal entry or
 * partial timing state exists before the release edge. */
static void bb_pe_nudge_turn(unsigned local_step,int delta){
    unsigned m=bb_face.machine;
    if(!delta||m>=BB_RICH_DRUMS||!bb_rich_active()||bb_pe.nudge_ticket||
       bb_pe.pending[0]||bb_pe.pending[1]){bb_pe_nudge_cancel(BB_TX(TX_BUSY_RETRY));return;}
    bb_phrase_view view={0};
    if(!bb_audio_phrase_view_read(&bb_sound,m,&view)||!view.valid){
        bb_pe_nudge_cancel("NUDGE: SOURCE CHANGED");return;
    }
    unsigned step=bb_face.bank*16u+local_step;
    unsigned voice=bb_rich_sources[bb_rich_voices[m]];
    if(bb_pe.nudge_kind!=3u){
        if(!bb_phrase_nudge_gesture_begin(&bb_pe.scratch.nudge_gesture,&view.phrase,&view.nudges,
             view.applied,view.edits,m,voice,step)){
            bb_pe_nudge_cancel("NUDGE: HIT REQUIRED");return;
        }
        bb_pe.nudge_kind=3;
        bb_pe.before=view.phrase;
    }else if(bb_pe.scratch.nudge_gesture.transaction.machine!=m||
             bb_pe.scratch.nudge_gesture.transaction.voice!=voice||
             bb_pe.scratch.nudge_gesture.transaction.step!=step||
             bb_pe.scratch.nudge_gesture.transaction.source_ticket!=view.applied||
             bb_pe.scratch.nudge_gesture.transaction.source_epoch!=view.edits||
             memcmp(&bb_pe.before,&view.phrase,sizeof view.phrase)||
             memcmp(&bb_pe.scratch.nudge_gesture.transaction.before,&view.nudges,sizeof view.nudges)){
        bb_pe_nudge_cancel("NUDGE: SOURCE CHANGED");return;
    }
    if(!bb_phrase_nudge_gesture_turn(&bb_pe.scratch.nudge_gesture,&bb_pe.before,delta)){
        bb_pe_nudge_cancel("NUDGE LIMIT / QUEUE FULL");return;
    }
    bb_pe.work_nudges=bb_pe.scratch.nudge_gesture.transaction.after;
    bb_pe_show(m,&bb_pe.before);bb_pe_nudge_value(bb_pe.scratch.nudge_gesture.value);
}
static void bb_pe_nudge_end(unsigned end){
    if(bb_pe.nudge_kind!=3u)return;
    if(end!=BB_PE_NUDGE_END_COMMIT){bb_pe_nudge_cancel("NUDGE CANCELLED");return;}
    unsigned m=bb_pe.scratch.nudge_gesture.transaction.machine;bb_phrase_view view={0};
    if(m>=BB_RICH_DRUMS||!bb_audio_phrase_view_read(&bb_sound,m,&view)||!view.valid||
       memcmp(&view.phrase,&bb_pe.before,sizeof view.phrase)){
        bb_pe_nudge_cancel("NUDGE: SOURCE CHANGED");return;
    }
    bb_phrase_nudge_transaction tx;int result=bb_phrase_nudge_gesture_finish(
        &bb_pe.scratch.nudge_gesture,view.applied,view.edits,&tx);
    bb_pe.nudge_kind=0;
    if(result==BB_NUDGE_GESTURE_UNCHANGED){bb_rich_draft_on[m]=0;bb_say("NUDGE UNCHANGED");bb_dirty=1;return;}
    if(result!=BB_NUDGE_GESTURE_CHANGED||memcmp(&view.nudges,&tx.before,sizeof view.nudges)){
        bb_rich_draft_on[m]=0;bb_say("NUDGE: SOURCE CHANGED");bb_dirty=1;return;
    }
    bb_pe.work_nudges=tx.after;
    if(!bb_pe_send(m,&bb_pe.before,&view)){
        bb_rich_draft_on[m]=0;bb_say(BB_TX(TX_BUSY_RETRY));bb_dirty=1;return;
    }
    bb_pe.scratch.nudge_tx=tx;bb_pe.nudge_ticket=bb_pe.ticket[m];bb_pe.nudge_kind=1;
    bb_pe_show(m,&bb_pe.before);bb_say("NUDGE QUEUED");
}
static int bb_pe_nudge_undo(void){
    bb_phrase_nudge_transaction tx;int latest=bb_pe_nudge_latest(&bb_pe.edit,&tx);
    if(latest==BB_PE_CHANGED||latest==BB_PE_NOTHING)return 0;
    if(latest!=BB_PE_OK){bb_say(BB_TX(TX_UNDO_REFUSED_PATTERN_KEPT));return 1;}
    unsigned m=tx.machine;bb_phrase_view view={0};
    if(m>=BB_RICH_DRUMS||!bb_audio_phrase_view_read(&bb_sound,m,&view)||!view.valid||
       memcmp(&view.nudges,&tx.after,sizeof view.nudges)){
        bb_say(BB_TX(TX_UNDO_REFUSED_PATTERN_KEPT));return 1;
    }
    bb_pe.work_nudges=tx.before;
    if(!bb_pe_send(m,&view.phrase,&view)){
        bb_say(BB_TX(TX_UNDO_REFUSED_PATTERN_KEPT));return 1;
    }
    bb_pe.scratch.nudge_tx=tx;bb_pe.nudge_ticket=bb_pe.ticket[m];bb_pe.nudge_kind=2;
    bb_pe.before=view.phrase;bb_pe_show(m,&view.phrase);bb_say("NUDGE UNDO QUEUED");return 1;
}
static void bb_pe_nudge_poll(void){
    if(!bb_pe.nudge_ticket)return;
    unsigned m=bb_pe.scratch.nudge_tx.machine;bb_audio_phrase_status st;
    if(m>=BB_RICH_DRUMS||!bb_audio_phrase_status_read(&bb_sound,m,&st))return;
    uint32_t ticket=bb_pe.nudge_ticket;
    if(st.state.applied==ticket){
        unsigned kind=bb_pe.nudge_kind;
        bb_phrase_view view={0};int exact=bb_audio_phrase_view_read(&bb_sound,m,&view)&&view.valid&&
            view.applied==ticket&&!memcmp(&view.phrase,&bb_pe.before,sizeof view.phrase)&&
            !memcmp(&view.nudges,kind==1?&bb_pe.scratch.nudge_tx.after:&bb_pe.scratch.nudge_tx.before,
                    sizeof view.nudges);
        int journal=exact&&(kind==1?
            bb_pe_push_nudge(&bb_pe.edit,&view.phrase,&bb_pe.scratch.nudge_tx)==BB_PE_OK:
            bb_pe_pop_nudge_match(&bb_pe.edit,&bb_pe.scratch.nudge_tx)==BB_PE_OK);
        bb_pe.seen[m]=ticket;bb_pe.nudge_ticket=0;bb_pe.nudge_kind=0;bb_rich_draft_on[m]=0;bb_dirty=1;
        bb_say(journal?(kind==1?"NUDGE APPLIED / REC+EDIT UNDO":"NUDGE UNDONE"):
            BB_TX(TX_UNDO_APPLIED_HISTORY_CHANGED));return;
    }
    if((st.state.seen==ticket&&st.state.result==BB_RICH_REJECTED)||
       (st.state.seen&&st.state.seen!=ticket&&(int32_t)(st.state.seen-ticket)>0)){
        bb_pe.nudge_ticket=0;bb_pe.nudge_kind=0;bb_rich_draft_on[m]=0;bb_dirty=1;
        bb_say("NUDGE REFUSED / PATTERN KEPT");
    }
}
#endif
/* Publish `after` on layer m, remembering `before` for Undo (push=1). The history changes only once the
 * owner has accepted the request: a busy or refused send leaves every entry, including the oldest, intact. */
static int bb_pe_publish(unsigned m,const bb_phrase_view *source,const bb_phrase *before,const bb_phrase *after,int push,
                         const char *queued,const char *applied){
    if(m>=BB_RICH_DRUMS||(push&&!before)){bb_say(BB_TX(TX_EDIT_REFUSED));return 0;}
#if BB_WITH_PHRASE_NUDGE
    if(bb_pe.nudge_ticket||bb_pe.nudge_kind==3u){bb_say(BB_TX(TX_BUSY_RETRY));return 0;}
#endif
#if BB_WITH_PHRASE_NUDGE
    if(!source||!source->valid||
       (push&&memcmp(before,&source->phrase,sizeof *before))||
       (push&&!bb_phrase_nudges_rebase(before,&source->nudges,after,&bb_pe.work_nudges))){
        bb_say(BB_TX(TX_EDIT_REFUSED));return 0;
    }
#endif
    if(!bb_pe_send(m,after,source)){bb_say(BB_TX(TX_BUSY_OR_SOUND_NOT_IN_KIT_RETRY));return 0;}
    bb_pe.has_evicted[m]=0;
    if(push){
        if(bb_pe.edit.count==BB_PE_HISTORY){bb_pe.backup[m].evicted=bb_pe.edit.history[0];bb_pe.has_evicted[m]=1;}
#if BB_WITH_PHRASE_NUDGE
        (void)bb_pe_push_state(&bb_pe.edit,m,before,&source->nudges);
#else
        (void)bb_pe_push(&bb_pe.edit,m,before);       /* cannot fail: m and before checked above */
#endif
    }
    bb_pe.pending[m]=1;bb_pe.kind[m]=(uint8_t)(push?BB_PE_KIND_EDIT:BB_PE_KIND_UNDO);
    bb_pe.applied[m]=applied;bb_pe_show(m,after);bb_say(queued);return 1;
}
#if BB_WITH_STEP_CONDITIONS
static int bb_pe_condition_request(unsigned m,unsigned voice,unsigned step,unsigned condition){
 if(m>=BB_RICH_DRUMS||bb_count_busy()||bb_pe.pending[0]||bb_pe.pending[1])return 0;
 bb_phrase_view *view=&bb_rich_views[m];(void)bb_audio_phrase_view_read(&bb_sound,m,view);
 if(!view->valid)return 0;
 bb_pe.before=view->phrase;bb_pe.work=view->phrase;
 if(!bb_phrase_condition_set_cell(&bb_pe.work,voice,step,condition))return 0;
 if(!memcmp(&bb_pe.before,&bb_pe.work,sizeof bb_pe.work))return 1;
 return bb_pe_publish(m,view,&bb_pe.before,&bb_pe.work,1,"CONDITION QUEUED","CONDITION APPLIED / REC+EDIT UNDO");
}
#endif
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
  if(!bb_audio_section_ratchet_submit(&bb_sound,&section,view,m,voice,step,count,0,&ticket))return 0;
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
 if(bb_pe.count.kind==BB_PE_ENTRY_NATIVE_STEP){
  if(status.phase==BB_SECTION_EDIT_APPLIED){
   if(!bb_panel_sync_patterns_mask(&bb_face,&bb_sound.commands,&bb_sound.core,1u<<status.target))return;
   if(bb_pe.count.undo){
    int matched=bb_pe_pop_native_match(&bb_pe.edit,BB_PE_ENTRY_NATIVE_STEP,status.target,status.source_ticket,
          status.source_generation,status.voice,status.step,status.count,status.old_count);
    bb_say(matched==BB_PE_OK?"UNDONE":BB_TX(TX_UNDO_APPLIED_HISTORY_CHANGED));
   }else{
    (void)bb_pe_push_native(&bb_pe.edit,BB_PE_ENTRY_NATIVE_STEP,status.target,status.source_ticket,
          status.source_generation,status.voice,status.step,status.old_count,status.count);
    bb_say(BB_TX(TX_STEP_APPLIED_REC_EDIT_UNDO));
   }
  }else bb_say(BB_TX(TX_STEP_REFUSED));
  if(bb_audio_section_ratchet_ack(&bb_sound,bb_pe.count.ticket))bb_pe.count.ticket=0;
  bb_dirty=1;return;
 }

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
static void bb_pe_send_native_input(void){
 if(bb_count_busy()||!bb_pe.native_input_count)return;
 const bb_native_step_input *input=&bb_pe.native_inputs[0];
 if(bb_audio_native_step_submit(&bb_sound,input->machine,input->voice,input->step,input->accent,
       input->source,BB_NATIVE_STEP_TOGGLE,0,&bb_pe.count.ticket)){
  bb_pe.count.kind=BB_PE_ENTRY_NATIVE_STEP;bb_pe.count.undo=0;
 }else bb_say(BB_TX(TX_STEP_REFUSED));
 bb_pe.native_input_count--;
 for(unsigned i=0;i<bb_pe.native_input_count;i++)bb_pe.native_inputs[i]=bb_pe.native_inputs[i+1u];
}
/* Return zero for older/non-native pattern owners, which retain their own input path. */
static int bb_pe_native_step(unsigned key,unsigned accent){
 unsigned m=bb_face.machine,step=bb_face.bank*16u+key;uint32_t source[2];
 bb_section_status section;
 if(!bb_audio_native_pattern_identity(&bb_sound,m,source)||
    !bb_audio_section_status_read(&bb_sound,&section)||section.playing_section)return 0;
 if(step>=bb_face.requested.drums[m].length)return 1;
 if(bb_pe.native_input_count==BB_PE_NATIVE_INPUTS){bb_say(BB_TX(TX_STEP_REFUSED));return 1;}
 bb_native_step_input *input=&bb_pe.native_inputs[bb_pe.native_input_count++];
 input->source[0]=source[0];input->source[1]=source[1];
 input->machine=(uint8_t)m;input->voice=bb_face.voice;
 input->step=(uint8_t)step;input->accent=(uint8_t)accent;
 return 1;
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
    (void)bb_pe_publish(m,v,&bb_pe.before,&bb_pe.work,1,
        first?BB_TX(TX_EDIT_COPY_STEP_QUEUED):BB_TX(TX_STEP_QUEUED),BB_TX(TX_STEP_APPLIED_REC_EDIT_UNDO));
}
/* REC+EDIT on PLAY/STEPS (BB_NAV_A_UNDO): newest edit or kept take first. */
static void bb_pe_undo_last(void){
    bb_nav_ratchet_cancel();
    if(bb_pe_authoring_busy()||bb_pe.pending[0]||bb_pe.pending[1]){bb_say(BB_TX(TX_WAIT_FOR_EDIT_RETRY));return;}
    if(bb_arr_count_undo())return;
#if BB_WITH_PHRASE_NUDGE
    if(bb_pe_nudge_undo())return;
#endif
    if(bb_pe.edit.count&&bb_pe.edit.history[bb_pe.edit.count-1u].kind==BB_PE_ENTRY_NATIVE_STEP){
        const bb_pe_entry *v=&bb_pe.edit.history[bb_pe.edit.count-1u];
        uint32_t source[2]={v->native.source_ticket,v->native.source_epoch};
        if(bb_audio_native_step_submit(&bb_sound,v->machine,v->native.voice,v->native.step,0,source,
              v->native.before_count,v->native.after_count,&bb_pe.count.ticket)){
            bb_pe.count.kind=BB_PE_ENTRY_NATIVE_STEP;bb_pe.count.undo=1;
        }else bb_say(BB_TX(TX_UNDO_REFUSED_PATTERN_KEPT));
        return;
    }
    if(bb_pe.edit.count&&bb_pe.edit.history[bb_pe.edit.count-1u].kind==BB_PE_ENTRY_NATIVE_COUNT){
        const bb_pe_entry *v=&bb_pe.edit.history[bb_pe.edit.count-1u];uint32_t ticket;
        if(!bb_audio_native_ratchet_view(&bb_sound,v->machine,v->native.voice,v->native.step,&bb_pe.count.native)||
           bb_pe.count.native.source_ticket!=v->native.source_ticket||
           bb_pe.count.native.epoch!=v->native.source_epoch||
           bb_ratchet_bits_get(bb_pe.count.native.packed,v->native.voice)!=v->native.after_count||
           !bb_audio_native_ratchet_submit(&bb_sound,&bb_pe.count.native,v->native.before_count,&ticket)){
            bb_say(BB_TX(TX_UNDO_REFUSED_PATTERN_KEPT));return;
        }
        bb_pe.count.ticket=ticket;bb_pe.count.machine=v->machine;bb_pe.count.kind=BB_PE_ENTRY_NATIVE_COUNT;bb_pe.count.undo=1;
        bb_pe.count.before_count=v->native.before_count;bb_pe.count.after_count=v->native.after_count;
        bb_say(BB_TX(TX_RATCHET_UNDO_QUEUED));return;
    }
    unsigned m;
#if BB_WITH_PHRASE_NUDGE
    if(bb_pe_undo_state(&bb_pe.edit,&m,&bb_pe.work,&bb_pe.work_nudges)!=BB_PE_OK){bb_say(BB_TX(TX_NOTHING_TO_UNDO));return;}
#else
    if(bb_pe_undo(&bb_pe.edit,&m,&bb_pe.work)!=BB_PE_OK){bb_say(BB_TX(TX_NOTHING_TO_UNDO));return;}
#endif
    if(m<BB_RICH_DRUMS)bb_pe.backup[m].undo.phrase=bb_pe.work; /* restored if the owner refuses the undo */
#if BB_WITH_PHRASE_NUDGE
    if(m<BB_RICH_DRUMS)bb_pe.backup[m].undo.nudges=bb_pe.work_nudges;
#endif
    bb_phrase_view current={0};
#if BB_WITH_PHRASE_NUDGE
    int source_ok=m<BB_RICH_DRUMS&&bb_audio_phrase_view_read(&bb_sound,m,&current)&&current.valid;
#else
    int source_ok=m<BB_RICH_DRUMS;
#endif
    if(!source_ok||!bb_pe_publish(m,
#if BB_WITH_PHRASE_NUDGE
                       &current,
#else
                       NULL,
#endif
                       NULL,&bb_pe.work,0,BB_TX(TX_UNDO_QUEUED),
                      bb_pe_entries(&bb_pe.edit,m)?"UNDONE":BB_TX(TX_UNDONE_SOURCE_PATTERN))){
#if BB_WITH_PHRASE_NUDGE
        (void)bb_pe_push_state(&bb_pe.edit,m,&bb_pe.work,&bb_pe.work_nudges);
#else
        (void)bb_pe_push(&bb_pe.edit,m,&bb_pe.work);   /* nothing changed: keep it for the next try */
#endif
    }
}
/* QUEUED -> APPLIED, refusal, or another owner replacing the pattern (bb_gen_poll's rule). */
static void bb_pe_poll(void){
#if BB_WITH_PHRASE_NUDGE
    bb_pe_nudge_poll();
#endif
    bb_pe_count_poll();
    for(unsigned m=0;m<BB_RICH_DRUMS;m++){
        bb_audio_phrase_status st;
        if(!bb_audio_phrase_status_read(&bb_sound,m,&st))continue;
        if(!st.valid){uint32_t identity[2]={0};int valid=bb_audio_native_pattern_identity(&bb_sound,m,identity);
            bb_pe_filter_native(&bb_pe.edit,m,valid,identity[0],identity[1]);
            if(bb_pe_entries(&bb_pe.edit,m)||bb_pe.pending[m]){
                bb_pe_forget_phrases(&bb_pe.edit,m);bb_pe.pending[m]=0;bb_pe.kind[m]=0;
            }
            bb_rich_draft_on[m]=0;bb_pe.seen[m]=0;continue;}
        bb_pe_filter_native(&bb_pe.edit,m,0,0,0);
        if(bb_pe.pending[m]){
            uint32_t t=bb_pe.ticket[m];
            if(st.state.applied==t){
                bb_pe.pending[m]=0;bb_pe.has_evicted[m]=0;bb_pe.seen[m]=t;bb_rich_draft_on[m]=0;bb_dirty=1;
                if(bb_pe.kind[m]==BB_PE_KIND_RETRO){
#if BB_WITH_RETRO_TAKE
                    bb_phrase_view view={0};
                    int committed=bb_audio_phrase_view_read(&bb_sound,m,&view)&&view.valid&&
                        bb_pe.retro_revision!=UINT32_MAX&&view.edits==bb_pe.retro_revision+1u&&
                        !memcmp(&view.phrase,&bb_pe.work,sizeof view.phrase)&&
#if BB_WITH_PHRASE_NUDGE
                        !memcmp(&view.nudges,&bb_pe.work_nudges,sizeof view.nudges)&&
                        bb_pe_push_state(&bb_pe.edit,m,&bb_pe.before,&bb_pe.backup[m].undo.nudges)==BB_PE_OK;
#else
                        bb_pe_push(&bb_pe.edit,m,&bb_pe.before)==BB_PE_OK;
#endif
                    bb_say(committed?BB_TX(TX_TAKE_APPLIED_REC_EDIT_UNDO):BB_TX(TX_UNDO_REFUSED_PATTERN_KEPT));
#endif
                }else if(bb_pe.applied[m])bb_say(bb_pe.applied[m]);
                bb_pe.kind[m]=0;
            }else if(st.state.seen==t&&st.state.result==BB_RICH_REJECTED){
                bb_pe.pending[m]=0;bb_rich_draft_on[m]=0;bb_dirty=1;
                if(bb_pe.kind[m]==BB_PE_KIND_EDIT){
                    bb_pe_drop_newest(&bb_pe.edit,m);                                       /* only this layer's */
                    if(bb_pe.has_evicted[m]&&bb_pe.edit.count<BB_PE_HISTORY){              /* the oldest it pushed out */
                        for(unsigned i=bb_pe.edit.count;i;i--)bb_pe.edit.history[i]=bb_pe.edit.history[i-1u];
                        bb_pe.edit.history[0]=bb_pe.backup[m].evicted;bb_pe.edit.count++;
                    }
                }
                else if(bb_pe.kind[m]==BB_PE_KIND_UNDO){
#if BB_WITH_PHRASE_NUDGE
                    (void)bb_pe_push_state(&bb_pe.edit,m,&bb_pe.backup[m].undo.phrase,
                                           &bb_pe.backup[m].undo.nudges);
#else
                    (void)bb_pe_push(&bb_pe.edit,m,&bb_pe.backup[m].undo);
#endif
                }
                bb_pe.kind[m]=0;bb_pe.has_evicted[m]=0;
                bb_say(BB_TX(TX_EDIT_REFUSED_PATTERN_KEPT));
            }else if(st.state.seen&&st.state.seen!=t&&(int32_t)(st.state.seen-t)>0){
                bb_pe.pending[m]=0;bb_rich_draft_on[m]=0;bb_pe_forget_standalone(&bb_pe.edit,m);bb_dirty=1;   /* superseded */
                bb_pe.kind[m]=0;
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
static int bb_pe_authoring_busy(void){return 0;}
static int bb_pe_count_request(unsigned m,unsigned v,unsigned s,unsigned c){return bb_audio_ratchet_request(&bb_sound,m,v,s,c);}
#if BB_WITH_STEP_CONDITIONS
static int bb_pe_condition_request(unsigned m,unsigned v,unsigned s,unsigned c){(void)m;(void)v;(void)s;(void)c;return 0;}
#endif
static void bb_pe_reset(void){}
static void bb_pe_step(unsigned key,unsigned accent){(void)key;(void)accent;bb_say("SOURCE PATTERN VIEW");}
static void bb_pe_undo_last(void){bb_say(BB_TX(TX_NOTHING_TO_UNDO));}
static void bb_pe_poll(void){}
#endif
