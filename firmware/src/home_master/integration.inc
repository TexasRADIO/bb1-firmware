/* SPDX-License-Identifier: GPL-3.0-only */
static void hm_reset(void) {
    hm_focus.selected=HM_DRUMS;
}
static void hm_enter(void) {
#if defined(BB_TWO_DRUM_HOST_EXPERIMENT)
    unsigned row=hm_focus.selected;
    if(st_from_editor(bb_face.requested.page,
        bb_face.requested.page==BB_DRUMS?bb_face.machine:0,&row))
        hm_focus.selected=(uint8_t)row;
#else
    hm_focus.selected=bb_face.requested.page==BB_DILLO?HM_DILLO:
        bb_face.requested.page==BB_TELEGRAM?HM_TELEGRAM:HM_DRUMS;
#endif
    hm_intent ignored;
    (void)hm_turn_available(&hm_focus,HM_SELECT,0,HM_ACTIVE_MASK,&ignored);
}
static void hm_focus_page(void) {
    hm_intent ignored;
    (void)hm_turn_available(&hm_focus,HM_SELECT,0,HM_ACTIVE_MASK,&ignored);
#if defined(BB_TWO_DRUM_HOST_EXPERIMENT)
    st_route route;
    if(st_resolve(hm_focus.selected,&route)) {
        bb_face.requested.page=route.page;
        if(route.page==BB_DRUMS)bb_face.machine=route.target;
    }
#else
    const bb_page pages[]={BB_DRUMS,BB_DILLO,BB_TELEGRAM};
    if(hm_focus.selected<HM_INSTRUMENTS)bb_face.requested.page=pages[hm_focus.selected];
#endif
}
static unsigned hm_mutes(void) {
#if defined(BB_TWO_DRUM_HOST_EXPERIMENT)
    return (!!bb_sound.core.drums[0].muted || !bb_sound.core.drums[0].enabled) |
           ((!!bb_sound.core.drums[1].muted || !bb_sound.core.drums[1].enabled)<<1) |
           ((!!bb_sound.core.synths[0].muted)<<2);
#else
    return bb_sound.core.band_muted;
#endif
}
static void hm_native_draw(int pulse) {
    hm_pattern_row rows[HM_INSTRUMENTS];
    hm_sound_row sounds[HM_INSTRUMENTS];
    if(!hm_pattern_rows(&hm_focus,bb_drum_library,bb_library,bb_face.machine,&bb_sound,rows) ||
       !hm_sound_rows_with_catalogue(&bb_sound,bb_face.machine,sounds,
                                    bb_sample_ui_names,bb_sample_ui_count))return;
    uintptr_t guard=bb_target_enter(0);
    unsigned muted=hm_mutes();
#if defined(BB_TWO_DRUM_HOST_EXPERIMENT)
    uint8_t levels[3]={bb_sound.core.drums[0].level,bb_sound.core.drums[1].level,
                       bb_sound.core.band_level[BB_MIX_DILLO]};
#endif
    bb_target_leave(0,guard);
    hm_draw_screen(rows,sounds,
#if defined(BB_TWO_DRUM_HOST_EXPERIMENT)
                   levels,
#else
                   bb_face.requested.band_level,
#endif
                   bb_face.requested.bpm,muted,pulse);
}
static void hm_native_leds(uint8_t *lit,uint8_t *dim,uint8_t *mid) {
    uint8_t breath[FM1_NCOL]={0};
    const unsigned actions[]={B_HOME,B_SEQ,B_PLAY,B_REC,B_FX,B_GLO,B_SAVE,B_ENV,B_EDIT};
    for(unsigned i=0;i<sizeof actions/sizeof *actions;i++)led_put(dim,panel.btn[actions[i]],1);
    led_put(lit,panel.btn[B_HOME],1);
    bb_player *p=hm_ready_player(&hm_focus,bb_drum_library,bb_library,bb_face.machine);
    uintptr_t mute_guard=bb_target_enter(0);
    unsigned muted=hm_mutes();
    bb_target_leave(0,mute_guard);
    for(unsigned key=0;key<27;key++) {
        int row=bb_panel_white_step(key);
        if(row>=0 && hm_available(HM_ACTIVE_MASK,(unsigned)row)) {
            led_put(dim,14u+key,1);
            if(!(muted&(1u<<row)))led_put(lit,14u+key,1);
        }
    }
    if(p) {
        (void)bb_player_sync(p,&bb_sound);
        for(unsigned key=0;key<27;key++) {
            int part=bb_panel_black_index(key);
            if(part<0 || part>=8 ||
               bb_browser_part_cell(&p->view,(unsigned)part)==BB_CATALOGUE_NONE)continue;
            unsigned index=p->view.part_page*BB_BROWSER_PART_KEYS+(unsigned)part;
            if(bb_player_pending(p) && p->request.choice.group==p->view.group &&
               p->request.choice.bank_index==p->view.bank_index && p->request.choice.part_index==index)
                led_put(breath,14u+key,1);
            else if(p->playing.ticket && p->playing.choice.group==p->view.group &&
                    p->playing.choice.bank_index==p->view.bank_index && p->playing.choice.part_index==index)
                led_put(lit,14u+key,1);
            else led_put(dim,14u+key,1);
        }
    }
    if(bb_face.requested.running)lit[LED_PLAY_GREEN>>3]|=(uint8_t)(1u<<(LED_PLAY_GREEN&7));
    for(unsigned i=0;i<FM1_NCOL;i++){
        fm1_led[i]=lit[i];fm1_led_dim[i]=dim[i];fm1_led_mid[i]=mid[i];fm1_led_breath[i]=breath[i];
    }
}
