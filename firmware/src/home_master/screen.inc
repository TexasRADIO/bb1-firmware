/* SPDX-License-Identifier: GPL-3.0-only
 * Native three-row HOME candidate — BB1 2.3 redesign.
 *
 * Changes from 2.2:
 *  - Song name drawn large at the top (AF_M), always visible.
 *  - Three instrument rows in the upper half: compact, colour-coded,
 *    with level and mute status on one line each.
 *  - Self-labelling knob strip shows what each physical knob does.
 *  - Key legend strip at the bottom of the lower half shows current
 *    white and black key assignments.
 *  - Armadillo fills the lower half as the visualiser backdrop.
 *
 * Call only from main-loop drawing after hm_pattern_rows; never the audio IRQ.
 * This renderer does not replace the existing arrangement browser. */

/* ── Key legend helper ───────────────────────────────────────────────
 * One line at the bottom of the lower strip. This appears on the HOME
 * page; other pages will need their own legend once the pattern is
 * confirmed. The legend sits at y=108 in local strip coordinates,
 * mapping to screen y=228. */
static void hm_key_legend(void) {
#if defined(BB_TWO_DRUM_HOST_EXPERIMENT)
    cv_text(9,108,&AF_S,"W1:A  W2:B  W3:D  BLK:SECTION",BB_DIM);
#else
    cv_text(9,108,&AF_S,"WHITE: MUTE  BLACK: SECTION",BB_DIM);
#endif
}

/* ── Compact instrument row ─────────────────────────────────────────
 * Each row is 18px tall instead of the 2.2 version's 43px, so all
 * three fit in the upper half alongside the song name and knob labels.
 * The accent colour identifies the instrument: orange for drums,
 * green for Dillo. A focused row gets a left-side highlight bar. */
static void hm_draw_row_compact(unsigned row, int y,
        const hm_pattern_row *view, unsigned level,
        unsigned muted, unsigned enabled) {
    static const char *const names[] = {
#if defined(BB_TWO_DRUM_HOST_EXPERIMENT)
        "A", "B", "D"
#else
        "DR", "DL", "TG"
#endif
    };
    static const uint16_t row_ink[] = {
#if defined(BB_TWO_DRUM_HOST_EXPERIMENT)
        BB_ORANGE, BB_ORANGE, BB_GREEN
#else
        BB_ORANGE, BB_GREEN, BB_TELEGRAM_INK
#endif
    };
    char text[128];
    uint16_t accent = row_ink[row < 3 ? row : 0];
    uint16_t label_ink = view->focused ? accent : BB_DIM;

    /* Left marker: 2px tall accent bar for focused row. */
    if(view->focused) {
        cv_rect(4,y,2,16,accent);
        cv_rect(4,y,232,1,accent);
        cv_rect(4,y+15,232,1,accent);
    }

    /* Row label (A/B/D). */
    cv_text(9,y+1,&AF_S,names[row],label_ink);

    /* Status: OFF / MUTE / level%. */
    if(!enabled) cv_text(22,y+1,&AF_S,"OFF",BB_DIM);
    else if(muted) cv_text(22,y+1,&AF_S,"MUTE",BB_SPARK_EMBER);
    else bb_level_number(22,y+1,level,&AF_S,BB_TEXT);

    /* Current song/pattern name, truncated to fit. */
    const bb_player_selection *shown = &view->playing;
    if(view->pending) shown = &view->requested;
    if(shown->ticket && shown->choice.catalogue == &bb_factory_catalogue) {
        bb_library_text(bb_catalogue_find(&bb_factory_catalogue, shown->choice.bank,
                        shown->choice.group ? "song" : "name"), &AF_S, 160, text);
        cv_text(62,y+1,&AF_S,text,view->focused ? BB_TEXT : BB_DIM);
    } else if(!view->ready) {
        cv_text(62,y+1,&AF_S,"NOT READY",BB_DIM);
    } else {
        cv_text(62,y+1,&AF_S,"CURRENT",BB_DIM);
    }

    /* Pending indicator. */
    if(view->pending)
        cv_rect(57,y+14,170,1,BB_SPARK_EMBER);
    else if(view->error)
        cv_rect(57,y+14,170,1,BB_SPARK_RED);
}

static void hm_draw_screen(const hm_pattern_row rows[HM_INSTRUMENTS],
                           const hm_sound_row sounds[HM_INSTRUMENTS],
                           const uint8_t levels[HM_INSTRUMENTS],
                           unsigned bpm, unsigned muted, int pulse) {
    /* ── Upper half: song identity and instrument status ──────────── */
    bb_backdrop(0,120,pulse);

    /* Song name — large, top of screen, always visible.
     * Use the focused row's current song for the big title. */
    {
        unsigned focus = 0;
        for(unsigned i=0;i<HM_INSTRUMENTS;i++)
            if(rows[i].focused && hm_available(HM_ACTIVE_MASK,i)) focus=i;
        char title[128];
        const bb_player_selection *shown = &rows[focus].playing;
        if(rows[focus].pending) shown = &rows[focus].requested;
        if(shown->ticket && shown->choice.catalogue == &bb_factory_catalogue) {
            bb_library_text(bb_catalogue_find(&bb_factory_catalogue, shown->choice.bank,
                            shown->choice.group ? "song" : "name"), &AF_M, 190, title);
        } else {
            unsigned n=0;
            const char *src = sounds[focus].name ? sounds[focus].name : "BIG BEAT";
            while(src[n] && n<126){title[n]=src[n];n++;}
            title[n]=0;
        }
        cv_text(9,2,&AF_M,title,BB_TEXT);
    }

    /* BPM and transport state — right side of title. */
    bb_number(191,2,bpm,&AF_M,BB_TEXT);
    cv_text(191,18,&AF_S,bb_face.requested.running ? "PLAY" : "STOP",
            bb_face.requested.running ? BB_GREEN : BB_DIM);

    /* Self-labelling knob strip.
     * Shows what each physical knob currently does. */
    {
        const char *knobs[4]={
#if defined(BB_TWO_DRUM_HOST_EXPERIMENT)
            "1 A VOL", "2 B VOL", "3 D VOL", "4 BPM"
#else
            "1 DRUM", "2 DILLO", "3 TELE", "4 BPM"
#endif
        };
        for(unsigned k=0;k<4;k++) {
            int available=k==3 || hm_available(HM_ACTIVE_MASK,k);
            const char *caption=available ? knobs[k] :
                k==0 ? "1 OFF" : k==1 ? "2 OFF" : "3 OFF";
            cv_text(3+(int)k*60,30,&AF_S,caption,available ? BB_TEXT : BB_DIM);
        }
    }

    /* Three instrument rows — compact, all visible at once. */
    hm_draw_row_compact(0, 46, &rows[0], levels[0],
                        muted&(1u<<0), sounds[0].enabled);
#if defined(BB_TWO_DRUM_HOST_EXPERIMENT)
    if(hm_available(HM_ACTIVE_MASK,1)) {
        /* Visual connector between the two kit rows. */
        cv_rect(5,62,2,2,BB_SPARK_EMBER);
        hm_draw_row_compact(1, 64, &rows[1], levels[1],
                            muted&(1u<<1), sounds[1].enabled);
    }
#else
    if(hm_available(HM_ACTIVE_MASK,1))
        hm_draw_row_compact(1, 64, &rows[1], levels[1],
                            muted&(1u<<1), sounds[1].enabled);
    else cv_text(9,64,&AF_S,"DILLO: NOT IN THIS BUILD",BB_DIM);
#endif
    if(hm_available(HM_ACTIVE_MASK,2))
        hm_draw_row_compact(2, 82, &rows[2], levels[2],
                            muted&(1u<<2), sounds[2].enabled);
#if !defined(BB_TWO_DRUM_HOST_EXPERIMENT)
    else cv_text(9,82,&AF_S,"TELEGRAM: NOT IN THIS BUILD",BB_DIM);
#endif

    /* Sound name — the focused instrument's current sound. */
    {
        unsigned focus = 0;
        for(unsigned i=0;i<HM_INSTRUMENTS;i++)
            if(rows[i].focused && hm_available(HM_ACTIVE_MASK,i)) focus=i;
        char sound[128];
        const char *name = sounds[focus].name ? sounds[focus].name : "UNAVAILABLE";
        unsigned n=0;
        while(name[n] && n<117){sound[n]=name[n];n++;}
        sound[n]=0;
        if(name[n] || text_w(&AF_S,sound)>122) {
            while(n && text_w(&AF_S,sound)>110) sound[--n]=0;
            sound[n++]='.';sound[n++]='.';sound[n]=0;
        }
        cv_text(9,100,&AF_S,"SOUND:",BB_DIM);
        cv_text(62,100,&AF_S,sound,BB_TEXT);
    }

    cv_blit(0,0);

    /* ── Lower half: armadillo visualiser + help + key legend ─────── */
    bb_backdrop(120,120,pulse);

#if defined(BB_TWO_DRUM_HOST_EXPERIMENT)
    /* Control hints — concise, leaving room for the armadillo. */
    cv_text(9,4,&AF_S,"SELECT: FOCUS ROW",BB_DIM);
    cv_text(9,18,&AF_S,"PRESETS: SOUND / ALGO: SONG",BB_TEXT);
    cv_text(9,32,&AF_S,"HOME: OPEN / HOLD: HELP",BB_DIM);

    /* Conductor status — show when all three instruments are cueing together. */
    {
        int all_same = 1;
        const bb_player_selection *ref = &rows[0].playing;
        for(unsigned i=1;i<ST_ROWS;i++) {
            if(!rows[i].ready) { all_same=0; break; }
            if(rows[i].playing.ticket != ref->ticket) { all_same=0; break; }
            if(rows[i].playing.ticket &&
               rows[i].playing.choice.bank != ref->choice.bank) { all_same=0; break; }
        }
        cv_text(9,50,&AF_S,all_same ? "ALL INSTRUMENTS ON SAME SONG" :
                "INSTRUMENTS ON DIFFERENT SONGS",
                all_same ? BB_GREEN : BB_SPARK_EMBER);
    }
#else
    cv_text(9,4,&AF_S,"PRESETS: SOUND",BB_DIM);
    cv_text(9,18,&AF_S,"ALGO: PATTERN / BLACK: PART",BB_DIM);
    cv_text(9,32,&AF_S,BB_WITH_TELEGRAM?"WHITE 1-3: MUTE":"WHITE 1-2: MUTE",BB_DIM);
    cv_text(9,50,&AF_S,BB_WITH_TELEGRAM?
            "K1-3 MIX / K4 TEMPO":"K1-2 MIX / K3 OFF / K4 BPM",BB_TEXT);
#endif

    /* Key legend strip — bottom of screen. */
    hm_key_legend();

    cv_blit(0,120);
}
