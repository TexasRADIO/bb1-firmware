/* SPDX-License-Identifier: GPL-3.0-only */
static void bb_home_label(int y,const char *prefix,const bb_performer_selection *selection) {
    char title[128];int x=cv_text(9,y,&AF_S,prefix,BB_DIM);
    if(!selection->ticket){cv_text(x+3,y,&AF_S,"CURRENT SETUP",BB_TEXT);return;}
    bb_catalogue_id id=bb_catalogue_find(&bb_factory_catalogue,selection->choice.bank,"name");
    /* This is the whole bank, not the initial part used when it was loaded.
     * Actual playing and queued parts are shown independently in the A-H row. */
    bb_library_text(id,&AF_S,231-x-3,title);cv_text(x+3,y,&AF_S,title,BB_TEXT);
}
static void bb_home_draw(int pulse) {
    const bb_performer *p=bb_home_view();char title[128];bb_factory_choice choice;
    bb_backdrop(0,120,pulse);
#if defined(BB_TWO_DRUM_HOST_EXPERIMENT)
    cv_text(9,6,&AF_M,"A + DILLO / BANKS",BB_TEXT);
#else
    cv_text(9,6,&AF_M,"BIG BEAT / BAND",BB_TEXT);
#endif
    bb_number(191,5,p->status.bpm?p->status.bpm:bb_face.requested.bpm,&AF_M,BB_TEXT);
    /* Share the actual knob contract with input; performer channel order is
     * not the two-drum interface order. Never label drum B as Dillo/Telegram. */
    for(unsigned slot=0;slot<4;slot++) {
        int x=3+(int)slot*60;
        const bb_mixer_descriptor *control=bb_mix_control(BB_MIX_BAND_PAGE,slot);
        if(!control)continue;
        cv_text(x,27,&AF_S,control->label,BB_DIM);
        if(control->kind==BB_MX_TEMPO)bb_number(x,40,bb_mix_value(control),&AF_S,BB_TEXT);
        else bb_level_number(x,40,bb_mix_value(control),&AF_S,BB_TEXT);
    }
    cv_text(9,54,&AF_S,"CHOOSE EXPERIENCE",BB_DIM);
    if(p->ready && bb_browser_current(&p->view,&choice)) {
        bb_library_text(bb_catalogue_find(&bb_factory_catalogue,choice.bank,"name"),&AF_M,222,title);
        cv_text(9,68,&AF_M,title,BB_TEXT);
    } else cv_text(9,68,&AF_M,"LIBRARY NOT READY",BB_TEXT);
    bb_home_label(88,p->status.running?"PLAYING:":"LOADED:",&p->playing);
    if(bb_performer_pending(p))bb_home_label(105,bb_performer_pending(p)==2?"QUEUED:":"REQUEST:",&p->request);
    else cv_text(9,105,&AF_S,bb_hint_until?bb_hint:
        p->status.bank.result==BB_TRIO_REJECTED?"REQUEST REJECTED":
        p->playing.capabilities&BB_RECIPE_GREYHOUND?"TRIO VERSION / NO GREYHOUND":"ALGO: BANK / PRESETS: PAGE",BB_DIM);
    cv_blit(0,0);bb_backdrop(120,120,pulse);
    unsigned first=(p->view.bank_index%16u)>=8u?8u:0u;
    for(unsigned row=0;row<8;row++) {
        unsigned key=first+row;bb_catalogue_id bank=p->ready?bb_browser_bank_cell(&p->view,key):BB_CATALOGUE_NONE;
        if(bank==BB_CATALOGUE_NONE)continue;
        int x=8+(int)(row%2u)*115,y=2+(int)(row/2u)*21;
        unsigned active=p->playing.ticket && p->playing.choice.bank==bank;
        uint16_t bg=active?BB_ART:BB_GREEN,ink=active?BB_TEXT:BB_BG;
        cv_rect(x,y,109,20,bg);
        cv_frame(x,y,109,20,p->view.bank_index==p->view.bank_page*16u+key?BB_SPARK_EMBER:BB_DIM);
        char number[12];fmt_int(number,(int32_t)key+1);cv_text_on(x+3,y+2,&AF_S,number,ink,bg);
        bb_library_text(bb_catalogue_find(&bb_factory_catalogue,bank,"name"),&AF_S,84,title);
        cv_text_on(x+22,y+2,&AF_S,title,ink,bg);
        if(bb_performer_pending(p) && p->request.choice.bank==bank)cv_rect(x+3,y+18,103,2,BB_SPARK_EMBER);
    }
    for(unsigned key=0;key<8;key++) {
        int x=9+(int)key*28;char letter[2]={(char)('A'+key),0};
        uint16_t ink=p->status.ready?(p->status.active==key?BB_BG:BB_DIM):BB_ART;
        if(p->status.ready && p->status.active==key)cv_rect(x,86,25,18,BB_SPARK_EMBER);
        cv_frame(x,86,25,18,p->status.ready && p->status.queued==key?BB_SPARK_RED:ink);
        cv_text_on(x+8,87,&AF_S,letter,ink,p->status.ready && p->status.active==key?BB_SPARK_EMBER:BB_BG);
    }
#if defined(BB_TWO_DRUM_HOST_EXPERIMENT)
    cv_text(9,105,&AF_S,"DRUM B KEEPS ITS PATTERN",BB_TEXT);
#else
    cv_text(9,105,&AF_S,"K1-3: MIX / REC+KEY: TRIO",BB_TEXT);
#endif
    cv_blit(0,120);
}
static void bb_home_leds(uint8_t *lit,uint8_t *dim,uint8_t *mid) {
    const bb_performer *p=bb_home_view();uint8_t breath[FM1_NCOL]={0};
    static const uint8_t actions[]={B_HOME,B_SEQ,B_PLAY,B_REC,B_FX,B_SAVE,B_OCTDN,B_OCTUP};
    for(unsigned i=0;i<sizeof actions;i++)led_put(dim,panel.btn[actions[i]],1);
    led_put(lit,panel.btn[B_HOME],1);
    led_put(dim,panel.btn[B_LFO],(fm1_in.buttons&(1u<<panel.btn[B_REC]))!=0);
    for(unsigned key=0;key<27;key++) {
        int bank_key=bb_panel_white_step(key),part=bb_panel_black_index(key);
        bb_catalogue_id bank=p->ready && bank_key>=0?bb_browser_bank_cell(&p->view,(unsigned)bank_key):BB_CATALOGUE_NONE;
        if(bank!=BB_CATALOGUE_NONE) {
            if(bb_performer_pending(p) && p->request.choice.bank==bank)led_put(breath,14u+key,1);
            else if(p->playing.ticket && p->playing.choice.bank==bank)led_put(lit,14u+key,1);
            else if(p->view.bank_index==p->view.bank_page*16u+(unsigned)bank_key)led_put(mid,14u+key,1);
            else led_put(dim,14u+key,1);
        } else if(part>=0 && part<8 && p->status.ready) {
            if(p->status.queued==(unsigned)part)led_put(breath,14u+key,1);
            else if(p->status.active==(unsigned)part)led_put(lit,14u+key,1);
            else led_put(dim,14u+key,1);
        }
    }
    if(bb_face.requested.running)lit[LED_PLAY_GREEN>>3]|=(uint8_t)(1u<<(LED_PLAY_GREEN&7));
    for(unsigned i=0;i<FM1_NCOL;i++){fm1_led[i]=lit[i];fm1_led_dim[i]=dim[i];fm1_led_mid[i]=mid[i];fm1_led_breath[i]=breath[i];}
}
