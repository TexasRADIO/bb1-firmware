/* SPDX-License-Identifier: GPL-3.0-only
 * Isolated native main-loop adapter. Hardware input policy is experimental. */
static int bb_sound_pick_kit_request(void *ctx,unsigned layer,unsigned family,unsigned recipe) {
    (void)ctx;return bb_kit_switch_request(layer,family,recipe);
}
static int bb_sound_pick_kit_busy(void *ctx){(void)ctx;return bb_kit_switch_busy();}
static const bb_sound_picker_kits bb_sound_pick_kits={0,bb_sound_pick_kit_request,bb_sound_pick_kit_busy};
static int bb_sound_pick_open(bb_page page,unsigned layer) {
    return bb_sound_picker_open(&bb_sound_pick,&bb_sound,page,layer,
                                bb_sample_ui_names,bb_sample_ui_count);
}
static int bb_sound_pick_visible(void) {
    return bb_sound_pick_page&&!bb_mix_page&&!bb_diag_page&&!bb_send_page&&!bb_effect_page&&
        !bb_scene_page&&!bb_evo_page&&bb_face.requested.page==bb_sound_pick.page&&
        (bb_sound_pick.page!=BB_DRUMS||bb_face.machine==bb_sound_pick.machine);
}
/* A latched backend failure is not a kit rejection or cancellation. Keep
 * request ownership intact; recovery must settle it before another load. */
static int bb_sound_pick_audio_failed(void) {
#if BB_OUTPUT_GUARD
    /* CPU0 audio IRQ writes this aligned uint32_t latch; the main loop
     * reads only this scalar, not the associated multi-field fault record.
     * Force a fresh read without changing IRQ state or touching CPU1. This
     * is visibility of a monotonic latch, not permission to retry a job. */
    return !bb_target_audio_operable();
#else
    return 0;
#endif
}
static void bb_sound_pick_close(void) {
    bb_sound_pick_page=bb_sound_pick_return=0;bb_dirty=1;
    bb_keys_blocked|=fm1_in.notes;bb_keys_edges=bb_keys_taps=0;
}
/* Only these fields affect the chooser's loaded label and OFF/MUTE/EDIT.
 * Request completion is independently dirtied by the controller status below.
 * Epoch/queue/running changes are not displayed here and need no cache copy. */
static uint16_t bb_sound_pick_display_state(const bb_sound_picker_view *view) {
    return (uint16_t)((unsigned)(view->active_index+1) |
        (!!view->edited<<8) | (!!view->enabled<<9) | (!!view->muted<<10));
}
static void bb_sound_pick_poll(void) {
    static unsigned prior_failed;
    unsigned failed=(unsigned)bb_sound_pick_audio_failed();
    if(failed!=prior_failed){prior_failed=failed;bb_dirty=1;}
    /* D1 (reliability r1): the command plane now keeps draining during a
     * fault, so a pending request settles truthfully (a refused sample load
     * completes its serial; a dropped native/Dillo load leaves the queue idle
     * and unchanged). Settle it instead of leaving SOUND REQUEST STILL
     * PENDING blocking navigation until reboot. */
    if(failed && !bb_sound_pick.pending)return;
    if(!bb_sound_pick.ready||(!bb_sound_pick_page&&!bb_sound_pick_return&&!bb_sound_pick.pending))return;
    unsigned was=bb_sound_pick.pending,status=bb_sound_pick.status;
    bb_sound_picker_view view;
    if(!bb_sound_picker_poll_view(&bb_sound_pick,&bb_sound,&bb_sound_pick_kits,&view))return;
    {
        uint16_t display=bb_sound_pick_display_state(&view);
#if BB_SAMPLE_HOST_INTEGRATION
        static uint16_t sample_compatible;
        if(bb_sound_pick.sample_count&&view.sample_compatible!=sample_compatible)bb_dirty=1;
        sample_compatible=view.sample_compatible;
#endif
        if(display!=bb_sound_pick_view)bb_dirty=1;
        bb_sound_pick_view=display;
        if(was&&!bb_sound_pick.pending&&bb_sound_pick.page!=BB_DRUMS&&view.queue_idle) {
            /* Queue acceptance never advances the UI's patch identity. Only
             * after consumption reconcile its mirror with applied values. */
            unsigned synth=bb_sound_pick.page==BB_TELEGRAM;
            uintptr_t token=bb_target_enter(0);
            if(synth)bb_face.requested.telegram=bb_sound.core.telegram;
            else bb_face.requested.dillo=bb_sound.core.dillo;
            bb_target_leave(0,token);
            if(view.active_index>=0){bb_patch_index[synth]=(uint8_t)view.active_index;bb_patch_from_scene[synth]=0;}
            bb_patch_edited[synth]=view.edited;
        }
    }
    if(status!=bb_sound_pick.status)bb_dirty=1;
    if(failed && was && !bb_sound_pick.pending && bb_sound_pick.status!=BB_SP_APPLIED)
        bb_say(bb_notice_text(BB_NOTICE_REQUEST_FAULT));
}
static const char *bb_sound_pick_refusal_message(void) {
    /* Sample requests do not use the native-kit selector. Never display its
     * stale success message for a rejected sample/pattern combination. */
    if(bb_sound_pick.sample_count) {
        bb_sound_picker_view view;
        if(bb_sound_picker_observe(&bb_sound_pick,&bb_sound,&view) &&
           !bb_sound_picker_available(&bb_sound_pick,&view,bb_sound_pick.cursor))
            return "KIT / PATTERN MISMATCH";
        return "SOUND REQUEST REFUSED";
    }
    return bb_sound_pick.page==BB_DRUMS&&bb_kit_switch_message?
        bb_kit_switch_message:"SOUND REQUEST REFUSED";
}
/* Returns 1 when the choice was taken (requested, already loaded, or a
 * sample kit intent); navigation then continues to PATTERN. */
static int bb_sound_pick_commit_result(void) {
    if(bb_sound_pick_audio_failed()) {
        bb_say("AUDIO FAULT / LOAD BLOCKED");bb_dirty=1;return 0;
    }
#if BB_WITH_BROWSER && BB_SAMPLE_HOST_INTEGRATION && defined(BB_SAMPLE_HYBRID_TRIAL)
    if(bb_sound_pick.page==BB_DRUMS && bb_sound_pick.sample_count) {
        /* Choosing a kit must not be vetoed by the old phrase or enqueue a
         * separate sound change. The selected phrase is confirmed next. */
        if(bb_sound_pick.pending || !bb_sound_pick.ready ||
           bb_sound_pick.machine>=BB_MACHINES ||
           bb_sound_pick.cursor>bb_sound_pick.sample_count ||
           !bb_drum_library[bb_sound_pick.machine].ready) {
            bb_say("KIT NOT READY");return 0;
        }
        unsigned kit=bb_sound_pick.cursor?bb_sound_pick.cursor-1u:BB_SAMPLE_808_ID;
#if BB_WITH_RICH_DRUM_BROWSER
        if(!bb_player_preview_align_kit(&bb_drum_library[bb_sound_pick.machine],&bb_sound,kit)){
            bb_say("NO PATTERNS FOR THIS KIT");return 0;
        }
#endif
        bb_kit_choice *intent=&bb_kit_intent[bb_sound_pick.machine];
        intent->id=(uint8_t)kit;
        intent->cursor=bb_sound_pick.cursor;intent->valid=1;
        bb_sound_pick_close();bb_library_page=1;
        bb_say("CHOOSE PATTERN / EDIT: CHOOSE");return 1;
    }
#endif
    int result=bb_sound_picker_commit(&bb_sound_pick,&bb_sound,&bb_sound_pick_kits);
    if(result==BB_SP_OK)bb_say("SOUND REQUESTED / WAIT");
    else if(result==BB_SP_UNCHANGED)bb_say("SOUND ALREADY LOADED");
    else if(result==BB_SP_BUSY)bb_say("BUSY / SELECT AGAIN");
    else bb_say(bb_sound_pick_refusal_message());
    bb_dirty=1;
    return result==BB_SP_OK||result==BB_SP_UNCHANGED;
}
static void bb_sound_pick_commit(void){(void)bb_sound_pick_commit_result();}
static int bb_sound_pick_input(uint32_t pressed,uint32_t *notes,int rec) {
    if(!bb_sound_picker_enabled)return 0;
    int32_t delta;
    if(!bb_sound_pick_page) {
        /* An explicit sound-editor action outranks a simultaneous PRESETS
         * detent. The native ENV route drains stale turns and held notes. */
        if(pressed&(1u<<panel.btn[B_ENV]))return 0;
        /* PRESETS keeps its sound role while browsing patterns. Auxiliary
         * editors retain their explicit controls; REC preserves family access. */
        int pattern_face=bb_library_page&&!bb_mix_page&&!bb_diag_page&&
            !bb_send_page&&!bb_effect_page&&!bb_scene_page&&!bb_evo_page;
        int kit_face=bb_face.requested.page==BB_DRUMS&&bb_kit_page&&
            !bb_drum_sound_page&&!bb_mix_page&&!bb_diag_page&&!bb_send_page&&
            !bb_effect_page&&!bb_scene_page&&!bb_evo_page;
        if(rec||(!bb_play_face_visible()&&!pattern_face&&!kit_face))return 0;
        delta=bb_read_encoder(EN_PRESET);if(!delta)return 0;
        /* An explicit transport action wins over opening a new chooser. */
        if(pressed&(1u<<panel.btn[B_PLAY])) {
            for(unsigned k=0;k<7;k++)(void)bb_read_encoder(k);
            bb_keys_blocked|=fm1_in.notes;bb_keys_edges=0;*notes=0;
            bb_send(BB_CMD_TRANSPORT,!bb_face.requested.running);return 1;
        }
        /* The audio fault outranks a stale chooser request. Keep the queued
         * operation intact for recovery, but do not tell the player to keep
         * waiting when its consumer has stopped. Transport remains usable. */
        if(bb_sound_pick_audio_failed()) {
            for(unsigned k=0;k<7;k++)(void)bb_read_encoder(k);
            bb_keys_blocked|=fm1_in.notes;bb_keys_edges=bb_keys_taps=0;*notes=0;
            bb_say("AUDIO FAULT / LOAD BLOCKED");return 1;
        }
        if(bb_sound_pick.pending){
            /* A refused navigation gesture must not fall through the outer
             * held-key synchronizer as a new musical note. Keep the original
             * sound request intact and require release before playing again. */
            for(unsigned k=0;k<7;k++)(void)bb_read_encoder(k);
            bb_keys_blocked|=fm1_in.notes;bb_keys_edges=bb_keys_taps=0;*notes=0;
            bb_say("SOUND REQUEST STILL PENDING");return 1;
        }
        if(!bb_sound_pick_open(bb_face.requested.page,bb_face.machine)) {
            bb_say("SOUND LIST NOT READY");return 1;
        }
        bb_sound_pick_page=1;bb_sound_pick_return=0;
        bb_library_page=0;bb_tone_page=0;
        (void)bb_sound_picker_move(&bb_sound_pick,delta);
        for(unsigned k=0;k<7;k++)(void)bb_read_encoder(k);
        bb_keys_blocked|=fm1_in.notes;bb_keys_edges=bb_keys_taps=0;*notes=0;
        bb_dirty=1;bb_sound_pick_poll();return 1;
    }
    if(!bb_sound_pick_visible())return 0;
    if(pressed&(1u<<panel.btn[B_PLAY])) {
        for(unsigned k=0;k<7;k++)(void)bb_read_encoder(k);
        bb_keys_blocked|=fm1_in.notes;bb_keys_edges=0;*notes=0;
        bb_send(BB_CMD_TRANSPORT,!bb_face.requested.running);return 1;
    }
    const uint32_t exits=(1u<<panel.btn[B_ENV])|(1u<<panel.btn[B_SEQ])|
        (1u<<panel.btn[B_ARP])|(1u<<panel.btn[B_EDIT])|(1u<<panel.btn[B_LFO]);
    if(pressed&exits) {
        bb_sound_pick_close();*notes=0;
        for(unsigned k=0;k<7;k++)(void)bb_read_encoder(k);
        return 0; /* The existing action, including waveform/glide elsewhere, remains native. */
    }
    if((delta=bb_read_encoder(EN_SELECT))) {
        if(rec)bb_send(BB_CMD_TEMPO,(unsigned)bb_limit((int)bb_face.requested.bpm+bb_limit(delta,-270,270),30,300));
        else {bb_sound_pick_close();*notes=0;bb_select_instrument(delta);return 1;}
    }
    if((delta=bb_read_encoder(EN_ALGO))) {
        if(rec&&bb_sound_pick.page==BB_DRUMS) {
            if(bb_sound_pick.pending){bb_say("WAIT FOR KIT CHANGE");return 1;}
            bb_select_layer(delta);
            if(!bb_sound_pick_open(BB_DRUMS,bb_face.machine)) {
                bb_sound_pick_close();bb_say("SOUND LIST NOT READY");*notes=0;return 1;
            }
            bb_sound_pick_poll();bb_keys_blocked|=fm1_in.notes;bb_keys_edges=0;*notes=0;return 1;
        }
        if(!rec) {
            bb_sound_pick_close();*notes=0;bb_library_page=1;
            bb_player *player=bb_library_current();
            if(bb_pattern_confirm_first){(void)bb_library_preview_move(player,delta);bb_dirty=1;}
            else bb_library_result(bb_player_move(player,&bb_sound,delta));
            (void)bb_read_encoder(EN_PRESET);
            for(unsigned k=0;k<4;k++)(void)bb_read_encoder(EN_K1+k);
            return 1;
        }
    }
    int moved=0;
    if((delta=bb_read_encoder(EN_PRESET))) {
        (void)bb_sound_picker_move(&bb_sound_pick,delta);bb_dirty=1;moved=1;
    }
    if((delta=bb_read_encoder(EN_K1))) {
        if(bb_sound_pick.page==BB_DRUMS)
            bb_send(BB_CMD_DRUM_LEVEL,(unsigned)bb_limit((int)bb_face.requested.drums[bb_face.machine].level+bb_limit(delta,-127,127),0,127));
        else {unsigned channel=bb_sound_pick.page==BB_DILLO?BB_MIX_DILLO:BB_MIX_TELEGRAM;
            bb_send_machine(channel,BB_CMD_BAND_LEVEL,(unsigned)bb_limit((int)bb_face.requested.band_level[channel]+bb_limit(delta,-127,127),0,127));}
    }
    for(unsigned k=1;k<4;k++)(void)bb_read_encoder(EN_K1+k); /* No hidden sound edits in this picker. */
    int chosen=0;
    for(unsigned k=0;k<27;k++)if((*notes&~bb_keys_blocked)&(1u<<k)) {
        int key=bb_panel_white_step(k);
        if(key>=0&&(unsigned)key<bb_sound_picker_items(&bb_sound_pick)) {
            (void)bb_sound_picker_key(&bb_sound_pick,(unsigned)key);chosen=1;break;
        }
    }
    if(chosen||(!rec&&!moved&&!*notes&&(pressed&(1u<<panel.btn[B_SCL]))))bb_sound_pick_commit();
    bb_keys_edges=0;*notes=0;return 1;
}
