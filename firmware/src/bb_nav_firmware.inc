/* SPDX-License-Identifier: GPL-3.0-only
 * Navigation v2.1 integration (claude-code/navigation/integration).
 *
 * bb_nav owns the ONE page variable. Every input scan:
 *   panel edges/levels, all 7 encoders, 27 key edges/levels, facts
 *     -> bb_nav_input -> actions, encoder routes, key events (performed here
 *        with the existing calls: bb_send*, picker, library, session, evolve)
 *     -> bb_nav_project(): the legacy page flags the existing renderer reads
 *        are DERIVED from bb_nav's page/bank/keys here and nowhere else.
 * Main loop only. Nothing here runs in the audio interrupt, and no audio
 * object is written except through the existing command queue/helpers.
 * Included by bb_target_ui.c after the existing input helpers. */
#define BB_NAV_FIRMWARE_INCLUDE 1
#include "bb_nav.c"
#undef BB_NAV_FIRMWARE_INCLUDE
#undef B
#undef BOTH_OCT
#undef NONE_IDX

#define BB_NAV_FEATURES (BB_NAV_F_TAP | BB_NAV_F_SCALE | BB_NAV_F_THROW | BB_NAV_F_STEP_PITCH)
/* Not in this build (need audio-side or save-format work, INTEGRATION.md §7):
 * F_RESTART (11), F_FAV_KITS (14), F_FILL (15). Their gestures say NOT IN
 * THIS BUILD and their LEDs are dark. */

static bb_nav_state bb_nav;
static bb_nav_out bb_nav_o;          /* 288 B: static, not on the stack */
static bb_nav_out bb_nav_aux_o;      /* forced page / kit confirmed (never nested) */
static bb_nav_facts bb_nav_f;        /* facts of the last scan (LEDs, drawing) */
static uint8_t bb_nav_row;           /* focus: 0 DRUM A, 1 DRUM B, 2 DILLO */
static uint8_t bb_nav_shown_page=0xFF,bb_nav_shown_row=0xFF; /* last projection */
static uint8_t bb_nav_meters;        /* GLOBAL white 3: meters on the GLOBAL page */
static uint8_t bb_nav_swing_x2=112;  /* swing restored by SCL (x2 units, 112 = 56 %) */
static uint8_t bb_nav_gsolo[BB_MACHINES],bb_nav_gsolo_muted[BB_MACHINES];
static uint8_t bb_nav_isolo,bb_nav_isolo_muted;
static uint8_t bb_nav_throw,bb_nav_throw_saved;
static uint32_t bb_nav_note_on[4],bb_nav_note_owed[4]; /* MIDI note bitmaps, Dillo */
static uint8_t bb_nav_note_phys[128];                  /* physical key of a sounding note */
static uint8_t bb_nav_phys[BB_NAV_KEYS];               /* logical key -> physical note bit */
static uint32_t bb_global_dirty_at;

/* ------------------------------------------------------------ mapping */
static uint32_t bb_nav_buttons(uint32_t matrix) {
    uint32_t logical=0;
    for(unsigned b=0;b<NB;b++)if(matrix&(1u<<panel.btn[b]))logical|=1u<<b;
    return logical;
}
static uint32_t bb_nav_keys_logical(uint32_t notes) {
    uint32_t logical=0;
    for(unsigned k=0;k<27;k++)if(notes&(1u<<k)) {
        int w=bb_panel_white_step(k),b=bb_panel_black_index(k);
        logical|=w>=0?1u<<w:1u<<(16+b);
    }
    return logical;
}
static void bb_nav_map_init(void) {
    for(unsigned k=0;k<27;k++) {
        int w=bb_panel_white_step(k),b=bb_panel_black_index(k);
        bb_nav_phys[w>=0?(unsigned)w:16u+(unsigned)b]=(uint8_t)k;
    }
}
static unsigned bb_nav_is_drum(void){return bb_nav_row<2u;}
static bb_page bb_nav_face_page(void){return bb_nav_is_drum()?BB_DRUMS:BB_DILLO;}

/* ------------------------------------------------------------ focus */
static void bb_nav_focus_apply(void) {
    bb_face.requested.page=bb_nav_face_page();
    if(bb_nav_is_drum())bb_face.machine=bb_nav_row;
    hm_focus.selected=bb_nav_row;
    unsigned instrument=BB_NAV_INSTR_DILLO,banks=1;
    if(bb_nav_is_drum()) {
        instrument=bb_kit_family[bb_nav_row]==1?BB_NAV_INSTR_DRUM_909:BB_NAV_INSTR_DRUM;
        if(instrument==BB_NAV_INSTR_DRUM_909)banks=(bb909_control_count(bb_face.voice)+3u)/4u;
    }
    bb_nav_set_instrument(&bb_nav,(uint8_t)instrument,(uint8_t)(banks?banks:1u));
    bb_nav_set_focus(&bb_nav,bb_nav_row);
    bb_nav_set_voice(&bb_nav,bb_face.voice);
}
static void bb_nav_focus_move(int delta) {
    unsigned row=bb_nav_row;
    (void)st_select(&row,delta);
    if(row==bb_nav_row)return;
    bb_nav_row=(uint8_t)row;
    bb_face.bank=0;
    bb_nav_focus_apply();
    bb_dirty=1;
}

/* ------------------------------------------------------------ Dillo notes */
/* One owner of live-key note state (replaces bb_keys_sync for the panel). A
 * refused note-off stays owed and is retried before any new input. */
static int bb_nav_note_bit(const uint32_t *map,unsigned note){return (map[note>>5]>>(note&31u))&1u;}
static void bb_nav_note_set(uint32_t *map,unsigned note,int on){
    if(on)map[note>>5]|=1u<<(note&31u);else map[note>>5]&=~(1u<<(note&31u));
}
static int bb_nav_note_push(unsigned note,unsigned velocity) {
    bb_command c={0};c.type=BB_CMD_SYNTH_KEY;c.target=0;c.voice=(uint8_t)note;c.value=(uint16_t)velocity;
    return bb_command_push(&bb_sound.commands,&c);
}
static void bb_nav_note_lit(unsigned note,int on) {
    unsigned k=bb_nav_note_phys[note];
    if(k<27){if(on)bb_keys_sent[0]|=1u<<k;else bb_keys_sent[0]&=~(1u<<k);}
    bb_dirty=1;
}
static void bb_nav_note_off(unsigned note) {
    if(note>127||!bb_nav_note_bit(bb_nav_note_on,note))return;
    if(bb_nav_note_push(note,0)){bb_nav_note_set(bb_nav_note_on,note,0);bb_nav_note_set(bb_nav_note_owed,note,0);bb_nav_note_lit(note,0);}
    else {bb_nav_note_set(bb_nav_note_owed,note,1);bb_say("KEY RELEASE WAIT");}
}
static void bb_nav_note_on_key(unsigned note,unsigned velocity,unsigned phys) {
    if(note>127)return;
    if(bb_face.requested.synths[0].muted)return;          /* as bb_keys_sync */
#if BB_WITH_SESSION
    if(bb_session_busy())return;                          /* no producer writes during scene hold */
#endif
    if(bb_nav_note_bit(bb_nav_note_on,note))bb_nav_note_off(note);  /* same pitch twice: re-trigger */
    if(bb_nav_note_bit(bb_nav_note_on,note))return;
    if(!bb_nav_note_push(note,velocity)){bb_say("KEYS BUSY / RETRY");return;}
    bb_nav_note_set(bb_nav_note_on,note,1);bb_nav_note_phys[note]=(uint8_t)phys;bb_nav_note_lit(note,1);
}
static void bb_nav_notes_retry(void) {
    for(unsigned w=0;w<4;w++)while(bb_nav_note_owed[w]) {
        unsigned note=w*32u+(unsigned)__builtin_ctz(bb_nav_note_owed[w]);
        if(!bb_nav_note_push(note,0))return;
        bb_nav_note_set(bb_nav_note_on,note,0);bb_nav_note_set(bb_nav_note_owed,note,0);bb_nav_note_lit(note,0);
    }
}
static void bb_nav_notes_all_off(void) {
    for(unsigned n=0;n<128;n++)if(bb_nav_note_bit(bb_nav_note_on,n))bb_nav_note_off(n);
}

/* ------------------------------------------------------------ sections */
static bb_player *bb_nav_player(void){return bb_library_current();}
static unsigned bb_nav_section_count(const bb_player *p){return p&&p->ready?(unsigned)bb_browser_part_count(&p->view):0u;}
/* The playing section of the focused instrument, on the bank being viewed. */
static unsigned bb_nav_section_playing(const bb_player *p) {
    if(!p||!p->ready||!p->playing.ticket)return BB_NAV_NONE_IDX;
    if(p->playing.choice.group!=p->view.group||p->playing.choice.bank_index!=p->view.bank_index)return BB_NAV_NONE_IDX;
    return p->playing.choice.part_index<255u?p->playing.choice.part_index:BB_NAV_NONE_IDX;
}
static int bb_nav_part_page_to(bb_player *p,unsigned page) {
    int delta=(int)page-(int)p->view.part_page;
    if(delta)(void)bb_browser_move_part_page(&p->view,delta);
    return p->view.part_page==page;
}
/* Launch section `index` (0-based) of the focused instrument's pattern at
 * the next boundary, through the existing part routes: PATTERN = preview +
 * confirm (bb_library_input), elsewhere = bb_player_part_key (hm_variation). */
static void bb_nav_section_launch(unsigned index) {
    static char text[24];
    bb_player *p=bb_nav_player();
    if(!p->ready){bb_say("LIBRARY NOT READY");return;}
    if(bb_nav.page!=BB_NAV_PATTERN && p->playing.ticket &&
       (p->view.group!=p->playing.choice.group||p->view.bank_index!=p->playing.choice.bank_index)) {
        /* Sections belong to what is playing, not to a browsed card. */
        if(p->view.group!=p->playing.choice.group)(void)bb_player_group(p,p->playing.choice.group);
        int delta=(int)p->playing.choice.bank_index-(int)p->view.bank_index;
        if(delta)(void)bb_browser_move_bank(&p->view,delta);
    }
    unsigned count=bb_nav_section_count(p);
    if(index>=count){bb_say("NO SUCH SECTION");return;}
    if(!bb_nav_part_page_to(p,index/BB_BROWSER_PART_KEYS)){bb_library_result(BB_FACTORY_PATTERN_INVALID);return;}
    unsigned key=index%BB_BROWSER_PART_KEYS;int result;
    if(bb_nav.page==BB_NAV_PATTERN)
        result=bb_library_preview_part_key(p,key)?bb_library_confirm(p):BB_FACTORY_PATTERN_INVALID;
    else result=bb_player_part_key(p,&bb_sound,key);
    if(result!=BB_FACTORY_PATTERN_OK){bb_library_result(result);return;}
    /* "SECTION 12 QUEUED" / "SECTION 12 REQUESTED" */
    unsigned n=0,v=index+1u;const char *head="SECTION ";
    while(*head)text[n++]=*head++;
    if(v>=10u)text[n++]=(char)('0'+v/10u);
    text[n++]=(char)('0'+v%10u);
    head=bb_face.requested.running?" QUEUED":" REQUESTED";
    while(*head)text[n++]=*head++;
    text[n]=0;bb_say(text);
}
static void bb_nav_section_step(int delta) {
    bb_player *p=bb_nav_player();
    unsigned count=bb_nav_section_count(p),now=bb_nav_section_playing(p);
    if(!count){bb_say("LIBRARY NOT READY");return;}
    if(now==BB_NAV_NONE_IDX)now=p->view.part_index<count?p->view.part_index:0u;
    int next=(int)now+bb_limit(delta,-32,32);
    if(next<0)next=0;
    if(next>=(int)count)next=(int)count-1;
    if((unsigned)next==now&&bb_nav_section_playing(p)==now){bb_say(delta<0?"FIRST SECTION":"LAST SECTION");return;}
    bb_nav_section_launch((unsigned)next);
}

/* ALGORITHM browses one list: the ERAS, then the SONGS (the former
 * REC+PRESETS family switch). Moving past either end continues in the other
 * family, with the existing kit-aware preview helpers. Cursor only. */
static int bb_nav_pattern_move(bb_player *p,int delta) {
    int moved=0,dir=delta<0?-1:1;
    unsigned steps=(unsigned)(delta<0?-delta:delta);
    if(steps>32u)steps=32u;
    while(steps--) {
        unsigned count=(unsigned)bb_browser_bank_count(&p->view),g=p->view.group;
        if(count&&dir>0&&p->view.bank_index+1u>=count) {
            if(!bb_library_preview_group(p,g^1u))break;
        } else if(count&&dir<0&&p->view.bank_index==0u) {
            if(!bb_library_preview_group(p,g^1u))break;
            (void)bb_library_preview_move(p,-1);
        } else if(!bb_library_preview_move(p,dir))break;
        moved=1;
    }
    return moved;
}

/* ------------------------------------------------------------ facts */
static uint16_t bb_nav_step_mask(void) {
    uint16_t mask=0;
    if(bb_nav_is_drum()) {
        const bb_machine *m=&bb_face.requested.drums[bb_face.machine];
        for(unsigned k=0;k<16;k++) {
            unsigned step=bb_face.bank*16u+k;
#if BB_WITH_RICH_DRUM_BROWSER
            if(bb_rich_active()) {
                bb_phrase_cell cell={0};
                if(bb_rich_cell(step,&cell)&&cell.hits)mask|=(uint16_t)(1u<<k);
                continue;
            }
#endif
            if(step<m->length&&(m->steps[step].hits>>bb_face.voice)&1u)mask|=(uint16_t)(1u<<k);
        }
    } else {
        const bb_sequence *seq=&bb_face.requested.synths[0];
        for(unsigned k=0;k<16;k++) {
            unsigned step=bb_face.bank*16u+k;
            if(step>=seq->length)continue;
            const bb_note *note=&seq->notes[step];
            int on=bb_face.requested.edit==BB_ACCENT?note->accent:bb_face.requested.edit==BB_SLIDE?note->slide:note->gate;
            if(on)mask|=(uint16_t)(1u<<k);
        }
    }
    return mask;
}
static unsigned bb_nav_length(void) {
    return bb_nav_is_drum()?bb_drum_visible_length(&bb_face.requested.drums[bb_face.machine]):
        bb_face.requested.synths[0].length;
}
static unsigned bb_nav_instr_muted(void) {
    return (!!bb_face.requested.drums[0].muted||!bb_face.requested.drums[0].enabled) |
           ((!!bb_face.requested.drums[1].muted||!bb_face.requested.drums[1].enabled)<<1) |
           ((!!bb_face.requested.synths[0].muted)<<2);
}
static void bb_nav_fill_facts(bb_nav_facts *f) {
    memset(f,0,sizeof *f);
    f->kit_slot=f->kit_slot_queued=f->section_queued=BB_NAV_NONE_IDX;
    f->list_cursor=f->list_playing=f->list_queued=BB_NAV_NONE_IDX;
    f->section=BB_NAV_NONE_IDX;
    f->running=bb_face.requested.running;
#if BB_WITH_SESSION
    f->saving=(uint8_t)!!bb_session_busy();
    f->slot=bb_scene_slot;
#endif
    bb_player *p=bb_nav_player();
    unsigned pending=p->ready?bb_player_pending(p):0u;
    unsigned count=bb_nav_section_count(p);
    f->section_count=(uint8_t)(count>255u?255u:count);
    f->section=(uint8_t)bb_nav_section_playing(p);
    if(pending) {
        const bb_factory_choice *rq=&p->request.choice;
        if(p->playing.ticket&&rq->group==p->playing.choice.group&&rq->bank_index==p->playing.choice.bank_index&&
           rq->part_index!=p->playing.choice.part_index) {
            f->queued|=BB_NAV_Q_SECTION;f->section_queued=(uint8_t)(rq->part_index<255u?rq->part_index:255u);
        } else f->queued|=BB_NAV_Q_PATTERN;
    }
    if(bb_sound_pick.pending&&bb_sound_pick.page==bb_nav_face_page()&&
       (!bb_nav_is_drum()||bb_sound_pick.machine==bb_face.machine))f->queued|=BB_NAV_Q_KIT;
    if(bb_nav.page==BB_NAV_KIT && bb_sound_pick_visible()) {
        unsigned items=bb_sound_picker_items(&bb_sound_pick);
        bb_sound_picker_view view;
        f->list_count=(uint8_t)(items>16u?16u:items);
        f->list_cursor=bb_sound_pick.cursor<16u?bb_sound_pick.cursor:BB_NAV_NONE_IDX;
        if(!bb_sound_pick_audio_failed()&&bb_sound_picker_observe(&bb_sound_pick,&bb_sound,&view)&&
           view.active_index>=0&&view.active_index<16)f->list_playing=(uint8_t)view.active_index;
        if(bb_sound_pick.pending&&bb_sound_pick.requested<16u)f->list_queued=bb_sound_pick.requested;
    } else if(bb_nav.page==BB_NAV_PATTERN && p->ready) {
        unsigned first=p->view.bank_page*BB_BROWSER_BANK_KEYS,n=0;
        for(unsigned k=0;k<BB_BROWSER_BANK_KEYS;k++)if(bb_browser_bank_cell(&p->view,k)!=BB_CATALOGUE_NONE)n=k+1u;
        f->list_count=(uint8_t)n;
        if(p->view.bank_index>=first&&p->view.bank_index<first+16u)f->list_cursor=(uint8_t)(p->view.bank_index-first);
        if(p->playing.ticket&&p->playing.choice.group==p->view.group&&
           p->playing.choice.bank_index>=first&&p->playing.choice.bank_index<first+16u)
            f->list_playing=(uint8_t)(p->playing.choice.bank_index-first);
        if(pending&&p->request.choice.group==p->view.group&&
           p->request.choice.bank_index>=first&&p->request.choice.bank_index<first+16u)
            f->list_queued=(uint8_t)(p->request.choice.bank_index-first);
    }
    f->instr_muted=(uint8_t)bb_nav_instr_muted();
    f->instr_solo=bb_nav_isolo;
    if(bb_nav_is_drum()) {
        bb_group_view view;
        if(bb_group_input(&bb_sound.commands,bb_sound.groups,bb_face.machine,BB_GROUP_READ,0,0,&view)!=BB_GROUP_REFUSED)
            f->group_muted=view.muted;
        f->group_solo=bb_nav_gsolo[bb_face.machine];
    }
    f->swing_on=bb_swing_x2(bb_face.requested.swing)>100u;
#if BB_WITH_EVOLVE
    f->evolve_on=bb_evo_view.enabled&&!bb_evo_view.frozen;
#endif
    unsigned length=bb_nav_length();
    f->step_bank=bb_face.bank;
    f->step_banks=(uint8_t)(length?(length+15u)/16u:1u);
    f->steps=bb_nav_step_mask();
    f->display=(uint8_t)((bb_face.animate?BB_NAV_DISP_ANIMATION:0)|(bb_scope_enabled?BB_NAV_DISP_SCOPE:0)|
                         (bb_nav_meters?BB_NAV_DISP_METERS:0));
}

/* ------------------------------------------------------------ projection */
/* The legacy face flags, derived from bb_nav. The renderer, LED and helper
 * predicates read these; nothing else writes them after a scan. */
static void bb_nav_project(void) {
    uint8_t page=bb_nav.page,bank=bb_nav.bank;
    int live=bb_nav.play_keys==BB_NAV_PLAY_LIVE;
    bb_nav_focus_apply();
    bb_mix_page=0;bb_diag_page=0;bb_tone_page=0;bb_drum_sound_page=0;bb_drum_color_page=0;bb_kit_page=0;
    bb_pad_page=1;bb_keys_page[0]=bb_keys_page[1]=1;
#if BB_WITH_BROWSER
    bb_library_page=0;
#endif
#if BB_WITH_SESSION
    bb_scene_page=0;
#endif
#if BB_WITH_EVOLVE
    bb_evo_page=0;
#endif
#if BB_WITH_EFFECTS
    bb_send_page=bb_effect_page=0;
#endif
    bb_sound_pick_return=0;
    int pick=0;
    switch(page) {
    case BB_NAV_STUDIO: bb_mix_page=BB_MIX_DIRECT_PAGE;break;
    case BB_NAV_PLAY:
        if(bb_nav_is_drum())bb_pad_page=(uint8_t)live;else bb_keys_page[0]=(uint8_t)live;
        break;
    case BB_NAV_KIT: pick=1;break;
    case BB_NAV_PATTERN:
#if BB_WITH_BROWSER
        bb_library_page=1;
#endif
        break;
    case BB_NAV_SOUND:
        if(bb_nav_is_drum()){bb_drum_sound_page=1;bb_drum_color_page=(uint8_t)(bank&1u);bb_ui_909_page=bank;}
        else bb_tone_page=1;
        break;
    case BB_NAV_FX:
#if BB_WITH_EFFECTS
        if(bank<4u){bb_effect_page=1;bb_effect_bank=bank;}else bb_send_page=1;
#endif
        break;
    case BB_NAV_SAVE:
#if BB_WITH_SESSION
        bb_scene_page=1;
#endif
        break;
    case BB_NAV_EVOLVE:
#if BB_WITH_EVOLVE
        bb_evo_page=1;
#endif
        break;
    default: break;  /* GLOBAL: own face (bb_nav_global_draw) */
    }
    if(pick) {
        /* (Re)open the chooser on entry or a focus change, as the old
         * PRESETS entrance did; never while a request is still owed. */
        if(bb_nav_shown_page!=BB_NAV_KIT||bb_nav_shown_row!=bb_nav_row||!bb_sound_pick.ready||
           bb_sound_pick.page!=bb_face.requested.page||(bb_nav_is_drum()&&bb_sound_pick.machine!=bb_face.machine)) {
            if(!bb_sound_pick.pending&&!bb_sound_pick_audio_failed()&&
               !bb_sound_pick_open(bb_face.requested.page,bb_face.machine))bb_say("SOUND LIST NOT READY");
        }
        bb_sound_pick_page=1;
    } else bb_sound_pick_page=0;
    /* Keyboard picture for the existing Dillo face: bottom key and the
     * GLOBAL transpose (bb_pattern_shift) are separate (OCT = keys only). */
    bb_keys_base[0]=(uint8_t)(41+12*bb_nav.key_octave);
    if(page!=bb_nav_shown_page||bb_nav_row!=bb_nav_shown_row)bb_dirty=1;
    bb_nav_shown_page=page;bb_nav_shown_row=bb_nav_row;
    bb_help_visible=bb_nav.help;
}
/* Firmware-forced page change; owed releases are performed at once. */
static void bb_nav_do_keys(const bb_nav_out *o);
static void bb_nav_force(uint8_t page) {
    bb_nav_goto(&bb_nav,page,&bb_nav_aux_o);
    bb_nav_do_keys(&bb_nav_aux_o);   /* releases only: never re-enters here */
    if(bb_nav_aux_o.message)bb_say(bb_nav_message_text(bb_nav_aux_o.message));
    bb_nav_project();
}

/* ------------------------------------------------------------ knobs */
static int bb_nav_drum_param(unsigned role) {
    switch(role) {
    case BB_NAV_R_VOICE_LEVEL: return BB_DRUM_LEVEL;
    case BB_NAV_R_TUNE: return BB_DRUM_TUNE;
    case BB_NAV_R_DECAY: return BB_DRUM_DECAY;
    case BB_NAV_R_TONE: return BB_DRUM_TONE;
    case BB_NAV_R_DRIVE: return BB_DRUM_DRIVE;
    case BB_NAV_R_ATTACK: return BB_DRUM_ATTACK;
    case BB_NAV_R_DIST: return BB_DRUM_DIST;
    case BB_NAV_R_SNAPPY: return BB_DRUM_SNAPPY;
    case BB_NAV_R_MODE: return BB_DRUM_MODE;
    case BB_NAV_R_SEND_ECHO: return BB_DRUM_ECHO;
    case BB_NAV_R_SEND_SPRING: return BB_DRUM_SPRING;
    default: return -1;
    }
}
static int bb_nav_synth_param(unsigned role) {
    switch(role) {
    case BB_NAV_R_SYNTH_LEVEL: return BB_UI_LEVEL;
    case BB_NAV_R_CUTOFF: return BB_PARAM_CUTOFF;
    case BB_NAV_R_RESO: return BB_PARAM_RESONANCE;
    case BB_NAV_R_ENV: return BB_PARAM_ENVELOPE;
    case BB_NAV_R_SYNTH_DECAY: return BB_PARAM_DECAY;
    case BB_NAV_R_ACCENT: return BB_PARAM_ACCENT;
    case BB_NAV_R_SYNTH_DRIVE: return BB_PARAM_DRIVE;
    case BB_NAV_R_SLIDE: return BB_PARAM_SLIDE_MS;
    case BB_NAV_R_WAVE: return BB_PARAM_WAVE;
    case BB_NAV_R_SQUARE: return BB_PARAM_SQUARE;
    case BB_NAV_R_SAW: return BB_PARAM_SAW;
    case BB_NAV_R_SUB: return BB_PARAM_SUB;
    case BB_NAV_R_MOD: return BB_PARAM_MODULATION;
    case BB_NAV_R_LFO: return BB_PARAM_LFO_RATE;
    case BB_NAV_R_GLIDE: return BB_PARAM_GLIDE;
    default: return -1;
    }
}
#if BB_WITH_EFFECTS
static int bb_nav_fx_param(unsigned role) {
    switch(role) {
    case BB_NAV_R_ECHO_TIME: return BB_FX_ECHO_MS;
    case BB_NAV_R_ECHO_FDBK: return BB_FX_ECHO_FEEDBACK;
    case BB_NAV_R_ECHO_HEADS: return BB_FX_ECHO_HEADS;
    case BB_NAV_R_ECHO_WET: return BB_FX_ECHO_WET;
    case BB_NAV_R_ECHO_TONE: return BB_FX_ECHO_TONE;
    case BB_NAV_R_ECHO_DRIVE: return BB_FX_ECHO_DRIVE;
    case BB_NAV_R_ECHO_WEAR: return BB_FX_ECHO_WEAR;
    case BB_NAV_R_SPRING_SIZE: return BB_FX_SPRING_SIZE;
    case BB_NAV_R_SPRING_DAMP: return BB_FX_SPRING_DAMP;
    case BB_NAV_R_SPRING_WET: return BB_FX_SPRING_WET;
    case BB_NAV_R_ECHO_MOTION: return BB_FX_ECHO_MOTION;
    default: return -1;
    }
}
static void bb_nav_fx_step(unsigned p,int delta) {
    unsigned lo,hi;
    bb_fx_parameter_limits(p,&lo,&hi);
    int scale=p==BB_FX_ECHO_MS?5:1;   /* as bb_effect_input */
    bb_send_effect_parameter(p,(unsigned)bb_limit((int)bb_fx_parameter_get(&bb_face.requested,p)+
        bb_limit(delta,-1000,1000)*scale,(int)lo,(int)hi));
}
/* SENDS bank of a sample layer: the bb_sample_sends_input body, with the
 * detents bb_nav routed (it no longer reads encoders itself). */
#if BB_SAMPLE_HOST_INTEGRATION
static int bb_nav_sample_send(unsigned k,int delta) {
    bb_sample_status view;int route=bb_sample_sends_view(&view);
    if(!route)return 0;
    bb_dirty=1;
    if(route<0){bb_say("SAMPLE STATE WAIT");return 1;}
    if(!view.queue_idle||view.stop_pending){bb_say("MIX BUSY - RETRY");return 1;}
    unsigned value=(unsigned)bb_limit((int)bb_sample_sends_value(&view,k)+bb_limit(delta,-127,127),k==3?1:0,k==3?100:127);
    if(k==2){bb_send_machine(view.machine,BB_CMD_DRUM_LEVEL,value);return 1;}
    bb_command command;
    unsigned parameter=k==3?BB_SAMPLE_MIX_GAIN:k;
    if(!bb_audio_sample_mix_command(&view,parameter,value,&command)||
       !bb_command_push(&bb_sound.commands,&command)){bb_say("MIX BUSY - RETRY");return 1;}
    bb_say("SAMPLE MIX REQUESTED");return 1;
}
#endif
#endif
static void bb_nav_level(unsigned type,unsigned target,unsigned current,int delta) {
    bb_send_machine(target,type,(unsigned)bb_limit((int)current+bb_limit(delta,-127,127),0,127));
}
static void bb_nav_knob(unsigned role,unsigned index,int delta) {
    bb_machine *m=&bb_face.requested.drums[bb_face.machine];
    int p;
    switch(role) {
    case BB_NAV_R_GROUP_KICK: case BB_NAV_R_GROUP_SNCL: case BB_NAV_R_GROUP_HATS: case BB_NAV_R_GROUP_PERC: {
        bb_group_view view;   /* bb_group_knobs_input body */
        int result=bb_group_input(&bb_sound.commands,bb_sound.groups,bb_face.machine,BB_GROUP_TURN,
                                  role-BB_NAV_R_GROUP_KICK,delta,&view);
        bb_dirty=1;
        if(result==BB_GROUP_REFUSED){bb_face.rejected++;bb_say("GROUP BUSY - RETRY");}
        else bb_face.rejected=0;
        return;
    }
    case BB_NAV_R_LAYER_LEVEL: bb_nav_level(BB_CMD_DRUM_LEVEL,bb_face.machine,m->level,delta);return;
    case BB_NAV_R_MIX_DRUM_A: bb_nav_level(BB_CMD_DRUM_LEVEL,0,bb_face.requested.drums[0].level,delta);return;
    case BB_NAV_R_MIX_DRUM_B: bb_nav_level(BB_CMD_DRUM_LEVEL,1,bb_face.requested.drums[1].level,delta);return;
    case BB_NAV_R_MIX_DILLO: bb_nav_level(BB_CMD_BAND_LEVEL,BB_MIX_DILLO,bb_face.requested.band_level[BB_MIX_DILLO],delta);return;
    case BB_NAV_R_SWING:
        bb_send(BB_CMD_SWING_X2,(unsigned)bb_limit((int)bb_swing_x2(bb_face.requested.swing)+bb_limit(delta,-50,50),100,150));
        return;
    case BB_NAV_R_BPM:
        bb_send(BB_CMD_TEMPO,(unsigned)bb_limit((int)bb_face.requested.bpm+bb_limit(delta,-270,270),30,300));return;
    case BB_NAV_R_AB_BALANCE: {
        /* Opposite-direction level pair: right = more DRUM B. */
        int d=bb_limit(delta,-127,127);
        bb_nav_level(BB_CMD_DRUM_LEVEL,0,bb_face.requested.drums[0].level,-d);
        bb_nav_level(BB_CMD_DRUM_LEVEL,1,bb_face.requested.drums[1].level,d);
        return;
    }
    case BB_NAV_R_TRANSPOSE: {
        /* Dillo pattern transpose, the command OCT used to send. */
        int wanted=bb_limit((int)bb_pattern_shift[0]+bb_limit(delta,-24,24),-BB_TRANSPOSE_LIMIT,BB_TRANSPOSE_LIMIT);
        if(wanted==bb_pattern_shift[0]){bb_say("TRANSPOSE LIMIT");return;}
        bb_command c={0};c.type=BB_CMD_SYNTH_TRANSPOSE;c.target=0;c.value=(uint16_t)(wanted+BB_TRANSPOSE_LIMIT);
        if(bb_command_push(&bb_sound.commands,&c)){bb_pattern_shift[0]=(int8_t)wanted;bb_dirty=1;}
        else bb_say("BUSY - RETRY");
        return;
    }
#if BB_WITH_EFFECTS
    case BB_NAV_R_FX_RETURN: {
        /* Master return: echo and spring wet move together. */
        bb_nav_fx_step(BB_FX_ECHO_WET,delta);bb_nav_fx_step(BB_FX_SPRING_WET,delta);return;
    }
#endif
    case BB_NAV_R_909_CONTROL: (void)bb_ui_909_knob(index%4u,delta);return;
    case BB_NAV_R_SLOT:
#if BB_WITH_SESSION
        bb_scene_slot=(uint8_t)bb_limit((int)bb_scene_slot+bb_limit(delta,-3,3),0,3);bb_dirty=1;
#endif
        return;
#if BB_WITH_EVOLVE
    case BB_NAV_R_EVO_AMOUNT: bb_evo_send(BB_EV_AMOUNT,(unsigned)bb_limit((int)bb_evo_view.settings.amount+bb_limit(delta,-100,100),0,100),0,0);return;
    case BB_NAV_R_EVO_EVERY: bb_evo_send(BB_EV_INTERVAL,(unsigned)bb_limit((int)bb_evo_view.settings.every_phrases+bb_limit(delta,-64,64),1,64),0,0);return;
    case BB_NAV_R_EVO_ROOT: bb_evo_send(BB_EV_ROOT,(unsigned)bb_limit((int)bb_evo_view.settings.root+bb_limit(delta,-12,12),0,11),0,0);return;
    case BB_NAV_R_EVO_SCALE: bb_evo_send(BB_EV_SCALE,delta>0?0x5ad:0xab5,0,0);return;
#endif
    default: break;
    }
#if BB_WITH_EFFECTS
    if((p=bb_nav_fx_param(role))>=0){bb_nav_fx_step((unsigned)p,delta);return;}
    if(bb_nav.page==BB_NAV_FX && (role==BB_NAV_R_SEND_ECHO||role==BB_NAV_R_SEND_SPRING||
       role==BB_NAV_R_VOICE_LEVEL||role==BB_NAV_R_DRIVE)) {
        /* SENDS bank: bb_effect_input's send branch. */
        if(bb_nav_is_drum()) {
#if BB_SAMPLE_HOST_INTEGRATION
            static const uint8_t slot[]={0,1,2,3};
            unsigned k=role==BB_NAV_R_SEND_ECHO?slot[0]:role==BB_NAV_R_SEND_SPRING?slot[1]:role==BB_NAV_R_VOICE_LEVEL?slot[2]:slot[3];
            if(bb_nav_sample_send(k,delta))return;
#endif
            p=bb_nav_drum_param(role);
            bb_send_drum_parameter((unsigned)p,(unsigned)bb_limit((int)bb_visible_drum_value(&m->sound,(unsigned)p)+bb_limit(delta,-127,127),0,127));
        } else if(role==BB_NAV_R_SEND_ECHO||role==BB_NAV_R_SEND_SPRING) {
            unsigned q=BB_FX_DILLO_ECHO+(role==BB_NAV_R_SEND_SPRING);
            bb_send_effect_parameter(q,(unsigned)bb_limit((int)bb_fx_parameter_get(&bb_face.requested,q)+bb_limit(delta,-127,127),0,127));
        }
        return;
    }
#endif
    if((p=bb_nav_drum_param(role))>=0 && bb_nav_is_drum()) {
        if(bb_nav.page==BB_NAV_SOUND) {
            /* Voice editor: the existing clamp to the voice's own range. */
            int max=bb_drum_parameter_max(&m->sound,bb_face.voice,(unsigned)p);
            if(max<0){bb_say("NO CONTROL ON THIS VOICE");return;}
            bb_send_drum_parameter((unsigned)p,(unsigned)bb_limit((int)bb_drum_parameter_get(&m->sound,bb_face.voice,(unsigned)p)+bb_limit(delta,-127,127),0,max));
        } else bb_send_drum_parameter((unsigned)p,(unsigned)bb_limit((int)bb_visible_drum_value(&m->sound,(unsigned)p)+bb_limit(delta,-127,127),0,127));
        return;
    }
    if((p=bb_nav_synth_param(role))>=0 && !bb_nav_is_drum()) {
        if(p==BB_UI_LEVEL){bb_nav_level(BB_CMD_BAND_LEVEL,1u,bb_face.requested.band_level[1],delta);return;}
        if(p==BB_PARAM_WAVE) {
            unsigned wave=delta>0;   /* right = SAW, left = SQUARE (was SCL) */
            if(wave==bb_face.requested.dillo.wave)return;
            if(bb_send_synth(0,BB_CMD_SYNTH_PARAMETER,wave,BB_PARAM_WAVE)){bb_patch_edited[0]=1;bb_say(wave?"DILLO: SAW":"DILLO: SQUARE");}
            return;
        }
        unsigned lo,hi;   /* bb_synth_knobs_input body */
        int scale=p==BB_PARAM_CUTOFF?50:p==BB_PARAM_RESONANCE?25:p==BB_PARAM_LFO_RATE?10:1;
        bb_synth_parameter_limits(0,(unsigned)p,&lo,&hi);
        unsigned value=(unsigned)bb_limit((int)bb_synth_parameter_get(&bb_face.requested,0,(unsigned)p)+bb_limit(delta,-20000,20000)*scale,(int)lo,(int)hi);
        if(bb_send_synth(0,BB_CMD_SYNTH_PARAMETER,value,(unsigned)p))bb_patch_edited[0]=1;
        return;
    }
}

/* ------------------------------------------------------------ mutes/solos */
static void bb_nav_group_toggle(unsigned group) {
    bb_group_view view;   /* bb_group_mutes_input body */
    int result=bb_group_input(&bb_sound.commands,bb_sound.groups,bb_face.machine,BB_GROUP_TOGGLE,group,0,&view);
    bb_dirty=1;
    if(result==BB_GROUP_REFUSED){bb_face.rejected++;bb_say("GROUP BUSY - RETRY");}
    else bb_face.rejected=0;
}
static unsigned bb_nav_group_muted(void) {
    bb_group_view view;
    return bb_group_input(&bb_sound.commands,bb_sound.groups,bb_face.machine,BB_GROUP_READ,0,0,&view)!=BB_GROUP_REFUSED?view.muted:0u;
}
/* Solo = mute every other group; un-solo unmutes exactly the groups the
 * solo muted. Plain mute while soloed ends the solo bookkeeping. */
static void bb_nav_group_solo(unsigned group) {
    unsigned machine=bb_face.machine,muted=bb_nav_group_muted(),bit=1u<<group;
    if(bb_nav_gsolo[machine]==bit) {
        for(unsigned g=0;g<GG_COUNT;g++)if((bb_nav_gsolo_muted[machine]>>g)&1u&&(muted>>g)&1u)bb_nav_group_toggle(g);
        bb_nav_gsolo[machine]=0;bb_nav_gsolo_muted[machine]=0;bb_say("SOLO OFF");return;
    }
    if(bb_nav_gsolo[machine]) {   /* move the solo: restore first */
        for(unsigned g=0;g<GG_COUNT;g++)if((bb_nav_gsolo_muted[machine]>>g)&1u&&(muted>>g)&1u)bb_nav_group_toggle(g);
        muted&=~bb_nav_gsolo_muted[machine];
    }
    bb_nav_gsolo_muted[machine]=0;
    if(muted&bit){bb_nav_group_toggle(group);}
    for(unsigned g=0;g<GG_COUNT;g++)if(g!=group&&!((muted>>g)&1u)){bb_nav_group_toggle(g);bb_nav_gsolo_muted[machine]|=(uint8_t)(1u<<g);}
    bb_nav_gsolo[machine]=(uint8_t)bit;bb_say("GROUP SOLO / REC+KEY: OFF");
}
/* Instrument mute: hm_native_input's row loop (enable an off layer, else
 * toggle mute). Instrument 3 = both drums (KILL ALL DRUMS). */
static void bb_nav_instr_toggle(unsigned row) {
    if(row<2) {
        if(!bb_face.requested.drums[row].enabled)bb_send_machine(row,BB_CMD_DRUM_ENABLE,1);
        else bb_send_machine(row,BB_CMD_DRUM_MUTE,!bb_face.requested.drums[row].muted);
    } else bb_send_machine(0,BB_CMD_SYNTH_MUTE,!bb_face.requested.synths[0].muted);
}
static void bb_nav_instr_mute(unsigned row) {
    if(row==3) {   /* ALL DRUMS: both follow A's audible state */
        unsigned mute=!(bb_nav_instr_muted()&3u);
        for(unsigned r=0;r<2;r++)if(((bb_nav_instr_muted()>>r)&1u)!=mute)bb_send_machine(r,BB_CMD_DRUM_MUTE,mute);
        return;
    }
    if(row>2)return;
    bb_nav_isolo&=(uint8_t)~(1u<<row);
    bb_nav_instr_toggle(row);
}
static void bb_nav_instr_solo(unsigned row) {
    unsigned muted=bb_nav_instr_muted(),bit=1u<<row;
    if(row>2)return;
    if(bb_nav_isolo==bit) {
        for(unsigned r=0;r<3;r++)if((bb_nav_isolo_muted>>r)&1u&&(muted>>r)&1u)bb_nav_instr_toggle(r);
        bb_nav_isolo=0;bb_nav_isolo_muted=0;bb_say("SOLO OFF");return;
    }
    if(bb_nav_isolo) {
        for(unsigned r=0;r<3;r++)if((bb_nav_isolo_muted>>r)&1u&&(muted>>r)&1u)bb_nav_instr_toggle(r);
        muted&=~bb_nav_isolo_muted;
    }
    bb_nav_isolo_muted=0;
    if(muted&bit)bb_nav_instr_toggle(row);
    for(unsigned r=0;r<3;r++)if(r!=row&&!((muted>>r)&1u)){bb_nav_instr_toggle(r);bb_nav_isolo_muted|=(uint8_t)(1u<<r);}
    bb_nav_isolo=(uint8_t)bit;bb_say("SOLO / REC+KEY: OFF");
}

/* ------------------------------------------------------------ kit / pattern */
/* Returns 1 when the choice was taken (requested, already loaded, or the
 * kit intent of a sample layer): bb_nav then continues to PATTERN. */
static int bb_nav_kit_commit(void) {
    if(!bb_sound_pick_visible()){bb_say("SOUND LIST NOT READY");return 0;}
    return bb_sound_pick_commit_result();
}
static void bb_nav_kit_chosen(void) {
    bb_nav_kit_confirmed(&bb_nav,&bb_nav_aux_o);
    bb_nav_do_keys(&bb_nav_aux_o);   /* releases only: never re-enters here */
    if(bb_nav_aux_o.message)bb_say(bb_nav_message_text(bb_nav_aux_o.message));
    bb_nav_project();
}
static int bb_nav_pattern_is_playing(const bb_player *p) {
    return p->playing.ticket&&p->playing.choice.group==p->view.group&&
        p->playing.choice.bank_index==p->view.bank_index&&p->playing.choice.part_index==p->view.part_index;
}
/* PLAY. On PATTERN while stopped: the highlighted pattern is the one that
 * starts (confirm, then START), with the queue room check of the old first
 * PLAY. Elsewhere: start/stop exactly as before. */
static void bb_nav_transport(int pattern_play) {
    int start=!bb_face.requested.running;
    bb_player *p=bb_nav_player();
    if(start&&pattern_play&&p->ready) {
        int intent=bb_nav_is_drum()&&bb_face.machine<BB_MACHINES&&bb_kit_intent[bb_face.machine].valid;
        if(intent||!bb_nav_pattern_is_playing(p)) {
            uintptr_t token=bb_target_enter(0);
            int space=bb_sound.commands.count+2u<=BB_COMMAND_CAPACITY &&
                      !bb_sound.commands.stop_pending && !bb_sound.commands.stop_inflight;
            bb_target_leave(0,token);
            if(!space){bb_say("BUSY / SELECT AGAIN");return;}
            int result=bb_library_confirm(p);
            if(result!=BB_FACTORY_PATTERN_OK){bb_library_result(result);return;}
        }
    }
    if(!start)bb_nav_notes_all_off();
    bb_send(BB_CMD_TRANSPORT,(unsigned)start);
}

/* Drum layer switch-on, bound to the applied outcome (r9.8): decisions in bb_nav_arm.h (host-tested).
 * A sample-kit intent changes no audio, so it arms nothing: the pattern confirmed next does. */
#include "bb_nav_arm.h"
static bb_nav_arm_state bb_nav_arm[BB_MACHINES];
static unsigned bb_nav_arm_enables,bb_nav_arm_dropped;   /* diagnostics: switched on / disarmed */
static void bb_nav_arm_enable(unsigned m) {
    if(bb_face.requested.drums[m].enabled)return;
    if(!bb_send_machine(m,BB_CMD_DRUM_ENABLE,1)){bb_say("LOADED; LAYER STILL OFF: REC+ARP");return;}
    bb_nav_arm_enables++;
    if(bb_face.requested.drums[m].muted)bb_say("LAYER ON, STILL MUTED");
}
static void bb_nav_arm_pattern(const bb_player *p) {
    unsigned m=bb_face.machine;
    if(bb_nav_is_drum()&&m<BB_MACHINES)bb_nav_arm_on_pattern(&bb_nav_arm[m],bb_face.requested.drums[m].enabled,p->request.ticket);
}
/* Called only after bb_nav_kit_commit() accepted the choice. */
static void bb_nav_arm_kit(void) {
    unsigned m=bb_face.machine;
    if(!bb_nav_is_drum()||m>=BB_MACHINES||bb_face.requested.drums[m].enabled)return;
#if BB_WITH_BROWSER && BB_SAMPLE_HOST_INTEGRATION && defined(BB_SAMPLE_HYBRID_TRIAL)
    if(bb_sound_pick.sample_count)return;     /* sample-kit intent: no audio change until the pattern */
#endif
    if(bb_sound_pick.pending)bb_nav_arm_on_sound(&bb_nav_arm[m],0);
    else bb_nav_arm_enable(m);                /* SOUND ALREADY LOADED: accepted, nothing pending */
}
static void bb_nav_arm_poll(void) {
    for(unsigned m=0;m<BB_MACHINES;m++) {
        if(bb_nav_arm[m].kind==BB_NAV_ARM_NONE)continue;
        const bb_player *p=&bb_drum_library[m];
        bb_nav_arm_view v={p->request.ticket,p->status.seen,(uint8_t)(p->status.result==BB_PATTERN_APPLIED),
            bb_sound_pick.pending,(uint8_t)(bb_sound_pick.machine==m),
            (uint8_t)(bb_sound_pick.status==BB_SP_APPLIED||bb_sound_pick.status==BB_SP_SAME)};
        int verdict=bb_nav_arm_step(&bb_nav_arm[m],&v);
        if(verdict==BB_NAV_ARM_ENABLE)bb_nav_arm_enable(m);
        else if(verdict==BB_NAV_ARM_DROP)bb_nav_arm_dropped++;
    }
}
/* ------------------------------------------------------------ actions */
static void bb_nav_do_actions(const bb_nav_out *o) {
    uint32_t a=o->actions;
    bb_machine *m=&bb_face.requested.drums[bb_face.machine];
    if(a&BB_NAV_A_TRANSPORT)bb_nav_transport(o->pattern_play);
    if(a&BB_NAV_A_PANIC) {
        /* All Dillo notes off (every source), transport untouched. Drum
         * voices are one-shots; no choke command exists (follow-up). */
        bb_nav_notes_all_off();
        bb_command c={0};c.type=BB_CMD_SYNTH_RELEASE;c.target=0;
        if(!bb_command_push(&bb_sound.commands,&c))bb_say("BUSY - RETRY");
        bb_dirty=1;
    }
    /* Choosing a kit or a pattern for a drum layer that is off switches it on (Joe, #4 6469173268), but
     * only once the audio owner has APPLIED that choice (bb_nav_arm_poll). A refused, not-ready, busy,
     * cancelled or rejected confirmation never enables the layer (Duke, #4 6469741984). */
    if(a&BB_NAV_A_KIT_CONFIRM){if(bb_nav_kit_commit()){bb_nav_arm_kit();bb_nav_kit_chosen();}}
    if(a&BB_NAV_A_PATTERN_CONFIRM) {
        bb_player *p=bb_nav_player();
        if(!p->ready)bb_say("LIBRARY NOT READY");
        else {int result=bb_library_confirm(p);bb_library_result(result);if(result==BB_FACTORY_PATTERN_OK)bb_nav_arm_pattern(p);}
    }
    if(a&BB_NAV_A_MUTE) {
        if(bb_nav_is_drum())bb_send(BB_CMD_DRUM_MUTE,!m->muted);
        else bb_send_synth(0,BB_CMD_SYNTH_MUTE,!bb_face.requested.synths[0].muted,0);
    }
    if(a&BB_NAV_A_EDIT_TYPE){bb_face.requested.edit=(bb_edit)((bb_face.requested.edit+1u)%3u);bb_dirty=1;
        bb_say(bb_face.requested.edit==BB_ACCENT?"ACCENT":bb_face.requested.edit==BB_SLIDE?"SLIDE":"GATE");}
    if(a&BB_NAV_A_SWING_TOGGLE) {
        unsigned x2=bb_swing_x2(bb_face.requested.swing);
        if(x2>100u){bb_nav_swing_x2=(uint8_t)x2;bb_send(BB_CMD_SWING_X2,100);}
        else bb_send(BB_CMD_SWING_X2,bb_nav_swing_x2>100u?bb_nav_swing_x2:112u);
    }
    if(a&BB_NAV_A_LAYER_ENABLE) {
        if(bb_nav_is_drum())bb_send(BB_CMD_DRUM_ENABLE,!m->enabled);
        else bb_send_synth(0,BB_CMD_SYNTH_MUTE,!bb_face.requested.synths[0].muted,0);
    }
    if(a&BB_NAV_A_STEP_BANK_NEXT) {
        unsigned length=bb_nav_length();
        bb_face.bank=(uint8_t)((bb_face.bank+1u)*16u<length?bb_face.bank+1u:0u);bb_dirty=1;
    }
    if(a&BB_NAV_A_REVERT) {
        if(!bb_nav_is_drum()) {
            if(bb_send_synth(0,BB_CMD_DILLO_PATCH,bb_patch_index[0],0)){bb_patch_edited[0]=0;bb_patch_from_scene[0]=0;bb_say("SOUND BACK TO ITS PRESET");}
        } else bb_say("NOT IN THIS BUILD");   /* drum voice revert needs a new command */
    }
#if BB_WITH_EVOLVE
    if(a&BB_NAV_A_EVO_OPEN) {
        if(bb_evo_source_blocked()){bb_nav_force(BB_NAV_PLAY);bb_say("SOURCE PATTERN: EVOLVE OFF");}
        else if(!bb_evo_view.ready)bb_evo_send(BB_EV_CAPTURE,0,0,0);
    }
    if(a&BB_NAV_A_EVO_TOGGLE) {
        if(bb_evo_view.enabled&&!bb_evo_view.frozen)bb_evo_send(BB_EV_ENABLE,0,0,0);
        else bb_evo_send(BB_EV_RUN,0,0,0);
    }
    if(a&BB_NAV_A_EVO_CAPTURE)bb_evo_send(BB_EV_CAPTURE,0,0,0);
#endif
    if(a&BB_NAV_A_TEMPO_SET)
        bb_send(BB_CMD_TEMPO,(unsigned)bb_limit(((int)o->tempo_x10+5)/10,30,300));
#if BB_WITH_EFFECTS
    if(a&BB_NAV_A_THROW_ON) {
        /* Dub throw: echo return to full while FX is held, then restored.
         * Exactly one command each way (the module pairs them). */
        unsigned lo,hi;bb_fx_parameter_limits(BB_FX_ECHO_WET,&lo,&hi);
        bb_nav_throw_saved=(uint8_t)bb_fx_parameter_get(&bb_face.requested,BB_FX_ECHO_WET);
        bb_nav_throw=bb_send_effect_parameter(BB_FX_ECHO_WET,hi);
    }
    if((a&BB_NAV_A_THROW_OFF)&&bb_nav_throw) {
        if(bb_send_effect_parameter(BB_FX_ECHO_WET,bb_nav_throw_saved))bb_nav_throw=0;
    }
#endif
#if BB_WITH_SESSION
    if(a&(BB_NAV_A_SAVE_STORE|BB_NAV_A_QUICK_SAVE)){(void)bb_session_request(bb_scene_slot,0);bb_say(bb_session_message);}
#endif
    if(a&BB_NAV_A_KEY_OCTAVE) {
        static char text[]="KEYS OCTAVE +0";
        text[12]=o->key_octave<0?'-':'+';text[13]=(char)('0'+(o->key_octave<0?-o->key_octave:o->key_octave));
        bb_say(text);
    }
    if(o->oct==BB_NAV_OCT_STEP_BANK) {
        unsigned length=bb_nav_length();
        if(o->oct_dir<0&&bb_face.bank)bb_face.bank--;
        if(o->oct_dir>0&&(bb_face.bank+1u)*16u<length)bb_face.bank++;
        bb_dirty=1;
    } else if(o->oct==BB_NAV_OCT_LIST_PAGE&&bb_nav.page==BB_NAV_PATTERN) {
        bb_player *p=bb_nav_player();
        if(p->ready){(void)bb_player_page(p,o->oct_dir,0);bb_dirty=1;}
    }
}
/* A retry of a still-pending throw-off (queue was full). */
static void bb_nav_throw_retry(void) {
#if BB_WITH_EFFECTS
    if(bb_nav_throw&&!bb_nav.throw_on&&bb_send_effect_parameter(BB_FX_ECHO_WET,bb_nav_throw_saved))bb_nav_throw=0;
#endif
}

/* ------------------------------------------------------------ encoders */
static void bb_nav_do_encoders(const bb_nav_out *o) {
    for(unsigned e=0;e<BB_NAV_ENCODERS;e++) {
        const bb_nav_route *r=&o->enc[e];
        int d=r->delta;
        if(r->target==BB_NAV_TO_NONE||!d)continue;
        switch(r->target) {
        case BB_NAV_TO_INSTRUMENT: bb_nav_focus_move(d);break;
        case BB_NAV_TO_TEMPO:
            bb_send(BB_CMD_TEMPO,(unsigned)bb_limit((int)bb_face.requested.bpm+bb_limit(d,-270,270),30,300));
            if(!bb_face.rejected)bb_say("TEMPO / REC + SELECT");
            break;
        case BB_NAV_TO_SECTION: bb_nav_section_step(d);break;
        case BB_NAV_TO_KIT:
            if(bb_sound_pick_audio_failed()){bb_say("AUDIO FAULT / LOAD BLOCKED");break;}
            if(bb_sound_pick.pending){bb_say("SOUND REQUEST STILL PENDING");break;}
            bb_nav_project();   /* opens the chooser on this scan's entry */
            if(bb_sound_pick_visible()){(void)bb_sound_picker_move(&bb_sound_pick,d);bb_dirty=1;bb_sound_pick_poll();}
            break;
        case BB_NAV_TO_PATTERN: {
            bb_player *p=bb_nav_player();
            if(!p->ready){bb_say("LIBRARY NOT READY");break;}
            if(bb_nav_pattern_move(p,d))bb_dirty=1;else bb_library_result(BB_FACTORY_PATTERN_INVALID);
            break;
        }
        case BB_NAV_TO_VOICE:
#if BB_WITH_RICH_DRUM_BROWSER
            if(bb_rich_active()&&bb_nav.page==BB_NAV_PLAY){bb_rich_move(bb_limit(d,-17,17));break;}
#endif
            bb_face.voice=(uint8_t)bb_limit((int)bb_face.voice+bb_limit(d,-11,11),0,bb_kit_family[bb_face.machine]==1?11:10);
            bb_nav_set_voice(&bb_nav,bb_face.voice);bb_dirty=1;
            break;
        case BB_NAV_TO_STEP_PITCH: bb_pitch_edit(0,d);break;
        case BB_NAV_TO_KNOB: bb_nav_knob(r->role,r->index,d);break;
        default: break;
        }
    }
}

/* ------------------------------------------------------------ keys */
static void bb_nav_do_keys(const bb_nav_out *o) {
    for(unsigned i=0;i<o->kev_count;i++) {
        const bb_nav_key_ev *e=&o->kev[i];
        unsigned arg=e->arg,accent=(e->flags&BB_NAV_KF_ACCENT)!=0,release=(e->flags&BB_NAV_KF_RELEASE)!=0;
        unsigned phys=e->key<BB_NAV_KEYS?bb_nav_phys[e->key]:27u;
        switch(e->role) {
        case BB_NAV_K_PAD:
            if(bb_panel_drum_hit(&bb_face,&bb_sound.commands,arg,accent?127u:88u)) {
                bb_face.voice=(uint8_t)arg;bb_nav_set_voice(&bb_nav,(uint8_t)arg);bb_dirty=1;
            } else bb_say("BUSY - RETRY");
            break;
        case BB_NAV_K_NOTE:
            if(release)bb_nav_note_off(arg);else bb_nav_note_on_key(arg,accent?127u:88u,phys);
            break;
        case BB_NAV_K_STEP:
            if(bb_nav_is_drum()) {
                if(bb_rich_active()){bb_say("SOURCE PATTERN VIEW");break;}
                uint8_t was=bb_face.accent;bb_face.accent=(uint8_t)accent;
                (void)bb_panel_step(&bb_face,&bb_sound.commands,arg);
                bb_face.accent=was;bb_dirty=1;
            } else {
                /* bb_synth_input's step loop */
                const bb_sequence *seq=&bb_face.requested.synths[0];
                unsigned step=bb_face.bank*16u+arg;
                if(step>=seq->length)break;
                bb_edit edit=accent?BB_ACCENT:bb_face.requested.edit;
                unsigned pitch=seq->notes[step].gate?seq->notes[step].note:bb_entry_pitch[0];
                bb_selected_note[0]=(uint8_t)step;
                if(!bb_panel_synth_step(&bb_face,&bb_sound.commands,0,arg,edit,pitch))bb_say("NOTE EDIT REJECTED");
                else bb_entry_pitch[0]=(uint8_t)pitch;
                bb_dirty=1;
            }
            break;
        case BB_NAV_K_VOICE:
#if BB_WITH_RICH_DRUM_BROWSER
            if(bb_rich_active()&&bb_nav.page==BB_NAV_PLAY){bb_rich_select(arg);break;}
#endif
            bb_face.voice=(uint8_t)arg;bb_dirty=1;
            break;
        case BB_NAV_K_BANK_PREV: if(bb_face.bank)bb_face.bank--;bb_dirty=1;break;
        case BB_NAV_K_BANK_NEXT: if((bb_face.bank+1u)*16u<bb_nav_length())bb_face.bank++;bb_dirty=1;break;
        case BB_NAV_K_GROUP_MUTE:
            if(bb_nav_gsolo[bb_face.machine]){bb_nav_gsolo[bb_face.machine]=0;bb_nav_gsolo_muted[bb_face.machine]=0;}
            bb_nav_group_toggle(arg);break;
        case BB_NAV_K_GROUP_SOLO: bb_nav_group_solo(arg);break;
        case BB_NAV_K_INSTR_MUTE: bb_nav_instr_mute(arg);break;
        case BB_NAV_K_INSTR_SOLO: bb_nav_instr_solo(arg);break;
        case BB_NAV_K_KILL: bb_nav_instr_mute(arg);break;
        case BB_NAV_K_SECTION: bb_nav_section_launch(arg);break;
        case BB_NAV_K_SECTION_PREV: bb_nav_section_step(-1);break;
        case BB_NAV_K_SECTION_NEXT: bb_nav_section_step(1);break;
        case BB_NAV_K_PICK:
            if(bb_nav.page==BB_NAV_KIT) {
                if(!bb_sound_pick_visible()||arg>=bb_sound_picker_items(&bb_sound_pick)){bb_say("NO SOUND ON THIS KEY");break;}
                (void)bb_sound_picker_key(&bb_sound_pick,arg);
                if(bb_nav_kit_commit())bb_nav_kit_chosen();
            } else {
                bb_player *p=bb_nav_player();
                if(!p->ready){bb_say("LIBRARY NOT READY");break;}
                int valid=bb_library_preview_bank_key(p,arg);
                bb_library_result(valid?bb_library_confirm(p):BB_FACTORY_PATTERN_INVALID);
            }
            break;
#if BB_WITH_SESSION
        case BB_NAV_K_SLOT: bb_scene_slot=(uint8_t)arg;bb_dirty=1;break;
        case BB_NAV_K_SLOT_LOAD: (void)bb_session_request(arg,1);bb_say(bb_session_message);break;
#endif
        /* ANIMATION / SCOPE / METERS: no longer offered (bb_nav.c GLOBAL keys are inert on v3) */
#if BB_WITH_EVOLVE
        case BB_NAV_K_EVO_KEY:
            if(!bb_evo_view.ready)break;
            if(arg<6)bb_evo_send(BB_EV_PARTS,bb_evo_view.settings.parts^(1u<<arg),0,0);
            else if(arg<10)bb_evo_send(BB_EV_ATTRIBUTES,bb_evo_view.settings.attributes^(1u<<(arg-6)),0,0);
            break;
        case BB_NAV_K_EVO_LOCK: {
            if(!bb_evo_view.ready)break;
            unsigned part=bb_nav_is_drum()?bb_face.machine:BB_MACHINES;
            unsigned step=bb_face.bank*16u+arg;
            if(step<BB_STEPS)bb_evo_send(BB_EV_LOCK,!(bb_evo_view.settings.locked[part]&(UINT64_C(1)<<step)),part,step);
            break;
        }
        case BB_NAV_K_EVO_FREEZE: if(bb_evo_view.ready)bb_evo_send(BB_EV_FREEZE,bb_evo_view.pending?1u:!bb_evo_view.frozen,0,0);break;
        case BB_NAV_K_EVO_HOME: if(bb_evo_view.ready)bb_evo_send(BB_EV_HOME,0,0,0);break;
#endif
        case BB_NAV_K_STEP_PITCH: {
            const bb_sequence *seq=&bb_face.requested.synths[0];
            unsigned step=bb_selected_note[0];
            if(step>=seq->length||!seq->notes[step].gate){bb_say("CHOOSE A STEP FIRST");break;}
            int delta=(int)arg-(int)seq->notes[step].note;
            if(delta)bb_pitch_edit(0,delta);
            break;
        }
        default: break;
        }
    }
}

/* ------------------------------------------------------------ boot */
static void bb_nav_boot(void) {
    bb_nav_map_init();
    bb_nav_init(&bb_nav,BB_NAV_STUDIO,HOLD_MS[settings_hold]);
    bb_nav_set_features(&bb_nav,BB_NAV_FEATURES);
    memset(bb_nav_note_on,0,sizeof bb_nav_note_on);memset(bb_nav_note_owed,0,sizeof bb_nav_note_owed);
    memset(bb_nav_gsolo,0,sizeof bb_nav_gsolo);memset(bb_nav_gsolo_muted,0,sizeof bb_nav_gsolo_muted);
    bb_nav_isolo=bb_nav_isolo_muted=0;bb_nav_throw=0;bb_nav_meters=0;
    bb_nav_row=0;bb_nav_shown_page=bb_nav_shown_row=0xFF;
    bb_nav_focus_apply();
    /* Power-on lands on STUDIO HOME (bb_nav_init above): the loaded patterns are the ready-to-play
     * choice. Joe, 28 Oct (#4 6466673215), replacing the earlier kit-first entrance on KIT. */
}

/* ------------------------------------------------------------ the scan */
static void bb_nav_scan(uint32_t pressed,uint32_t notes) {
    bb_nav_in in;
    memset(&in,0,sizeof in);
    in.pressed=bb_nav_buttons(pressed);
    in.held=bb_nav_buttons(fm1_in.buttons)|(ui.uboot?(BB_NAV_BIT(BB_NAV_B_OCTDN)|BB_NAV_BIT(BB_NAV_B_OCTUP)):0u);
    for(unsigned e=0;e<BB_NAV_ENCODERS;e++)in.enc[e]=(int16_t)bb_limit(bb_read_encoder(e),-32767,32767);
    in.keys=bb_nav_keys_logical(notes);
    in.keys_held=bb_nav_keys_logical(fm1_in.notes);
    in.now_ms=fm1_ms;
    bb_nav_fill_facts(&in.facts);
    bb_nav_f=in.facts;
    bb_nav_input(&bb_nav,&in,&bb_nav_o);
#if BB_WITH_SESSION
    /* SAVE into other data (bb/bb_save_claim.h): after the refused STORE armed
     * the claim, a fresh SAVE press held 2 s on this page and slot, alone,
     * replaces the slot. bb_nav does nothing with SAVE on the SAVE page. */
    if(bb_claim_scan(&bb_session_claim,fm1_ms,bb_nav.page==BB_NAV_SAVE,bb_scene_slot,
         !!(in.pressed&BB_NAV_BIT(BB_NAV_B_SAVE)),!!(in.held&BB_NAV_BIT(BB_NAV_B_SAVE)),
         (in.held&~BB_NAV_BIT(BB_NAV_B_SAVE))||in.keys_held||in.keys)) {
        (void)bb_session_request_ex(bb_scene_slot,0,1);bb_say(bb_session_message);
    }
#endif
    if(bb_nav_o.uboot) {
        /* Untouched platform gesture. Sounding panel notes stop now, as the
         * installed bb_keys_sync did (and bb_midi_input does for MIDI); the
         * module's owed releases then find nothing sounding. */
        bb_nav_notes_all_off();
        bb_nav_project();return;
    }
    if(bb_nav_o.message)bb_say(bb_nav_message_text(bb_nav_o.message));
    if(bb_nav_o.page_changed&&!bb_nav_o.message)bb_dirty=1;
    /* REC (or black 9 on drum steps) is the accent; the face shows it. */
    bb_face.accent=(uint8_t)!!(in.held&BB_NAV_BIT(BB_NAV_B_REC));
    bb_nav_do_keys(&bb_nav_o);       /* releases come first in the array */
    bb_nav_do_actions(&bb_nav_o);
    bb_nav_do_encoders(&bb_nav_o);
    if(bb_nav.page!=BB_NAV_SOUND&&bb_face.voice>10)bb_face.voice=10;
    bb_nav_project();
}

/* ------------------------------------------------------------ LEDs */
/* One LED language from bb_nav_leds() for every button and key:
 * DARK / DIM (fm1_led_dim) / LIT (fm1_led) / BREATH (fm1_led_breath).
 * PLAY's green LED keeps its running meaning. */
static bb_nav_led_out bb_nav_l;
static void bb_nav_leds_out(void) {
    uint8_t lit[FM1_NCOL]={0},dim[FM1_NCOL]={0},breath[FM1_NCOL]={0};
    bb_nav_fill_facts(&bb_nav_f);
    bb_nav_leds(&bb_nav,&bb_nav_f,bb_nav_keys_logical(fm1_in.notes),&bb_nav_l);
    for(unsigned b=0;b<BB_NAV_BUTTONS;b++) {
        unsigned id=panel.btn[b],v=bb_nav_l.button[b];
        led_put(v==BB_NAV_LED_LIT?lit:v==BB_NAV_LED_DIM?dim:breath,id,v!=BB_NAV_LED_DARK);
    }
    for(unsigned k=0;k<BB_NAV_KEYS;k++) {
        unsigned id=14u+bb_nav_phys[k],v=bb_nav_l.key[k];
        led_put(v==BB_NAV_LED_LIT?lit:v==BB_NAV_LED_DIM?dim:breath,id,v!=BB_NAV_LED_DARK);
    }
    if(bb_face.requested.running&&!bb_sound_pick_audio_failed())lit[LED_PLAY_GREEN>>3]|=(uint8_t)(1u<<(LED_PLAY_GREEN&7));
    for(unsigned c=0;c<FM1_NCOL;c++){fm1_led[c]=lit[c];fm1_led_dim[c]=dim[c];fm1_led_breath[c]=breath[c];fm1_led_mid[c]=0;}
}
