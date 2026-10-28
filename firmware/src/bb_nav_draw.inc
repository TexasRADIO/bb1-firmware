/* SPDX-License-Identifier: GPL-3.0-only
 * Navigation v2.1 drawing helpers: the screens follow bb_nav's page and
 * K1-K4 labels with the EXISTING renderer, fonts and primitives (cv_*,
 * bb_number, bb_rotary, bb_backdrop). Main-loop drawing only; reads the UI
 * mirror (bb_face.requested) and existing views, never writes audio state.
 * Included by bb_target_ui.c before bb_target_draw. */

/* K1-K4 of the current page: label from bb_nav, value text from the same
 * parameter the route writes, and a 0..127 dial position. */
static const char *bb_nav_knob_label(unsigned k) {
    uint8_t roles[4];const char *labels[4];
    bb_nav_knobs(&bb_nav,roles,labels);
    return k<4u?labels[k]:"";
}
static unsigned bb_nav_knob_role(unsigned k) {
    uint8_t roles[4];const char *labels[4];
    bb_nav_knobs(&bb_nav,roles,labels);
    return k<4u?roles[k]:BB_NAV_R_NONE;
}
static unsigned bb_nav_percent(unsigned level){level=level>127u?127u:level;return (level*100u+63u)/127u;}
static void bb_nav_fmt_signed(char *s,int v){if(v<0){*s++='-';v=-v;}else if(v>0)*s++='+';fmt_int(s,v);}
static void bb_nav_fmt_pct(char *s,unsigned level){fmt_int(s,(int32_t)bb_nav_percent(level));unsigned n=0;while(s[n])n++;s[n++]='%';s[n]=0;}
static unsigned bb_nav_knob_text(unsigned k,char out[16]) {
    static const char *const heads[8]={"OFF","1","2","1+2","3","1+3","2+3","1+2+3"};
    static const char *const roots[12]={"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};
    const bb_machine *m=&bb_face.requested.drums[bb_face.machine];
    unsigned role=bb_nav_knob_role(k),pos=0;int p;
    out[0]='-';out[1]='-';out[2]=0;
    switch(role) {
    case BB_NAV_R_NONE: out[0]=0;return 0;
    case BB_NAV_R_GROUP_KICK: case BB_NAV_R_GROUP_SNCL: case BB_NAV_R_GROUP_HATS: case BB_NAV_R_GROUP_PERC: {
        bb_group_view view;unsigned g=role-BB_NAV_R_GROUP_KICK;
        if(bb_group_input(&bb_sound.commands,bb_sound.groups,bb_face.machine,BB_GROUP_READ,0,0,&view)==BB_GROUP_REFUSED){
            const char *s="BUSY";unsigned n=0;while(s[n]){out[n]=s[n];n++;}out[n]=0;return 0;}
        if(view.pending&(1u<<g)){const char *s="WAIT";unsigned n=0;while(s[n]){out[n]=s[n];n++;}out[n]=0;bb_dirty=1;return 0;}
        if(view.muted&(1u<<g)){const char *s="MUTE";unsigned n=0;while(s[n]){out[n]=s[n];n++;}out[n]=0;return view.level[g];}
        bb_nav_fmt_pct(out,view.level[g]);return view.level[g];
    }
    case BB_NAV_R_LAYER_LEVEL: bb_nav_fmt_pct(out,m->level);return m->level;
    case BB_NAV_R_MIX_DRUM_A: bb_nav_fmt_pct(out,bb_face.requested.drums[0].level);return bb_face.requested.drums[0].level;
    case BB_NAV_R_MIX_DRUM_B: bb_nav_fmt_pct(out,bb_face.requested.drums[1].level);return bb_face.requested.drums[1].level;
    case BB_NAV_R_MIX_DILLO: case BB_NAV_R_SYNTH_LEVEL: {
        unsigned v=bb_face.requested.band_level[BB_MIX_DILLO];bb_nav_fmt_pct(out,v);return v;
    }
    case BB_NAV_R_SWING: {
        unsigned x2=bb_swing_x2(bb_face.requested.swing),n;
        fmt_int(out,(int32_t)(x2/2u));n=0;while(out[n])n++;
        if(x2&1u){out[n++]='.';out[n++]='5';}
        out[n]=0;return (x2>100u?x2-100u:0u)*127u/50u;
    }
    case BB_NAV_R_BPM: fmt_int(out,bb_face.requested.bpm);return (bb_face.requested.bpm-30u)*127u/270u;
    case BB_NAV_R_AB_BALANCE: {
        int d=(int)bb_nav_percent(bb_face.requested.drums[1].level)-(int)bb_nav_percent(bb_face.requested.drums[0].level);
        bb_nav_fmt_signed(out,d);return (unsigned)bb_limit(64+d*63/100,0,127);
    }
    case BB_NAV_R_TRANSPOSE: bb_nav_fmt_signed(out,bb_pattern_shift[0]);
        return (unsigned)bb_limit(64+bb_pattern_shift[0]*63/BB_TRANSPOSE_LIMIT,0,127);
#if BB_WITH_EFFECTS
    case BB_NAV_R_FX_RETURN: {
        unsigned v=bb_fx_parameter_get(&bb_face.requested,BB_FX_ECHO_WET);fmt_int(out,(int32_t)v);
        unsigned lo,hi;bb_fx_parameter_limits(BB_FX_ECHO_WET,&lo,&hi);
        return hi>lo?(v-lo)*127u/(hi-lo):0u;
    }
#endif
#if BB_WITH_SESSION
    case BB_NAV_R_SLOT: fmt_int(out,bb_scene_slot+1);return bb_scene_slot*42u;
#endif
#if BB_WITH_EVOLVE
    case BB_NAV_R_EVO_AMOUNT: fmt_int(out,bb_evo_view.settings.amount);return bb_evo_view.settings.amount*127u/100u;
    case BB_NAV_R_EVO_EVERY: fmt_int(out,bb_evo_view.settings.every_phrases);return bb_evo_view.settings.every_phrases*2u;
    case BB_NAV_R_EVO_ROOT: {const char *s=roots[bb_evo_view.settings.root%12u];out[0]=s[0];out[1]=s[1];out[2]=0;return bb_evo_view.settings.root*11u;}
    case BB_NAV_R_EVO_SCALE: {
        const char *s=bb_evo_view.settings.scale==0x5ad?"MINOR":bb_evo_view.settings.scale==0xab5?"MAJOR":"CUSTOM";
        unsigned n=0;while(s[n]){out[n]=s[n];n++;}out[n]=0;return 64;
    }
#endif
    case BB_NAV_R_909_CONTROL: {
        unsigned prm=bb_ui_909_parameter(k);
        if(bb909_control_max(bb_face.voice,prm)<0){out[0]=0;return 0;}
        unsigned v=bb_c1_909_controls.value[bb_face.voice][prm];fmt_int(out,(int32_t)v);return v;
    }
    default: break;
    }
#if BB_WITH_EFFECTS
    if((p=bb_nav_fx_param(role))>=0) {
        unsigned v=bb_fx_parameter_get(&bb_face.requested,(unsigned)p),lo,hi;
        if(p==BB_FX_ECHO_HEADS){const char *s=heads[v&7u];unsigned n=0;while(s[n]){out[n]=s[n];n++;}out[n]=0;return v*18u;}
        fmt_int(out,(int32_t)v);bb_fx_parameter_limits((unsigned)p,&lo,&hi);
        return hi>lo&&v>=lo?(v-lo)*127u/(hi-lo):0u;
    }
    if(bb_nav.page==BB_NAV_FX&&!bb_nav_is_drum()&&(role==BB_NAV_R_SEND_ECHO||role==BB_NAV_R_SEND_SPRING)) {
        unsigned v=bb_fx_parameter_get(&bb_face.requested,BB_FX_DILLO_ECHO+(role==BB_NAV_R_SEND_SPRING));
        fmt_int(out,(int32_t)v);return v;
    }
#endif
    if((p=bb_nav_drum_param(role))>=0&&bb_nav_is_drum()) {
        unsigned v=bb_nav.page==BB_NAV_SOUND?bb_drum_parameter_get(&m->sound,bb_face.voice,(unsigned)p):
            bb_visible_drum_value(&m->sound,(unsigned)p);
        if(p==BB_DRUM_LEVEL)bb_nav_fmt_pct(out,v);else fmt_int(out,(int32_t)v);
        return v>127u?127u:v;
    }
    if((p=bb_nav_synth_param(role))>=0&&!bb_nav_is_drum()) {
        if(p==BB_PARAM_WAVE){const char *s=bb_face.requested.dillo.wave?"SAW":"SQR";unsigned n=0;while(s[n]){out[n]=s[n];n++;}out[n]=0;
            return bb_face.requested.dillo.wave?127u:0u;}
        unsigned raw=bb_synth_parameter_get(&bb_face.requested,0,(unsigned)p),lo=0,hi=127,v=raw;
        if(bb_synth_parameter_limits(0,(unsigned)p,&lo,&hi)&&hi>lo) {
            if(raw<lo)raw=lo;if(raw>hi)raw=hi;pos=(raw-lo)*127u/(hi-lo);
            if(p==BB_PARAM_RESONANCE)v=((raw-lo)*100u+(hi-lo)/2u)/(hi-lo);   /* as bb_synth_control_value */
        }
        fmt_int(out,(int32_t)v);return pos;
    }
    return 0;
}
/* Four labelled values at the existing synth-strip positions (x = 4 + 60k). */
static void bb_nav_knob_strip(int label_y,int value_y,const aafont_t *font) {
    for(unsigned k=0;k<4;k++) {
        char value[16];int x=4+(int)k*60;
        (void)bb_nav_knob_text(k,value);
        const char *label=bb_nav_knob_label(k);
        if(!label[0])continue;
        cv_text(x,label_y,&AF_S,label,BB_DIM);
        cv_text(x,value_y,text_w(font,value)<=56?font:&AF_S,value,BB_TEXT);
    }
}
/* "SCALE A MINOR / SCL: NEXT" while scale lock is on (Dillo faces). */
static const char *bb_nav_scale_hint(void) {
    static const char *const roots[12]={"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};
    static char text[32];
    if(bb_nav.scale==BB_NAV_SCALE_OFF)return NULL;
    const char *s="SCALE ";unsigned n=0;
    while(*s)text[n++]=*s++;
    s=roots[bb_nav.root%12u];while(*s)text[n++]=*s++;
    text[n++]=' ';s=bb_nav_scale_name(bb_nav.scale);while(*s)text[n++]=*s++;
    s=" / SCL: NEXT";while(*s)text[n++]=*s++;text[n]=0;
    return text;
}
/* Section keys line for STUDIO / PATTERN when a pattern has more than 11. */
static const char *bb_nav_section_hint(void) {
    unsigned count=bb_nav_f.section_count,page=bb_nav_section_page(&bb_nav);
    if(count<=BB_NAV_SECTION_KEYS)return NULL;
    static char text[40];
    unsigned first=page*BB_NAV_SECTION_KEYS+1u,last=first+BB_NAV_SECTION_KEYS-1u,n=0;
    if(last>count)last=count;
    const char *s="BLACK: SECTIONS ";while(*s)text[n++]=*s++;
    if(first>=10u)text[n++]=(char)('0'+first/10u);
    text[n++]=(char)('0'+first%10u);
    if(last!=first){text[n++]='-';if(last>=10u)text[n++]=(char)('0'+last/10u);text[n++]=(char)('0'+last%10u);}
    s=bb_nav.page==BB_NAV_PATTERN?(page?" / REC+OCT-":" / REC+OCT+"):(page?" / OCT-":" / OCT+");
    while(*s)text[n++]=*s++;
    text[n]=0;return text;
}
/* Audio meters (formerly the DIAG page), drawn on GLOBAL when white 3 is on.
 * Same values and caveats as the former page. */
static void bb_nav_meters_draw(void) {
#ifdef BB_DIAGNOSTICS_WHOLE_HALF
    uint32_t total,budget,max_ticks,over,late,faults;
    const char *const labels[5]={"HALF LOAD %","MAX TOTAL US","OVER BUDGET","DMA LATE","DSP FAULTS"};
    unsigned values[5],row;
    uintptr_t token=bb_target_enter(0);
    total=bb_target_timing.last_total_ticks;budget=bb_target_timing.budget_ticks;
    max_ticks=bb_target_timing.max_total_ticks;over=bb_target_timing.budget_exceeds;
    late=bb_late_this_boot();faults=bb_audio_faults;
    bb_target_leave(0,token);
    values[0]=bb_deadline_percent(total,budget);
    values[1]=max_ticks/FM1_TICKS_PER_US;
    values[2]=over;values[3]=late;values[4]=faults;
    cv_text(9,0,&AF_M,"AUDIO / WHITE 3: OFF",BB_TEXT);
    for(row=0;row<5;row++) {
        cv_text(9,25+(int)row*19,&AF_S,labels[row],BB_DIM);
        if(!row && !budget)cv_text(176,22,&AF_M,"--",BB_DIM);
        else bb_number(176,22+(int)row*19,values[row]>999999u?999999u:values[row],&AF_M,BB_TEXT);
    }
#else
    uint32_t late=bb_late_this_boot();
    cv_text(9,3,&AF_M,"AUDIO DIAGNOSTICS",BB_TEXT);
    cv_text(9,26,&AF_S,"RENDER LOAD %",BB_DIM);
    bb_number(176,23,(unsigned)((BB_METER_CPU_Q8*100u+128u)/256u),&AF_M,BB_TEXT);
    cv_text(9,45,&AF_S,"MAX AUDIO US",BB_DIM);
    bb_number(176,42,(unsigned)(BB_METER_MAX_US>999999u?999999u:BB_METER_MAX_US),&AF_M,BB_TEXT);
    cv_text(9,64,&AF_S,"DMA LATE",BB_DIM);
    bb_number(176,61,(unsigned)(late>999999u?999999u:late),&AF_M,BB_TEXT);
    cv_text(9,83,&AF_S,"DSP FAULTS",BB_DIM);
    bb_number(176,80,(unsigned)(bb_audio_faults>999999u?999999u:bb_audio_faults),&AF_M,BB_TEXT);
    cv_text(9,104,&AF_S,"SINCE BOOT / WHITE 3: OFF",BB_DIM);
#endif
}
/* GLOBAL: tempo, swing, A/B balance, Dillo transpose on four rotaries (the
 * former mixer face's layout), display settings on white keys 1-3. */
static void bb_nav_global_draw(void) {
    bb_backdrop(0,120,0);
    cv_text(9,5,&AF_M,"GLOBAL",BB_TEXT);
    cv_text(142,7,&AF_S,bb_face.requested.running?"PLAY":"STOP",BB_GREEN);
    bb_number(191,5,bb_face.requested.bpm,&AF_M,BB_TEXT);
    for(unsigned k=0;k<4;k++) {
        char value[16];int x=8+(int)k*58;
        unsigned position=bb_nav_knob_text(k,value);
        const char *label=bb_nav_knob_label(k);
        bb_number(x,29,k+1u,&AF_S,BB_ORANGE);
        bb_rotary(x+26,57,position,1);
        cv_text(x+26-text_w(&AF_S,label)/2,80,&AF_S,label,BB_TEXT);
        cv_text(x+26-text_w(&AF_S,value)/2,96,&AF_S,value,BB_TEXT);
    }
    cv_blit(0,0);
    bb_backdrop(120,120,0);
    if(bb_nav_meters){bb_nav_meters_draw();cv_blit(0,120);return;}
    static const char *const names[3]={"1 ANIMATION","2 SCOPE","3 METERS"};
    const int on[3]={bb_face.animate!=0,bb_scope_enabled!=0,bb_nav_meters!=0};
    for(unsigned i=0;i<3;i++) {
        int y=4+(int)i*24;uint16_t ink=on[i]?BB_ORANGE:BB_DIM;
        cv_frame(9,y,222,21,ink);cv_text(17,y+5,&AF_S,names[i],BB_TEXT);
        cv_text(190,y+5,&AF_S,on[i]?"ON":"OFF",ink);
    }
    cv_text(9,88,&AF_S,bb_hint_until?bb_hint:"WHITE 1-3: DISPLAY",BB_DIM);
    cv_text(9,105,&AF_S,"REC+GLO: TAP TEMPO / HOME: STUDIO",BB_DIM);
    cv_blit(0,120);
}
/* Help (HOME held): nine rows for the page under the overlay. */
static const char *bb_nav_help_rows(const char *rows[9]) {
    static char title[24];
    const int drum=bb_nav_is_drum(),steps=bb_nav.play_keys==BB_NAV_PLAY_STEPS;
    const char *name=bb_nav_page_name(&bb_nav);unsigned n=0;
    while(*name&&n<12u)title[n++]=*name++;
    const char *g=" / GUIDE";while(*g)title[n++]=*g++;
    title[n]=0;
    rows[0]="RELEASE HOME TO RETURN";
    rows[1]="HOME: STUDIO / REC+HOME: PANIC";
    rows[2]="SELECT: A / B / DILLO";
    rows[3]="ALGO: PATTERN / PRESETS: KIT";
    rows[4]="ARP: PLAY KEYS / SEQ: STEPS";
    rows[5]=rows[6]=rows[7]=rows[8]="";
    switch(bb_nav.page) {
    case BB_NAV_STUDIO:
        rows[5]="EDIT: OPEN THE FOCUSED ONE";rows[6]="WHITE 1-3: MUTE / REC: SOLO";
        rows[7]="BLACK: SECTIONS / OCT: 12+";rows[8]="K1-3 LEVELS / K4 FX RETURN";break;
    case BB_NAV_PLAY:
        if(drum&&!steps){rows[5]="WHITE 1-11: DRUMS / REC: ACCENT";rows[6]="BLACK 1-4: MUTE / REC: SOLO";
            rows[7]="BLACK 6/7: SECTION - / +";rows[8]="BLACK 8-11: KILL A B DILLO ALL";}
        else if(drum){rows[5]="WHITE: STEPS / REC: ACCENT";rows[6]="BLACK 1-8: VOICE / 9: ACCENT";
            rows[7]="BLACK 10/11, OCT: STEP BANK";rows[8]="K1-K4: KICK SN/CL HATS PERC";}
        else if(!steps){rows[5]="ALL KEYS PLAY / REC: ACCENT";rows[6]="OCT: KEY OCTAVE (NOT PATTERN)";
            rows[7]="SCL: SCALE / REC+SCL: ROOT";rows[8]="OVERLAP TWO KEYS: SLIDE";}
        else {rows[5]="WHITE: STEPS / EDIT: TYPE";rows[6]="BLACK: PLAY THE STEP PITCH";
            rows[7]="OCT: STEP BANK";rows[8]="REC+PRESETS: STEP PITCH";}
        break;
    case BB_NAV_KIT:
        rows[5]=drum?"PRESETS OR WHITE: KIT":"PRESETS OR WHITE: SOUND";rows[6]="EDIT: TAKE IT, THEN PATTERN";
        rows[7]="LIT: LOADED / BREATH: WAIT";rows[8]="K1: LEVEL";break;
    case BB_NAV_PATTERN:
        rows[5]="ALGO OR WHITE: PATTERN";rows[6]="EDIT, OR PLAY WHEN STOPPED";
        rows[7]="BLACK: SECTIONS / REC+OCT: 12+";rows[8]="OCT: MORE PATTERNS";break;
    case BB_NAV_SOUND:
        if(drum){rows[5]="WHITE 1-11: CHOOSE VOICE";rows[6]="EDIT: BASIC / COLOUR";
            rows[7]="REC+PRESETS: VOICE";rows[8]="K1-K4: SHOWN CONTROLS";}
        else {rows[5]="K1 ACCENT K2 DRIVE";rows[6]="K3 SLIDE K4 WAVE";
            rows[7]="KEYS PLAY / REC+ENV: PRESET";rows[8]="OCT: KEY OCTAVE";}
        break;
    case BB_NAV_FX:
        rows[5]="EDIT OR OCT: NEXT BANK";rows[6]="ECHO TAPE SPRING MOTION SENDS";
        rows[7]="REC+FX HOLD: DUB THROW";rows[8]="K1-K4: SHOWN CONTROLS";break;
    case BB_NAV_SAVE:
        rows[5]="WHITE 1-4 OR K1: SLOT";rows[6]="EDIT: STORE (WHEN STOPPED)";
        rows[7]="REC+WHITE 1-4: LOAD";rows[8]="REC+SAVE: QUICK SAVE";break;
    case BB_NAV_EVOLVE:
        rows[5]="EDIT: CAPTURE / REC+LFO: ON";rows[6]="WHITE 1-10: PARTS / CHANGES";
        rows[7]="BLACK 1 FREEZE / 2 RETURN";rows[8]="REC+WHITE: LOCK A STEP";break;
    default:
        rows[5]="K1 BPM / K2 SWING";rows[6]="K3 A/B BALANCE / K4 TRANSPOSE";
        rows[7]="WHITE 1 ANIM 2 SCOPE 3 METERS";rows[8]="REC+GLO: TAP TEMPO";break;
    }
    return title;
}
