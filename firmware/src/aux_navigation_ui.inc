/* SPDX-License-Identifier: GPL-3.0-only
 * Main-loop UI only. No audio ownership, musical-state copy or allocation. */
#include "aux_navigation.c"
#define BB_DIRECT_MIXER 1
static an_return bb_aux_return;
static an_fx_gesture bb_aux_fx,bb_home_gesture;
static uint16_t bb_aux_snapshot(void) {
    unsigned kind=HM_VISIBLE?AN_BASE:bb_mix_page==BB_MIX_DIRECT_PAGE?AN_MIX:
        bb_effect_page?AN_FX:bb_send_page?AN_SENDS:AN_BASE;
    if(kind==AN_BASE)bb_sound_pick_return=bb_sound_pick_page;
    unsigned bits=bb_mix_page | ((unsigned)bb_diag_page<<2) |
        ((unsigned)bb_tone_page<<3) | ((unsigned)bb_drum_sound_page<<5) |
        ((unsigned)bb_drum_color_page<<6) | ((unsigned)bb_kit_page<<7) |
        ((unsigned)bb_pad_page<<8) | ((unsigned)bb_library_page<<9);
#if BB_WITH_EFFECTS
    bits|=((unsigned)bb_send_page<<10)|((unsigned)bb_effect_page<<11)|((unsigned)bb_effect_bank<<12);
#endif
    return (uint16_t)(bits|(kind<<AN_KIND_SHIFT));
}
static void bb_aux_apply(uint16_t page) {
    bb_sound_pick_page=(uint8_t)(an_kind(page)==AN_BASE&&bb_sound_pick_return);
    bb_mix_page=page&3u;bb_diag_page=(page>>2)&1u;bb_tone_page=(page>>3)&3u;
    bb_drum_sound_page=(page>>5)&1u;bb_drum_color_page=(page>>6)&1u;
    bb_kit_page=(page>>7)&1u;bb_pad_page=(page>>8)&1u;
#if BB_WITH_BROWSER
    bb_library_page=(page>>9)&1u;
#endif
#if BB_WITH_EFFECTS
    bb_send_page=(page>>10)&1u;bb_effect_page=(page>>11)&1u;bb_effect_bank=(page>>12)&3u;
#endif
    bb_keys_blocked|=fm1_in.notes;bb_keys_edges=bb_keys_taps=0;bb_dirty=1;
}
static void bb_aux_cancel(int history) {
    (void)an_fx_step(&bb_aux_fx,fm1_ms,!!(fm1_in.buttons&(1u<<panel.btn[B_FX])),0,0,1);
    (void)an_fx_step(&bb_home_gesture,fm1_ms,!!(fm1_in.buttons&(1u<<panel.btn[B_HOME])),0,0,1);
    if(history)an_reset(&bb_aux_return);
}
static void bb_aux_toggle(unsigned kind) {
    uint16_t wanted=(uint16_t)(kind<<AN_KIND_SHIFT),next;
    if(kind==AN_MIX)wanted|=BB_MIX_DIRECT_PAGE;
    else if(kind==AN_FX)wanted|=1u<<11;
    else if(kind==AN_SENDS)wanted|=1u<<10;
    /* A legacy auxiliary page without history returns to the instrument's
     * pads/keyboard face. Stored snapshots retain exact editor/page flags. */
    if(!an_toggle(&bb_aux_return,bb_aux_snapshot(),wanted,1u<<8,&next)) {
        bb_say("PAGE BUSY - RETRY");return;
    }
    bb_aux_apply(next);
    bb_say(an_kind(next)==AN_MIX?"MIX / GLO: BACK":
           an_kind(next)==AN_FX?"FX / HOLD: SENDS":
           an_kind(next)==AN_SENDS?"SENDS / HOLD FX: BACK":"BACK TO PAGE");
}
static int bb_aux_input(uint32_t pressed,int rec) {
    const uint32_t fx=1u<<panel.btn[B_FX],glo=1u<<panel.btn[B_GLO];
    unsigned gesture=an_fx_step(&bb_aux_fx,fm1_ms,!!(fm1_in.buttons&fx),!!(pressed&fx),
        rec,!!(pressed&~fx));
    /* STOP and every other panel action win over navigation, including a
     * release coincident with STOP. No delayed tap may replay afterwards. */
    if(pressed&~(fx|glo|(1u<<panel.btn[B_REC])))return 0;
    int consumed=0;
    if(!rec && (pressed&glo)){bb_aux_toggle(AN_MIX);consumed=1;}
#if BB_WITH_EFFECTS
    else if(rec && (pressed&fx)) {
        unsigned diag=!bb_diag_page;bb_aux_cancel(1);bb_aux_apply((uint16_t)((diag<<2)|(1u<<8)));
        consumed=1;
    } else if(gesture) {bb_aux_toggle(gesture==AN_FX_HOLD?AN_SENDS:AN_FX);consumed=1;}
    else if(!rec && (pressed&fx))consumed=1; /* Classify on release or hold. */
#else
    (void)gesture;
#endif
    if(consumed) {
        /* Detents from the old page must not leak into the new page. */
        for(unsigned role=0;role<7;role++)(void)bb_read_encoder(role);
        bb_keys_blocked|=fm1_in.notes;bb_keys_edges=bb_keys_taps=0;
    }
    return consumed;
}
