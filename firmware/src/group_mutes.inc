/* SPDX-License-Identifier: GPL-3.0-only */
/* Pads only. Edges are already lift-guarded by the caller. No mutable mirror,
 * voice stealing, transport change or loss of the pre-mute fader level. */
static void bb_group_mutes_input(uint32_t notes,uint32_t pressed,int rec) {
    /* A simultaneous panel/page/transport gesture cannot also toggle a group. */
    if(pressed || !bb_group_knobs_enabled(rec))return;
    for(unsigned key=0;key<27;key++)if(notes&(1u<<key)) {
        int group=bb_panel_black_index(key);
        if(group<0 || group>=GG_COUNT)continue;
        bb_group_view view;
        int result=bb_group_input(&bb_sound.commands,bb_sound.groups,
            bb_face.machine,BB_GROUP_TOGGLE,(unsigned)group,0,&view);
        bb_dirty=1;
        if(result==BB_GROUP_REFUSED){bb_face.rejected++;bb_say("GROUP BUSY - RETRY");}
        else bb_face.rejected=0;
    }
}
