/* SPDX-License-Identifier: GPL-3.0-only */
/* Target UI inclusion after bb_send_machine. No static mutable state. */
static int bb_group_knobs_enabled(int rec) {
    return !rec && bb_face.requested.page==BB_DRUMS && bb_pad_page &&
        !bb_drum_sound_page && !bb_kit_page && !bb_library_page &&
        !bb_mix_page && !bb_diag_page && !bb_send_page && !bb_effect_page &&
        !bb_scene_page && !bb_evo_page;
}
static int bb_group_knobs_input(int rec,int stopping) {
    if(!bb_group_knobs_enabled(rec))return 0;
    for(unsigned k=0;k<GG_COUNT;k++) {
        int32_t delta=bb_read_encoder(EN_K1+k);
        if(!delta || stopping)continue; /* Never replay a simultaneous STOP detent. */
        bb_group_view view;
        int result=bb_group_input(&bb_sound.commands,bb_sound.groups,
            bb_face.machine,BB_GROUP_TURN,k,delta,&view);
        bb_dirty=1;
        if(result==BB_GROUP_REFUSED){bb_face.rejected++;bb_say("GROUP BUSY - RETRY");}
        else bb_face.rejected=0;
    }
    return 1;
}
