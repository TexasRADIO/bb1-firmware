/* SPDX-License-Identifier: GPL-3.0-only */
static void bb_sound_pick_name(int x,int y,const char *name,uint16_t ink) {
    if(!name)name="UNAVAILABLE";
    const aafont_t *font=text_w(&AF_M,name)<=231-x?&AF_M:&AF_S;
    cv_text(x,y,font,name,ink);
}
static void bb_sound_pick_draw(void) {
    bb_sound_picker_view view;
    int failed=bb_sound_pick_audio_failed();
    int valid=!failed&&bb_sound_picker_observe(&bb_sound_pick,&bb_sound,&view);
    bb_page page=(bb_page)bb_sound_pick.page;
    char title[]="DRUM A / KITS";title[5]=(char)('A'+bb_sound_pick.machine);
    bb_backdrop(0,120,0);
    cv_text(9,6,&AF_M,page==BB_DRUMS?title:page==BB_DILLO?(BB_WITH_TELEGRAM?"DILLO BASS / SOUNDS":"DILLO / SOUNDS"):"DILLO LEAD / SOUNDS",BB_TEXT);
    cv_text(9,30,&AF_S,!valid?"STATE UNAVAILABLE":!view.enabled?"LAYER OFF":view.muted?"MUTED":
        page==BB_DRUMS&&bb_sound_pick.sample_count?"KIT > PATTERN":
        !bb_sound_picker_available(&bb_sound_pick,&view,bb_sound_pick.cursor)?"PATTERN MISMATCH":page==BB_DRUMS?"CHOOSE KIT":"CHOOSE SOUND",BB_DIM);
    cv_text(151,30,&AF_S,"K1 LEVEL",BB_DIM);
    unsigned level=page==BB_DRUMS?bb_face.requested.drums[bb_sound_pick.machine].level:
        bb_face.requested.band_level[page==BB_DILLO?BB_MIX_DILLO:BB_MIX_TELEGRAM];
    bb_level_number(204,30,level,&AF_S,BB_TEXT);
    bb_sound_pick_name(9,49,bb_sound_picker_item_name(&bb_sound_pick,bb_sound_pick.cursor),BB_ORANGE);
    cv_text(9,81,&AF_S,valid&&view.edited?"LOADED / EDITED:":"LOADED:",BB_DIM);
    cv_text(9,98,&AF_S,!valid?"STATE UNAVAILABLE":view.active_index<0?"CUSTOM SOUND":
        bb_sound_picker_item_name(&bb_sound_pick,(unsigned)view.active_index),BB_TEXT);
    cv_blit(0,0);bb_backdrop(120,120,0);
    unsigned first=(bb_sound_pick.cursor/4u)*4u,count=bb_sound_picker_items(&bb_sound_pick);
    for(unsigned row=0;row<4&&first+row<count;row++) {
        unsigned i=first+row;int y=2+(int)row*20;char number[12];fmt_int(number,(int32_t)i+1);
        cv_frame(8,y,224,19,i==bb_sound_pick.cursor?BB_ORANGE:BB_DIM);
        cv_text(14,y+2,&AF_S,number,BB_TEXT);
        cv_text(35,y+2,&AF_S,bb_sound_picker_item_name(&bb_sound_pick,i),
            valid&&(bb_sound_pick.sample_count||bb_sound_picker_available(&bb_sound_pick,&view,i))?BB_TEXT:BB_DIM);
        if(valid&&view.active_index==(int)i)cv_rect(225,y+5,3,8,BB_GREEN);
    }
    const char *state=failed?"AUDIO FAULT / LOAD BLOCKED":bb_sound_pick.status==BB_SP_FAILED?
        bb_sound_pick_refusal_message():
        bb_sound_pick.status==BB_SP_SUPERSEDED?"SOUND CHANGED ELSEWHERE":
        bb_sound_pick.status==BB_SP_APPLIED?"APPLIED / ALGO: PATTERNS":
        bb_sound_pick.status==BB_SP_SAME?"SOUND ALREADY LOADED":
        page==BB_DRUMS&&bb_sound_pick.sample_count?"KIT CHOICE / NOT LOADED":"ALGO: PATTERNS / HOME: STUDIO";
    if(failed)cv_text(9,87,&AF_S,"AUDIO FAULT / LOAD BLOCKED",BB_TEXT);
    else if(bb_sound_pick.pending) {
        int x=cv_text(9,87,&AF_S,"REQUEST: ",BB_DIM);
        cv_text(x,87,&AF_S,bb_sound_picker_item_name(&bb_sound_pick,bb_sound_pick.requested),BB_TEXT);
    } else cv_text(9,87,&AF_S,state,BB_TEXT);
    cv_text(9,105,&AF_S,failed?"RECOVERY NEEDED / HOME: STUDIO":
        page==BB_DRUMS&&bb_sound_pick.sample_count?"EDIT: CHOOSE KIT":page==BB_DRUMS?"PRESETS: BROWSE / EDIT: CHOOSE":"PRESETS: BROWSE / EDIT: LOAD",BB_DIM);cv_blit(0,120);
}
static void bb_sound_pick_leds(uint8_t *lit,uint8_t *dim,uint8_t *mid) {
    static const uint8_t actions[]={B_SCL,B_ENV,B_EDIT,B_GLO,B_HOME,B_SAVE,B_ARP,B_SEQ,B_PLAY,B_REC,B_FX};
    for(unsigned k=0;k<sizeof actions;k++)led_put(dim,panel.btn[actions[k]],1);
    if(bb_sound_pick.page==BB_TELEGRAM)led_put(dim,panel.btn[B_LFO],1);
    bb_sound_picker_view view;int valid=!bb_sound_pick_audio_failed()&&bb_sound_picker_observe(&bb_sound_pick,&bb_sound,&view);
    for(unsigned k=0;k<27;k++) {
        int key=bb_panel_white_step(k);
        if(key>=0&&(unsigned)key<bb_sound_picker_items(&bb_sound_pick)) {
            int available=valid&&(bb_sound_pick.sample_count||bb_sound_picker_available(&bb_sound_pick,&view,(unsigned)key));
            led_put(dim,14u+k,1);led_put(mid,14u+k,available&&key==bb_sound_pick.cursor);
            led_put(lit,14u+k,valid&&view.active_index==key);
        }
    }
    if(!bb_sound_pick_audio_failed()&&bb_face.requested.running)lit[LED_PLAY_GREEN>>3]|=(uint8_t)(1u<<(LED_PLAY_GREEN&7));
    for(unsigned k=0;k<FM1_NCOL;k++){fm1_led[k]=lit[k];fm1_led_dim[k]=dim[k];fm1_led_mid[k]=mid[k];fm1_led_breath[k]=0;}
}
