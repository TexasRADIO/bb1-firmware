/* SPDX-License-Identifier: GPL-3.0-only */
static void bb_home_result(int result) {
    bb_say(result==BB_FTRIO_CONTENT?bb_home_content_reason():
        result==BB_FTRIO_OK?"BANK REQUESTED":
        result==BB_FTRIO_GREYHOUND?"REC+KEY: TRIO VERSION":
        result==BB_FTRIO_FAMILY?"808 ENSEMBLE ONLY":
        result==BB_FTRIO_SWING?"UNSUPPORTED SWING":
        result==BB_FTRIO_REQUEST_OFF?"KIT A IS OFF":
        result==BB_PERFORMER_NO_BANK?"CHOOSE AN EXPERIENCE FIRST":
        result==BB_FTRIO_REQUEST_BUSY?"BUSY - RETRY":
        result==BB_FTRIO_REQUEST_UNAVAILABLE?"PERFORMANCE NOT READY":"SOURCE NOT SUPPORTED");
}
static void bb_home_poll(void) {
    const bb_performer *p=bb_home_view();bb_performance_status old=p->status;
    if(!bb_home_sync(&bb_sound))return;
    if(old.bank.applied!=p->status.bank.applied || old.part_launches!=p->status.part_launches)bb_home_refresh=1;
    if(bb_home_refresh && bb_home_sync_panel(&bb_face,&bb_sound)) {
        bb_home_refresh=0;bb_patch_from_scene[0]=bb_patch_from_scene[1]=p->playing.ticket?2:1;
        bb_patch_edited[0]=bb_patch_edited[1]=0;bb_dirty=1;
    }
    if(bb_mix_page==BB_MIX_BAND_PAGE &&
       (old.bank.seen!=p->status.bank.seen || old.bank.applied!=p->status.bank.applied ||
        old.bank.queued!=p->status.bank.queued || old.active!=p->status.active ||
        old.bpm!=p->status.bpm || old.running!=p->status.running || memcmp(old.level,p->status.level,sizeof old.level) ||
        old.queued!=p->status.queued || old.ready!=p->status.ready || old.result!=p->status.result))bb_dirty=1;
}
static void bb_home_input(uint32_t pressed,uint32_t *notes,int rec) {
    if(bb_mix_page!=BB_MIX_BAND_PAGE)return;
    uint32_t fresh=*notes&~bb_keys_blocked;
    *notes=0;bb_keys_edges=0;
    int32_t move=bb_read_encoder(EN_ALGO),page=bb_read_encoder(EN_PRESET);
    /* Navigation and storage gestures take priority over simultaneous notes. */
    const unsigned exits[]={B_HOME,B_SEQ,B_FX,B_GLO,B_SAVE};
    for(unsigned i=0;i<sizeof exits/sizeof *exits;i++)if(pressed&(1u<<panel.btn[exits[i]]))return;
    if(pressed&(1u<<panel.btn[B_OCTDN]))page--;
    if(pressed&(1u<<panel.btn[B_OCTUP]))page++;
    if(page){(void)bb_home_page(page);bb_dirty=1;}
    if(move){if(bb_home_refresh){bb_home_result(BB_FTRIO_REQUEST_BUSY);return;}
        bb_home_result(bb_home_move(&bb_sound,move,&bb_face.requested,
        rec?BB_FTRIO_THREE_ENGINE:0,64));return;}
    for(unsigned key=0;key<27;key++)if(fresh&(1u<<key)) {
        int bank=bb_panel_white_step(key);
        if(bank>=0){if(bb_home_refresh){bb_home_result(BB_FTRIO_REQUEST_BUSY);return;}
            bb_home_result(bb_home_bank_key(&bb_sound,(unsigned)bank,&bb_face.requested,
            rec?BB_FTRIO_THREE_ENGINE:0,64));return;}
    }
    for(unsigned key=0;key<27;key++)if(fresh&(1u<<key)) {
        int part=bb_panel_black_index(key);
        if(part>=0 && part<8){
            int result=bb_home_part_key(&bb_sound,(unsigned)part);
            if(result==BB_FTRIO_OK)bb_say("PART REQUESTED");else bb_home_result(result);
            return;
        }
    }
}
