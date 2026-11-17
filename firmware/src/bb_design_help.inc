/* Original Big Beat foreground adapter. Included after native send helpers. */
static uint8_t bb_design_home_layers;
static int bb_design_help_input(uint32_t *pressed,uint32_t *notes) {
    unsigned visible=bb_help_visible;
    if(!bb_design_help.home.down && (fm1_in.buttons&(1u<<panel.btn[B_HOME])))
        bb_design_home_layers=!!(fm1_in.buttons&(1u<<panel.btn[B_REC]));
    bb_help_route r=bb_help_filter(&bb_design_help,fm1_in.buttons,fm1_in.notes,
        *pressed,*notes,1u<<panel.btn[B_HOME],1u<<panel.btn[B_PLAY],fm1_ms,
        HOLD_MS[settings_hold]);
    *pressed=r.pressed;*notes=r.note_edges;bb_keys_edges=*notes;
    bb_keys_blocked=bb_help_note_block(bb_keys_blocked,fm1_in.notes,
        bb_keys_sent[0]|bb_keys_sent[1],r.consume_encoders);
    bb_help_visible=bb_design_help.home.help;
    if(visible!=bb_help_visible)bb_dirty=1;
    if(r.consume_encoders) {
        for(unsigned role=0;role<7;role++)(void)bb_read_encoder(role);
        /* Cancel pending FX tap without changing the current page. */
        bb_aux_cancel(0);
        if(*pressed&(1u<<panel.btn[B_PLAY]))
            bb_send(BB_CMD_TRANSPORT,!bb_face.requested.running);
        *pressed=0;*notes=0;bb_keys_edges=0;
    }
    if(r.home_event==BB_HOME_TAP) {
        /* HOME has one destination: the Studio play screen. EDIT opens the
         * focused instrument; repeated HOME must never toggle into an editor. */
        bb_sound_pick_page=bb_sound_pick_return=0;an_reset(&bb_aux_return);
#if BB_WITH_SESSION
        bb_scene_page=0;
#endif
#if BB_WITH_EVOLVE
        bb_evo_page=0;
#endif
        if(hm_enabled && !bb_design_home_layers) {
            hm_enter();hm_focus_page();bb_aux_apply(HM_MASTER_PAGE);
            bb_say("STUDIO / EDIT: OPEN INSTRUMENT");
        } else {
            bb_face.machine=0;bb_face.requested.page=BB_DRUMS;
            bb_aux_apply(1u<<8);bb_say("BIG BEAT / FOUR GROUP LEVELS");
        }
        bb_scope_enabled=0;
    }
    /* Explicit focused instrument entrance from the ensemble, without the
     * legacy EDIT mute action also firing in the same scan. */
    if(!r.consume_encoders && (HM_VISIBLE || bb_mix_page==BB_MIX_BAND_PAGE) &&
       !(fm1_in.buttons&(1u<<panel.btn[B_REC])) &&
       (*pressed&(1u<<panel.btn[B_EDIT])) &&
       !(*pressed&(1u<<panel.btn[B_PLAY]))) {
        if(HM_VISIBLE)hm_focus_page();
        bb_aux_cancel(1);bb_aux_apply(1u<<8);
        if(bb_face.requested.page!=BB_DRUMS)
            bb_keys_page[bb_face.requested.page==BB_TELEGRAM?1:0]=1;
        for(unsigned role=0;role<7;role++)(void)bb_read_encoder(role);
        *pressed=*notes=bb_keys_edges=0;
        bb_say("INSTRUMENT / HOME: ENSEMBLE");return 1;
    }
    return r.consume_encoders;
}
