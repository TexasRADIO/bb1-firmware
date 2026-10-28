/* SPDX-License-Identifier: GPL-3.0-only */
/* Drawing only. Read committed layer identity, including disabled layers.
 * Selection is independent of enable/mute: never imply an OFF layer plays. */
static void bb_kit_shelf_draw(void) {
    for(unsigned k=0;k<BB_MACHINES;k++) {
        const bb_machine *layer=&bb_face.requested.drums[k];
        int y=4+(int)k*23;char letter[2]={(char)('A'+k),0};
        int selected=k==bb_face.machine;
        uint16_t bg=selected?BB_ORANGE:BB_BG,ink=selected?BB_BG:BB_TEXT;
        if(selected)cv_rect(8,y,224,21,bg);else cv_frame(8,y,224,21,BB_DIM);
        cv_text_on(13,y+2,&AF_M,letter,ink,bg);
        cv_text_on(33,y+3,&AF_S,bb_ui_kit_name(k),ink,bg);
        cv_text_on(196,y+3,&AF_S,!layer->enabled?"OFF":layer->muted?"MUTE":"ON",ink,bg);
    }
    cv_text(9,105,&AF_S,"ARP: ADD / REC+ARP: ON/OFF",BB_TEXT);
}
