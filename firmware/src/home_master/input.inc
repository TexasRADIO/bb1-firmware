/* SPDX-License-Identifier: GPL-3.0-only
 * Native HOME event handler candidate. Main-loop only; caller invokes it
 * before instrument handlers when the master screen owns the controls.
 * Returns zero for navigation, one when this scan is fully consumed. */
#if defined(BB_TWO_DRUM_HOST_EXPERIMENT)
#define HM_ACTIVE_MASK 7u /* physical rows A, B, Dillo; not HM instrument IDs */
#else
#define HM_ACTIVE_MASK ((1u<<HM_DRUMS) | (BB_WITH_DILLO ? 1u<<HM_DILLO : 0u) | (BB_WITH_TELEGRAM ? 1u<<HM_TELEGRAM : 0u))
#endif
static hm_state hm_focus;
static int bb_home_sound_browse(bb_page page,unsigned layer,int delta);
static void hm_drain(void) {
    const unsigned knobs[]={EN_SELECT,EN_PRESET,EN_ALGO,EN_K1,EN_K2,EN_K3,EN_K4};
    for(unsigned i=0;i<7;i++)(void)bb_read_encoder(knobs[i]);
}
static int hm_native_input(uint32_t pressed,uint32_t *notes) {
    if(!notes)return 0;
    /* Physical transport wins over simultaneous notes and every knob. */
    if(pressed&(1u<<panel.btn[B_PLAY])) {
        hm_drain();bb_keys_blocked|=fm1_in.notes;bb_keys_edges=0;*notes=0;
        bb_send(BB_CMD_TRANSPORT,!bb_face.requested.running);return 1;
    }
    const unsigned exits[]={B_HOME,B_SEQ,B_FX,B_GLO,B_SAVE,B_ENV,B_EDIT,B_LFO,B_ARP,B_SCL};
    for(unsigned i=0;i<sizeof exits/sizeof *exits;i++)
        if(pressed&(1u<<panel.btn[exits[i]])) {
            hm_drain();bb_keys_blocked|=fm1_in.notes;bb_keys_edges=0;*notes=0;
            return 0;
        }
    if(fm1_in.buttons&(1u<<panel.btn[B_REC])) {
        int tempo=bb_read_encoder(EN_SELECT);
        hm_drain();bb_keys_blocked|=fm1_in.notes;bb_keys_edges=0;*notes=0;
        if(tempo)bb_send(BB_CMD_TEMPO,(unsigned)bb_limit(
            (int)bb_face.requested.bpm+bb_limit(tempo,-270,270),30,300));
        return 1;
    }
    hm_intent intent;
    int delta=bb_read_encoder(EN_SELECT);
    int selection_changed=!!delta;
    if(hm_turn_available(&hm_focus,HM_SELECT,delta,HM_ACTIVE_MASK,&intent)&&intent.action==HM_FOCUS)bb_dirty=1;
    delta=bb_read_encoder(EN_PRESET);
    if(delta) {
        if(bb_sound_pick_audio_failed())bb_say("AUDIO FAULT / LOAD BLOCKED");
        else if(bb_sound_pick.pending)bb_say("SOUND REQUEST STILL PENDING");
        else {
#if defined(BB_TWO_DRUM_HOST_EXPERIMENT)
            st_route route;
            if(st_resolve(hm_focus.selected,&route))
                (void)bb_home_sound_browse(route.page,route.target,delta);
            else bb_say("SOUND LIST NOT READY");
#else
            (void)bb_home_sound_browse((bb_page)hm_focus.selected,bb_face.machine,delta);
#endif
        }
        /* A sound-navigation gesture consumes this scan even when refused.
         * Never pass simultaneous parts, faders or held notes to the new page. */
        hm_drain();bb_keys_blocked|=fm1_in.notes;bb_keys_edges=0;*notes=0;
        bb_dirty=1;return 1;
    }
    delta=bb_read_encoder(EN_ALGO);
    if(delta) {
        selection_changed=1;
        bb_library_result(hm_pattern_move(&hm_focus,bb_drum_library,bb_library,
                                          bb_face.machine,&bb_sound,delta));
        bb_dirty=1;
    }
    const unsigned knobs[]={EN_K1,EN_K2,EN_K3,EN_K4};
    for(unsigned i=0;i<4;i++)if((delta=bb_read_encoder(knobs[i]))) {
        if(i<3 && !hm_available(HM_ACTIVE_MASK,i))continue;
#if defined(BB_TWO_DRUM_HOST_EXPERIMENT)
        if(i<2)bb_send_machine(i,BB_CMD_DRUM_LEVEL,(unsigned)bb_limit(
            (int)bb_face.requested.drums[i].level+bb_limit(delta,-127,127),0,127));
        else if(i==2)bb_send_machine(BB_MIX_DILLO,BB_CMD_BAND_LEVEL,(unsigned)bb_limit(
            (int)bb_face.requested.band_level[BB_MIX_DILLO]+bb_limit(delta,-127,127),0,127));
#else
        if(i<3)bb_send_machine(i,BB_CMD_BAND_LEVEL,(unsigned)bb_limit(
            (int)bb_face.requested.band_level[i]+bb_limit(delta,-127,127),0,127));
#endif
        else bb_send(BB_CMD_TEMPO,(unsigned)bb_limit(
            (int)bb_face.requested.bpm+bb_limit(delta,-270,270),30,300));
    }
    /* Focus/sound/pattern navigation wins over simultaneous variation keys.
     * A held key must be released before it can affect the newly focused row. */
    if(selection_changed)bb_keys_blocked|=fm1_in.notes;
    uint32_t fresh=selection_changed?0:*notes&~bb_keys_blocked;
    *notes=0;bb_keys_edges=0;
    for(unsigned key=0;key<27;key++)if(fresh&(1u<<key)) {
        int white=bb_panel_white_step(key);
        if(white>=0 && hm_available(HM_ACTIVE_MASK,(unsigned)white)) {
#if defined(BB_TWO_DRUM_HOST_EXPERIMENT)
            unsigned row=(unsigned)white;
            if(row<2) {
                if(!bb_face.requested.drums[row].enabled)
                    bb_send_machine(row,BB_CMD_DRUM_ENABLE,1);
                else bb_send_machine(row,BB_CMD_DRUM_MUTE,!bb_face.requested.drums[row].muted);
            }
            else bb_send_machine(0,BB_CMD_SYNTH_MUTE,!bb_face.requested.synths[0].muted);
#else
            bb_send_machine((unsigned)white,BB_CMD_BAND_TOGGLE,0);
#endif
            bb_dirty=1;continue;
        }
    }
    /* Process every bus mute before the single variation choice, independent
     * of physical white/black key ordering. Commands retain queue admission. */
    for(unsigned key=0;key<27;key++)if(fresh&(1u<<key)) {
        int part=bb_panel_black_index(key);
        if(part>=0 && part<8) {
            bb_library_result(hm_variation(&hm_focus,bb_drum_library,bb_library,
                                           bb_face.machine,&bb_sound,(unsigned)part));
            bb_dirty=1;break;
        }
    }
    return 1;
}
