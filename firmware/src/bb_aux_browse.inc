/* SPDX-License-Identifier: GPL-3.0-only
 * Global browsing from mixer/FX/sends. Explicit buttons and REC edits keep
 * priority. This main-loop adapter never mutates audio-owned state directly. */
static int bb_browse_consume(uint32_t *notes) {
    for(unsigned encoder=0;encoder<7;encoder++)(void)bb_read_encoder(encoder);
    bb_keys_blocked|=fm1_in.notes;bb_keys_edges=bb_keys_taps=0;*notes=0;
    return 1;
}
static int bb_aux_browse_input(uint32_t pressed,uint32_t *notes,int rec) {
    if(rec||pressed||bb_diag_page||(!bb_mix_page&&!bb_effect_page&&!bb_send_page))return 0;
    int delta=bb_read_encoder(EN_SELECT);
    if(delta){bb_select_instrument(delta);return bb_browse_consume(notes);}
    int sound=bb_read_encoder(EN_PRESET),pattern=bb_read_encoder(EN_ALGO);
    if(!sound&&!pattern)return 0;
    if(sound) {
        if(bb_sound_pick_audio_failed()){
            bb_say("AUDIO FAULT / LOAD BLOCKED");return bb_browse_consume(notes);
        }
        if(bb_sound_pick.pending){bb_say("SOUND REQUEST STILL PENDING");return bb_browse_consume(notes);}
        if(!bb_sound_pick_open(bb_face.requested.page,bb_face.machine)) {
            bb_say("SOUND LIST NOT READY");return bb_browse_consume(notes);
        }
        bb_aux_cancel(1);bb_sound_pick_return=0;bb_aux_apply(1u<<8);
        bb_sound_pick_page=1;bb_library_page=0;bb_tone_page=0;
        (void)bb_sound_picker_move(&bb_sound_pick,sound);
        bb_dirty=1;bb_sound_pick_poll();
    } else {
        bb_player *player=bb_library_current();
        if(!player->ready){bb_say("LIBRARY NOT READY");return bb_browse_consume(notes);}
        bb_aux_cancel(1);bb_sound_pick_return=0;bb_aux_apply(1u<<8);
        bb_library_page=1;bb_tone_page=0;
        if(bb_pattern_confirm_first){(void)bb_library_preview_move(player,pattern);bb_dirty=1;}
        else bb_library_result(bb_player_move(player,&bb_sound,pattern));
    }
    return bb_browse_consume(notes);
}
