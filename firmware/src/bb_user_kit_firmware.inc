/* SPDX-License-Identifier: GPL-3.0-only
 * BB1 2.0 kit builder: BUILD mode and USER kits on drum A, on the real navigation path.
 *
 * bb_nav decides (bb_nav.c: KIT black 1 = BUILD, white 1-11 + black 2-7 = lanes, K1-K3 shape, K4 voice,
 * PRESETS kit, EDIT = SAVE AS then STORE, HOME back one level); bb_nav_firmware.inc calls these helpers to
 * perform it. State is bb_uk (bb_user_kit_state.inc, .pool). Main loop only.
 *
 * Sound: entering BUILD changes nothing (the draft is a copy of the kit drum A plays). An edit resolves the draft
 * into the session scratch (free: no scene job can run while BUILD edits or a USER load is queued) and copies it
 * into drum A's table under the command lock, then drum A plays it from the next block. A stored USER kit loads
 * through the existing kit+phrase request (QUEUED, then APPLIED at the boundary) from the scratch, and is copied
 * into the table once applied. */
#if BB_WITH_USER_KITS
#define BB_UK_SCRATCH ((bb_uk_resolved *)(void *)&bb_session_memory.pair)
static const uint8_t bb_uk_lane_voice[BB_UK_VOICES]={0,1,2,3,4,8,10,12,13,14,15,5,6,7,9,11,16};
static const char bb_uk_code[BB_UK_VOICES][3]={"BD","SD","LT","MT","HT","LC","MC","HC","RS","CL","CP","MA","CB","CY","OH","CH","RD"};

static BB_UK_SMALL const char *bb_uk_machine_name(unsigned i){
    return !i?"808":bb_sample_ui_names&&i-1u<bb_sample_ui_count?bb_ui_kit_display_name(bb_sample_ui_names[i-1u]):"?";
}
static BB_UK_SMALL void bb_uk_boot(void){
    bb_uk.nm=1;
    bb_uk.m[0]=(bb_uk_machine){bb_uk_key('N',"808",3),BB_UK_ENGINE_808,BB_SAMPLE_808_ID,65535u,0,0};
    /* bb_sample_ui_names: the validated pack's directory names, set only once its kits are registered */
    for(unsigned k=0;bb_sample_ui_names&&k<bb_sample_ui_count&&k<8u&&k<bb_sound.sample_catalogue_count;k++){
        const bb_sample_catalogue_entry *c=&bb_sound.sample_catalogue[k];
        bb_uk.m[bb_uk.nm++]=(bb_uk_machine){bb_uk_key('S',bb_sample_ui_names[k],12),BB_UK_ENGINE_SAMPLE,(uint8_t)k,
                                            c->source_voices,c->zones,c->nz};
    }
    bb_uk_bank_names(bb_uk_bank_read());
}
static BB_UK_SMALL int bb_uk_find(uint32_t key){for(unsigned i=0;i<bb_uk.nm;i++)if(bb_uk.m[i].key==key)return (int)i;return -1;}
static BB_UK_SMALL unsigned bb_uk_playing_id(void){
    bb_sample_status s;return bb_audio_sample_status_layer(&bb_sound,0,&s)?s.selected:BB_SAMPLE_808_ID;
}
/* "USER 2 MYKIT", "USER 2", or a draft: "NEW KIT" (its own name when it has one) */
static BB_UK_SMALL const char *bb_uk_name(void){
    char *t=bb_uk.text;unsigned n=0;
    if(bb_uk.origin<BB_UK_SLOTS){memcpy(t,"USER ",5);n=5;t[n++]=(char)('1'+bb_uk.origin);t[n++]=' ';}
    else if(!bb_uk.draft.name[0]){memcpy(t,"NEW KIT",7);n=7;}
    memcpy(t+n,bb_uk.draft.name,BB_UK_NAME);t[n+BB_UK_NAME]=0;return t;
}
/* Display name of drum A when it plays a USER kit (STUDIO lane, NEXT KIT); 0 otherwise. */
static BB_UK_SMALL const char *bb_uk_layer_name(unsigned layer){
    return !layer&&bb_uk.live&&bb_uk_playing_id()==BB_SAMPLE_USER_ID?bb_uk_name():0;
}
/* The KIT list's USER rows (bb_sound_picker asks): "USER 3 SIMMONSY", "USER 4" */
const char *bb_sound_picker_user_name(unsigned s){
    static const char *const label[BB_UK_SLOTS]={"USER 1 ","USER 2 ","USER 3 ","USER 4 "};
    s&=3u;if(!(bb_uk.used>>s&1u))return label[s];
    memcpy(bb_uk.text,label[s],7);memcpy(bb_uk.text+7,bb_uk.name[s],BB_UK_NAME);bb_uk.text[7+BB_UK_NAME]=0;
    return bb_uk.text;
}
int bb_sound_picker_user_active(void){return bb_uk.live&&bb_uk.origin<BB_UK_SLOTS?(int)bb_uk.origin:-1;}

/* ------------------------------------------------------------ BUILD edits */
static BB_UK_SMALL int bb_uk_in_build(void){return bb_nav.page==BB_NAV_KIT&&bb_nav.bank==BB_NAV_KIT_BUILD&&!bb_nav_row;}
static BB_UK_SMALL bb_uk_voice *bb_uk_lane_entry(void){return &bb_uk.draft.v[bb_uk_lane_voice[bb_uk.lane%BB_UK_VOICES]];}
static BB_UK_SMALL int bb_uk_lane_machine(void){return bb_uk_find(bb_uk.draft.key[bb_uk_lane_entry()->machine]);}
/* Copy t into drum A's table and play it from the next block (command lock held: the owner is not rendering). */
static BB_UK_SMALL int bb_uk_play(const bb_uk_resolved *t){
    uintptr_t token=bb_target_enter(0);
    bb_uk.res=*t;int ok=bb_audio_user_kit_swap_owned(&bb_sound,0,&bb_uk.res);
    bb_target_leave(0,token);
    return ok;
}
/* The draft changed (kit[1] holds it as it was): resolve and play it. Refused: roll back and say why. */
static BB_UK_SMALL int bb_uk_commit_edit(void){
    int r=bb_uk.loading?-1:bb_uk_resolve_into(&bb_uk.draft,BB_UK_SCRATCH);
    if(r!=BB_UK_OK||!bb_uk_play(BB_UK_SCRATCH)){
        bb_uk.draft=bb_uk.kit[1];bb_say(r<0?BB_TX(TX_USER_LOAD_QUEUED_WAIT):r==BB_UK_FULL_ZONES?BB_TX(TX_KIT_FULL_16_SOUNDS):BB_TX(TX_NOT_PLAYABLE));
        return 0;
    }
    bb_uk.live=1;bb_uk.edits=1;bb_dirty=1;return 1;
}
/* BUILD, SAVE AS or the list again (bb_nav changed bank) */
static BB_UK_SMALL void bb_uk_nav_mode(void){
    bb_dirty=1;
    if(bb_nav.page!=BB_NAV_KIT||bb_nav_row||bb_nav.bank!=BB_NAV_KIT_BUILD)return;
    if(!bb_audio_sample_layer(&bb_sound,0)){bb_nav.bank=BB_NAV_KIT_LIST;bb_say("BUILD NEEDS THE SOUND LIBRARY");return;}
    if(!bb_uk.live||bb_uk_playing_id()!=BB_SAMPLE_USER_ID){
        /* a draft copy of the kit drum A plays: nothing changes until the first edit */
        unsigned id=bb_uk_playing_id(),i=id<bb_sample_ui_count&&id+1u<bb_uk.nm?id+1u:0u;
        (void)bb_uk_from_machine(&bb_uk.draft,&bb_uk.m[i],"");
        bb_uk.live=0;bb_uk.origin=BB_UK_ORIGIN_DRAFT;bb_uk.edits=0;
    }
    if(!bb_uk.loading)(void)bb_uk_resolve_into(&bb_uk.draft,BB_UK_SCRATCH);   /* the ~ and ? marks */
    bb_uk.save_slot=bb_uk.origin<BB_UK_SLOTS?bb_uk.origin:0u;
}
static BB_UK_SMALL void bb_uk_lane_key(unsigned lane){
    if(!bb_uk_in_build()||lane>=BB_UK_VOICES)return;
    bb_uk.lane=(uint8_t)lane;bb_dirty=1;
    if(lane<BB_DRUM_TRACKS)(void)bb_panel_drum_hit(&bb_face,&bb_sound.commands,lane,bb_face.accent?127u:88u);
}
/* PRESETS (machine) or K4 (voice) in BUILD: the focused lane's machine, or its voice there. */
static BB_UK_SMALL void bb_uk_source(int machine,int d){
    int m=bb_uk_lane_machine();unsigned v=bb_uk_lane_voice[bb_uk.lane%BB_UK_VOICES],sv=bb_uk_lane_entry()->voice;
    if(machine)m=bb_limit((m<0?0:m)+bb_limit(d,-8,8),0,(int)bb_uk.nm-1);
    else if(m<0){bb_say("KIT MISSING / PRESETS RE-POINTS");return;}
    else sv=(unsigned)bb_limit((int)sv+bb_limit(d,-17,17),0,BB_UK_VOICES-1);
    bb_uk.kit[1]=bb_uk.draft;
    if(!bb_uk_set_source(&bb_uk.draft,v,&bb_uk.m[m],machine?v:sv)){bb_uk.draft=bb_uk.kit[1];bb_say(BB_TX(TX_8_KITS_PER_USER_KIT));return;}
    if(memcmp(&bb_uk.draft,&bb_uk.kit[1],sizeof bb_uk.draft)&&bb_uk_commit_edit()&&!bb_face.requested.running&&
       bb_uk.lane<BB_DRUM_TRACKS)(void)bb_panel_drum_hit(&bb_face,&bb_sound.commands,bb_uk.lane,88u);   /* heard when stopped */
}
static BB_UK_SMALL int bb_uk_kit_turn(int d){
    if(bb_nav.page!=BB_NAV_KIT||bb_nav_row||!bb_nav.bank)return 0;
    if(bb_nav.bank==BB_NAV_KIT_BUILD)bb_uk_source(1,d);
    return 1;
}
static BB_UK_SMALL void bb_uk_knob(unsigned role,int d){
    if(!bb_uk_in_build())return;
    if(role==BB_NAV_R_UK_VOICE){bb_uk_source(0,d);return;}
    bb_uk_voice *e=bb_uk_lane_entry();
    uint8_t *p=role==BB_NAV_R_UK_LEVEL?&e->level:role==BB_NAV_R_UK_TUNE?&e->tune:&e->decay;
    int v=bb_limit((int)*p+bb_limit(d,-127,127),0,127);
    if(v==*p)return;
    bb_uk.kit[1]=bb_uk.draft;*p=(uint8_t)v;(void)bb_uk_commit_edit();
}
static BB_UK_SMALL void bb_uk_slot_key(unsigned s){if(s<BB_UK_SLOTS){bb_uk.save_slot=(uint8_t)s;bb_dirty=1;}}
static BB_UK_SMALL void bb_uk_store(void){(void)bb_uk_store_request();bb_say(bb_session_message);}

/* ------------------------------------------------------------ KIT list: USER 1-4 after the machines (drum A)
 * The rows are the sound picker's own, so PRESETS, the white keys, the LEDs and the v3 list treat them like any kit.
 * EDIT (or a white key) on one: the kit intent, as a sampled kit's, so the pattern confirmed next carries it.
 * -1 = not a USER row, 0 = refused, 1 = chosen. */
static BB_UK_SMALL int bb_uk_list_commit(void){
    unsigned base=bb_sound_picker_machine_items(bb_sound_pick.sample_count),s=bb_sound_pick.cursor-base;
    if(bb_nav_row||!bb_sound_pick.sample_count||bb_sound_pick.machine||bb_sound_pick.cursor<base||s>=BB_UK_SLOTS)return -1;
    if(!(bb_uk.used>>s&1u)){bb_say(BB_TX(TX_EMPTY_SLOT_BLACK_1_BUILD));return 0;}
    if(bb_uk.loading||bb_sound_pick.pending||!bb_drum_library[0].ready){bb_say(BB_TX(TX_KIT_NOT_READY));return 0;}
    if(!bb_player_preview_align_kit(&bb_drum_library[0],&bb_sound,BB_SAMPLE_USER_ID)){bb_say(BB_TX(TX_NO_PATTERNS_FOR_THIS_KIT));return 0;}
    bb_kit_intent[0].id=(uint8_t)BB_SAMPLE_USER_ID;bb_kit_intent[0].cursor=bb_sound_pick.cursor;bb_kit_intent[0].valid=1;
    bb_uk.load_slot=(uint8_t)s;
    bb_sound_pick_close();bb_library_page=1;
    bb_say(BB_TX(TX_CHOOSE_PATTERN_EDIT_CHOOSE));return 1;
}
/* bb_library_confirm, drum A, intent = USER: read the slot from flash and stage its table for the kit+phrase
 * request about to be sent. */
static BB_UK_SMALL int bb_uk_stage_load(void){
    unsigned s=bb_uk.load_slot&3u;
    if(bb_uk.loading||!(bb_uk_bank_read()>>s&1u)){bb_say(BB_TX(TX_USER_KIT_NOT_READY));return 0;}
    bb_uk.kit[0]=bb_uk_bank4()[s];
    if(bb_uk_resolve_into(&bb_uk.kit[0],BB_UK_SCRATCH)!=BB_UK_OK){bb_say(BB_TX(TX_USER_KIT_NOT_PLAYABLE));return 0;}
    uintptr_t token=bb_target_enter(0);bb_sound.user_staged[0]=BB_UK_SCRATCH;bb_target_leave(0,token);
    bb_uk.loading=1;return 1;
}
static BB_UK_SMALL void bb_uk_staged(int result){
    if(result!=BB_FACTORY_PATTERN_OK){bb_uk.loading=0;return;}
    bb_uk.ticket=bb_drum_library[0].request.ticket;
    bb_say(bb_face.requested.running?BB_TX(TX_USER_KIT_QUEUED_NEXT_BAR):BB_TX(TX_USER_KIT_REQUESTED));
}

/* ------------------------------------------------------------ facts and the poll */
static BB_UK_SMALL void bb_uk_facts(bb_nav_facts *f){
    if(bb_nav.page!=BB_NAV_KIT||bb_nav_row)return;
    if(bb_nav.bank==BB_NAV_KIT_BUILD){f->list_count=BB_UK_VOICES;f->list_cursor=bb_uk.lane;}
    else if(bb_nav.bank==BB_NAV_KIT_SAVE_AS){f->list_count=BB_UK_SLOTS;f->list_cursor=bb_uk.save_slot;}
}
static BB_UK_SMALL void bb_uk_poll(void){
    if(bb_uk.loading){
        const bb_player *p=&bb_drum_library[0];
        if(p->request.ticket!=bb_uk.ticket||!p->request.has_kit||p->request.kit!=BB_SAMPLE_USER_ID)bb_uk.loading=0;
        else if(p->status.seen==p->request.ticket&&(p->status.result==BB_PATTERN_APPLIED||
                p->status.result==BB_PATTERN_REJECTED||p->status.result==BB_PATTERN_CANCELLED)){
            bb_uk.loading=0;
            /* applied from the scratch: move it into drum A's own table before any scene job reuses the scratch */
            if(p->status.result==BB_PATTERN_APPLIED&&bb_uk_playing_id()==BB_SAMPLE_USER_ID&&bb_uk_play(BB_UK_SCRATCH)){
                bb_uk.draft=bb_uk.kit[0];bb_uk.live=1;bb_uk.origin=bb_uk.load_slot;bb_uk.edits=0;bb_say(BB_TX(TX_USER_KIT_APPLIED));
            }
            bb_dirty=1;
        }
    }
    if(bb_uk.live&&bb_uk_playing_id()!=BB_SAMPLE_USER_ID)bb_uk.live=0;   /* another kit took drum A */
    if(bb_uk.stored){bb_uk.stored=0;if(bb_nav.page==BB_NAV_KIT&&bb_nav.bank==BB_NAV_KIT_SAVE_AS)bb_nav.bank=BB_NAV_KIT_BUILD;bb_dirty=1;}
}
#endif
