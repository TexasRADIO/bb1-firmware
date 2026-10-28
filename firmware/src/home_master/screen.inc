/* SPDX-License-Identifier: GPL-3.0-only
 * Native three-row HOME candidate. Uses the existing Armadillo backdrop.
 * Call only from main-loop drawing after hm_pattern_rows; never the audio IRQ.
 * This renderer does not replace the existing arrangement browser. */
static void hm_draw_row(unsigned row, int y, const hm_pattern_row *view,
                        unsigned level, unsigned muted, unsigned enabled) {
    static const char *const names[] = {
#if defined(BB_TWO_DRUM_HOST_EXPERIMENT)
        "BIG BEAT / KIT A", "+ SECOND KIT", "DILLO"
#else
        "BIG BEAT", "DILLO", "TELEGRAM"
#endif
    };
    char text[128];
    uint16_t accent = view->focused ? BB_SPARK_EMBER : BB_DIM;
    /* Keep the character visible at the right while giving labels a stable
     * dark field. A focused frame also makes the selected instrument clear
     * without relying on animation or a dense instruction block. */
    cv_rect(4,y,130,43,BB_BG);
    if(view->focused){
        cv_rect(4,y,232,1,accent);cv_rect(4,y+42,232,1,accent);
        cv_rect(235,y,1,43,accent);
    }
    cv_rect(5, y, 2, 45, accent);
    cv_text(11, y, &AF_S, names[row], accent);
    if(!enabled)cv_text(137,y,&AF_S,"OFF",BB_DIM);
    else if(muted)cv_text(137,y,&AF_S,"MUTE",BB_SPARK_EMBER);
    bb_level_number(197, y, level, &AF_S, BB_TEXT);
#if defined(BB_TWO_DRUM_HOST_EXPERIMENT)
    /* A disabled second kit is not playing its retained phrase. Show the
     * action that makes the layer real, unless a request is still pending. */
    if(row==1 && !enabled && !view->pending) {
        cv_text(11,y+15,&AF_S,"ADD SECOND KIT",BB_TEXT);
        cv_text(11,y+30,&AF_S,"WHITE 2: ENABLE",BB_DIM);
        return;
    }
#endif
    const bb_player_selection *shown = &view->playing;
    if (shown->ticket && shown->choice.catalogue == &bb_factory_catalogue) {
        bb_library_text(bb_catalogue_find(&bb_factory_catalogue, shown->choice.bank,
                        shown->choice.group ? "song" : "name"), &AF_S, 120, text);
        cv_text(11, y+15, &AF_S, text, BB_TEXT);
    } else cv_text(11, y+15, &AF_S,
                   view->ready ? "CURRENT PHRASE" : "LIBRARY NOT READY", BB_DIM);
    if (view->pending) {
        cv_rect(4,y+29,182,14,BB_BG);
        cv_text(11, y+30, &AF_S, view->pending == 2 ? "QUEUED:" : "REQUEST:", accent);
        shown = &view->requested;
        if (shown->choice.catalogue == &bb_factory_catalogue) {
            bb_library_text(bb_catalogue_find(&bb_factory_catalogue, shown->choice.part,
                            "name"), &AF_S, 106, text);
            cv_text(77, y+30, &AF_S, text, BB_TEXT);
        }
    } else if (view->error == BB_PLAYER_CANCELLED) {   /* D8: a STOP-cancelled request is not a load failure */
        cv_text(11, y+30, &AF_S, bb_notice_text(BB_NOTICE_CANCELLED_BY_STOP), BB_DIM);
    } else if (view->error) {
        cv_text(11, y+30, &AF_S, "NOT LOADED", BB_SPARK_RED);
    } else if (shown->ticket && shown->choice.catalogue == &bb_factory_catalogue) {
        bb_library_text(bb_catalogue_find(&bb_factory_catalogue, shown->choice.part,
                        "name"), &AF_S, 120, text);
        cv_text(11, y+30, &AF_S, text, BB_DIM);
    }
}
static void hm_draw_screen(const hm_pattern_row rows[HM_INSTRUMENTS],
                           const hm_sound_row sounds[HM_INSTRUMENTS],
                           const uint8_t levels[HM_INSTRUMENTS],
                           unsigned bpm, unsigned muted, int pulse) {
    bb_backdrop(0,120,pulse);
    /* Fixed physical slots: disabled instruments never shift the other knobs.
     * Small transparent captions leave the animated backdrop visible. */
    const char *knobs[4]={
#if defined(BB_TWO_DRUM_HOST_EXPERIMENT)
        "1 A VOL", "2 B VOL", "3 D VOL", "4 BPM"
#else
        "1 DRUM", "2 DILLO", "3 TELE", "4 BPM"
#endif
    };
    (void)knobs;(void)bpm;
    for(unsigned k=0;k<4;k++) {
        /* Navigation v2.1: bb_nav's STUDIO roles (K4 = FX RETURN). */
        int available=k==3 || hm_available(HM_ACTIVE_MASK,k);
        char caption[16]={(char)('1'+k),' ',0},value[16];unsigned n=2;
        const char *label=bb_nav_knob_label(k);
        while(*label&&n<sizeof caption-1u)caption[n++]=*label++;
        caption[n]=0;
        cv_text(3+(int)k*60,0,&AF_S,available?caption:k==0?"1 OFF":k==1?"2 OFF":"3 OFF",available?BB_TEXT:BB_DIM);
        (void)bb_nav_knob_text(k,value);
        if(available)cv_text(3+(int)k*60,14,&AF_S,value,BB_TEXT);
    }
    (void)levels;
    hm_draw_row(0,29,&rows[0],levels[0],muted&(1u<<0),sounds[0].enabled);
    if(hm_available(HM_ACTIVE_MASK,1)) {
        /* Connect the two kit rows visually: they are layers of Big Beat,
         * not two peer instruments beside Dillo. */
        cv_rect(5,72,2,4,BB_SPARK_EMBER);
        hm_draw_row(1,74,&rows[1],levels[1],muted&(1u<<1),sounds[1].enabled);
    } else cv_text(9,74,&AF_S,"DILLO: NOT IN THIS BUILD",BB_DIM);
    cv_blit(0,0);
    bb_backdrop(120,120,pulse);
    if(hm_available(HM_ACTIVE_MASK,2))
        hm_draw_row(2,0,&rows[2],levels[2],muted&(1u<<2),sounds[2].enabled);
    else cv_text(9,0,&AF_S,"TELEGRAM: NOT IN THIS BUILD",BB_DIM);
    unsigned focus = 0;
    for (unsigned i=0;i<HM_INSTRUMENTS;i++) if (rows[i].focused && hm_available(HM_ACTIVE_MASK,i)) focus=i;
    char sound[128];
    const char *name = sounds[focus].name ? sounds[focus].name : "UNAVAILABLE";
    unsigned n=0;
    while (name[n] && n<117) {sound[n]=name[n];n++;}
    sound[n]=0;
    if (name[n] || text_w(&AF_S,sound)>122) {
        while(n && text_w(&AF_S,sound)>110) sound[--n]=0;
        sound[n++]='.';sound[n++]='.';sound[n]=0;
    }
    cv_text(9,48,&AF_S,"SOUND:",BB_DIM);
    cv_text(62,48,&AF_S,sound,BB_TEXT);
#if defined(BB_TWO_DRUM_HOST_EXPERIMENT)
    cv_text(9,68,&AF_S,focus<2?"PRESETS: KIT / ALGO: PATTERN":"PRESETS: SOUND / ALGO: PHRASE",BB_TEXT);
    cv_text(9,85,&AF_S,"SELECT: KIT A / KIT B / DILLO",BB_TEXT);
    {
        const char *section=bb_nav_section_hint();   /* sections 12+ (v2.1) */
        cv_text(9,102,&AF_S,bb_hint_until?bb_hint:section?section:"EDIT: OPEN / HOLD HOME: HELP",bb_hint_until?BB_TEXT:BB_DIM);
    }
#else
    cv_text(9,60,&AF_S,"PRESETS: SOUND",BB_DIM);
    cv_text(9,75,&AF_S,"ALGO: PATTERN / BLACK: PART",BB_DIM);
    cv_text(9,90,&AF_S,BB_WITH_TELEGRAM?"WHITE 1-3: MUTE":"WHITE 1-2: MUTE",BB_DIM);
    cv_text(9,105,&AF_S,BB_WITH_TELEGRAM?"K1-3 MIX / K4 TEMPO":"K1-2 MIX / K3 OFF / K4 BPM",BB_TEXT);
    #endif
    cv_blit(0,120);
}
