/* SPDX-License-Identifier: GPL-3.0-only
 * Main-loop HOME sound entrance. Browsing never submits a sound command. */
static int bb_home_sound_browse(bb_page page,unsigned layer,int delta) {
    if(bb_sound_pick_audio_failed()) {
        bb_say("AUDIO FAULT / LOAD BLOCKED");return 0;
    }
    if(bb_sound_pick.pending) {
        bb_say("SOUND REQUEST STILL PENDING");return 0;
    }
    if(!bb_sound_pick_open(page,layer)) {
        bb_say("SOUND LIST NOT READY");return 0;
    }
    bb_aux_cancel(1);bb_aux_apply(1u<<8);
    bb_face.requested.page=page;
    if(page==BB_DRUMS)bb_face.machine=(uint8_t)layer;
    bb_sound_pick_page=1;bb_sound_pick_return=0;
    bb_library_page=bb_tone_page=0;
    (void)bb_sound_picker_move(&bb_sound_pick,delta);
    bb_dirty=1;return 1;
}
