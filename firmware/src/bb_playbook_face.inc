/* SPDX-License-Identifier: GPL-3.0-only
 * Main-loop-only playing face. Reuses the owned 7200-byte mask and the two
 * native strips. No audio rendering, allocation, framebuffer or input edits. */
/* Enabled for this isolated source candidate; not an installed setting. */
static uint8_t bb_playbook_enabled=1;
#if BB_WITH_EVOLVE
static int bb_pb_evo_active(void);
#endif
static void bb_pb_pixel(int x,int y,unsigned strip,uint16_t ink) {
    if(x>=0&&x<240&&y>=(int)strip&&y<(int)strip+120)cv_pset(x,y-(int)strip,ink);
}
static void bb_pb_text(int x,int y,unsigned strip,const aafont_t *font,const char *s,uint16_t ink) {
    if(y<(int)strip||y+(int)font->h+1>(int)strip+120)return;
    /* Glyph-sized shadow, not a solid header band. */
    cv_text(x+1,y+1-(int)strip,font,s,BB_BG);cv_text(x,y-(int)strip,font,s,ink);
}
static void bb_pb_center(int x,int y,unsigned strip,const aafont_t *font,const char *s,uint16_t ink) {
    int width=text_w(font,s),left=x-width/2;
    if(left<5)left=5;if(left+width>234)left=234-width;
    bb_pb_text(left,y,strip,font,s,ink);
}
static void bb_pb_ring(int x,int y,unsigned radius,unsigned strip,uint16_t ink) {
    int a=(int)radius,b=0,error=1-(int)radius;
    while(a>=b) {
        bb_pb_pixel(x+a,y+b,strip,ink);bb_pb_pixel(x+b,y+a,strip,ink);
        bb_pb_pixel(x-b,y+a,strip,ink);bb_pb_pixel(x-a,y+b,strip,ink);
        bb_pb_pixel(x-a,y-b,strip,ink);bb_pb_pixel(x-b,y-a,strip,ink);
        bb_pb_pixel(x+b,y-a,strip,ink);bb_pb_pixel(x+a,y-b,strip,ink);
        b++;if(error<0)error+=2*b+1;else{a--;error+=2*(b-a)+1;}
    }
}
static void bb_pb_motion(void) {
    int live=bb_face.animate&&bb_face.requested.running;
    uint32_t tick=bb_ticks;
    if(!live) {memset(bb_pb.wave,0,sizeof bb_pb.wave);bb_pb.strength=0;bb_pb.tick=tick;bb_pb.seen=1;return;}
    unsigned target=bb_visual_peak/128u;if(target>127u)target=127u;
    bb_pb.strength=bb_throb_step(bb_pb.strength,target,fm1_ms-bb_pb.last_draw);
    if(bb_pb.seen&&tick&&tick!=bb_pb.tick&&(tick-1u)/4u!=(bb_pb.tick?bb_pb.tick-1u:0u)/4u) {
        bb_pb.wave[bb_pb.slot++%4u]=(bb_pb_wave){fm1_ms,1};
    }
    bb_pb.tick=tick;bb_pb.seen=1;
    for(unsigned i=0;i<4;i++)if((uint32_t)(fm1_ms-bb_pb.wave[i].born)>=900u)bb_pb.wave[i].active=0;
}
static uint16_t bb_pb_ink(unsigned y) {
    unsigned energy=bb_pb.strength,t=y*127u/239u;
    return bb_face.requested.page==BB_TELEGRAM?
        RGB(70u+energy/3u,154u+t/4u+energy/4u,159u+t/3u):
        RGB(208u+energy/4u,129u+t/3u+energy/4u,76u+t/3u+energy/4u);
}
static void bb_pb_art(unsigned strip) {
    if(ux.light||ux.mono||ux.bg!=BB_BG){ux.light=0;ux.mono=0;ux.bg=BB_BG;ux.gen++;}
    cv_begin(240,120,BB_BG);
    for(unsigned y=strip;y<strip+120;y++) {
        uint16_t ink=bb_pb_ink(y);
        for(unsigned x=0;x<240;x++){unsigned bit=y*240u+x;
            if(bb_armadillo[bit>>3]&(1u<<(bit&7u)))cv_pset((int)x,(int)(y-strip),ink);}
    }
    /* Four bounded outgoing quarter-note rings; not a spectrogram. Colour
     * strength follows published output peak, ring position follows ticks. */
    if(!strip)for(unsigned i=0;i<4;i++)if(bb_pb.wave[i].active) {
        unsigned age=fm1_ms-bb_pb.wave[i].born,r=6u+age*68u/900u,fade=900u-age;
        uint16_t ink=RGB(34u+fade*120u/900u,42u+fade*69u/900u,42u+fade*24u/900u);
        bb_pb_ring(120,12,r,strip,ink);
        static const int8_t ray[5][2]={{-29,8},{-21,21},{0,30},{21,21},{29,8}};
        for(unsigned j=0;j<5;j++)bb_pb_pixel(120+ray[j][0]*(int)(r+4u)/30,
            12+ray[j][1]*(int)(r+4u)/30,strip,BB_SPARK_EMBER);
    }
}
static void bb_pb_dial(unsigned i,unsigned strip,const char *label,const char *value,unsigned position,int active) {
    static const uint8_t xs[4]={30,210,45,125},ys[4]={109,109,204,204};
    static const int8_t tips[17][2]={{-13,13},{-16,10},{-18,4},{-18,-2},{-17,-7},{-14,-12},{-10,-16},{-5,-18},{0,-19},{5,-18},{10,-16},{14,-12},{17,-7},{18,-2},{18,4},{16,10},{13,13}};
    int x=xs[i],y=ys[i];uint16_t ink=active?bb_instrument_ink():BB_DIM;
    bb_pb_ring(x,y,20,strip,BB_ART);unsigned slot=(position>127u?127u:position)*16u/127u;
    for(unsigned n=0;n<=slot;n++)bb_pb_pixel(x+tips[n][0],y+tips[n][1],strip,ink);
    char caption[16]={'K',(char)('1'+i),' ',0};unsigned n=3;
    while(*label&&n+1<sizeof caption)caption[n++]=*label++;caption[n]=0;
    bb_pb_center(x,y-31,strip,&AF_S,caption,BB_TEXT);
    const aafont_t *font=text_w(&AF_M,value)<=53?&AF_M:&AF_S;
    bb_pb_center(x,y-10,strip,font,value,BB_TEXT);
}
typedef struct {const char *label;unsigned value,position;} bb_pb_knob_readout;
static bb_pb_knob_readout bb_pb_synth_readout(unsigned knob) {
    unsigned synth=bb_selected_synth();
    /* This playing face is inactive while REC is held. Use exactly the same
     * non-REC descriptor as the sound-knob input handler and browser strip. */
    const bb_knob_descriptor *control=bb_synth_control(synth,bb_tone_page,knob,0);
    bb_pb_knob_readout result={"--",0,0};
    if(!control)return result;
    result.label=control->label;
    result.value=bb_synth_control_value(&bb_face.requested,synth,control);
    unsigned raw=0,lo=0,hi=127;
    if(control->parameter==BB_UI_LEVEL){raw=bb_face.requested.band_level[synth+1u];result.value=(raw*100u+63u)/127u;}
    else if(control->parameter==BB_UI_LENGTH){raw=bb_face.requested.synths[synth].length;lo=1;hi=64;}
    else {
        raw=bb_synth_parameter_get(&bb_face.requested,synth,control->parameter);
        if(!bb_synth_parameter_limits(synth,control->parameter,&lo,&hi))return result;
    }
    if(hi>lo){if(raw<lo)raw=lo;if(raw>hi)raw=hi;result.position=(raw-lo)*127u/(hi-lo);}
    return result;
}
static void bb_pb_draw(void) {
    bb_sound_picker owner={0};owner.ready=1;owner.page=bb_face.requested.page;owner.machine=bb_face.machine;
    bb_sound_picker_view sound;int sound_ready=bb_sound_picker_observe(&owner,&bb_sound,&sound);
    bb_group_view groups;int drums=owner.page==BB_DRUMS;
    int groups_ready=drums&&bb_group_input(&bb_sound.commands,bb_sound.groups,owner.machine,BB_GROUP_READ,0,0,&groups)!=BB_GROUP_REFUSED;
    const char *sound_name=!sound_ready?"STATE UNAVAILABLE":sound.active_index<0?"CUSTOM SOUND":bb_sound_picker_name((bb_page)owner.page,(unsigned)sound.active_index);
    bb_player *player=bb_library_current();char pattern[128]="CURRENT PATTERN",part[128]="";
    if(player->playing.ticket&&player->playing.ticket==player->status.applied) {
        const bb_factory_choice *choice=&player->playing.choice;
        bb_library_text(bb_catalogue_find(&bb_factory_catalogue,choice->bank,choice->group?"song":"name"),&AF_M,224,pattern);
        bb_library_text(bb_catalogue_find(&bb_factory_catalogue,choice->part,"name"),&AF_S,157,part);
    } else {
        const bb_performer *band=bb_home_view();
        if(band->playing.ticket&&band->playing.ticket==band->status.bank.applied) {
            bb_library_text(bb_catalogue_find(&bb_factory_catalogue,band->playing.choice.bank,"name"),&AF_M,224,pattern);
            part[0]='B';part[1]='A';part[2]='N';part[3]='D';part[4]=' ';part[5]=(char)('A'+band->status.active);part[6]=0;
        }
    }
    for(unsigned strip=0;strip<240;strip+=120) {
        bb_pb_art(strip);
        bb_pb_text(7,4,strip,&AF_M,drums?"BIG BEAT":owner.page==BB_DILLO?"DILLO":"DILLO LEAD",bb_instrument_ink());
        bb_pb_text(151,7,strip,&AF_S,bb_face.requested.running?"PLAY":"STOP",BB_GREEN);
        char bpm[12];fmt_int(bpm,bb_face.requested.bpm);bb_pb_text(195,4,strip,&AF_M,bpm,BB_TEXT);
        bb_pb_text(7,27,strip,&AF_S,"ALGORITHM / PATTERN",BB_DIM);
        bb_pb_text(7,40,strip,&AF_M,pattern,BB_TEXT);
        bb_pb_text(7,60,strip,&AF_S,part,BB_DIM);
        for(unsigned i=0;i<4;i++) {
            static const char *const dlabels[]={"KICK","SN/CL","HATS","PERC"};
            unsigned value=0,position=0;char text[16];const char *state=NULL,*label=dlabels[i];
            (void)label;label=bb_nav_knob_label(i);   /* navigation v2.1: bb_nav's K label */
            if(drums) {
                if(!groups_ready)state="BUSY";
                else if(groups.pending&(1u<<i))state="WAIT";
                else {value=groups.level[i];position=value;if(groups.muted&(1u<<i))state="MUTE";}
                fmt_int(text,(int32_t)((value*100u+63u)/127u));
            } else {
                bb_pb_knob_readout shown=bb_pb_synth_readout(i);
                value=shown.value;position=shown.position;
                fmt_int(text,(int32_t)value);
            }
            bb_pb_dial(i,strip,label,state?state:text,position,!state);
        }
        bb_pb_text(7,141,strip,&AF_S,drums?"PRESETS / KIT":"PRESETS / SOUND",BB_DIM);
        const aafont_t *font=&AF_S;
        char title[128];/* Native text fitting, never a fake factory identity. */
        unsigned n=0;while(sound_name[n]&&n+1<sizeof title){title[n]=sound_name[n];n++;}title[n]=0;
        while(n&&text_w(font,title)>169)title[--n]=0;
        if(sound_name[n]&&n>2){title[n-2]='.';title[n-1]='.';}
        bb_pb_text(7,154,strip,font,title,BB_TEXT);
        if(sound_ready&&(!sound.enabled||sound.muted||sound.edited))bb_pb_text(179,154,strip,&AF_S,!sound.enabled?"OFF":sound.muted?"MUTE":"EDIT",BB_DIM);
        if(drums){char layer[]="KIT A";layer[4]=(char)('A'+owner.machine);bb_pb_text(181,211,strip,&AF_S,layer,BB_DIM);}
        else bb_pb_text(181,211,strip,&AF_S,owner.page==BB_DILLO?(bb_face.requested.dillo.wave?"SAW":"SQUARE"):(bb_face.requested.telegram.glide?"GLIDE":"KEYS"),BB_DIM);
        bb_pb_text(7,224,strip,&AF_S,bb_hint_until?bb_hint:bb_player_pending(player)?"PATTERN REQUEST PENDING":!drums&&bb_nav_scale_hint()?bb_nav_scale_hint():"SEQ: STEPS / HOME: STUDIO",BB_DIM);
        cv_blit(0,strip);
    }
    if(groups_ready&&groups.pending)bb_dirty=1;
}
static void bb_pb_frame(void) {
    int animated=bb_face.animate&&bb_face.requested.running;
    if(bb_hint_until&&(int32_t)(fm1_ms-bb_hint_until)>=0){bb_hint_until=0;bb_hint="";bb_dirty=1;}
    if(!bb_dirty&&!animated)return;
    if(bb_pb.seen&&(uint32_t)(fm1_ms-bb_pb.last_draw)<50u)return;
    bb_pb_motion();bb_pb.last_draw=fm1_ms;bb_dirty=0;bb_pb_draw();
}
static int bb_pb_active(void) {
    return bb_playbook_enabled&&bb_sound_picker_enabled&&bb_play_face_visible()&&
        !bb_scope_enabled&&!bb_help_visible&&!ui.uboot&&!(fm1_in.buttons&(1u<<panel.btn[B_REC]));
}
static void bb_pb_own_workspace(void) {
    unsigned owner=bb_pb_active()!=0;
#if BB_WITH_EVOLVE
    owner|=bb_pb_evo_active()!=0;
#endif
    if(owner!=bb_visual_workspace_owner) {
        /* Both renderers run only on the main loop. Discard decorative
         * history before interpreting the union's other member. No ISR or
         * pending musical transaction owns any byte of this workspace. */
        memset(&bb_visual_workspace,0,sizeof bb_visual_workspace);
        bb_visual_workspace_owner=(uint8_t)owner;bb_dirty=1;
    }
}

/* SPDX-License-Identifier: GPL-3.0-only
 * Full-height Evolution face. Existing part/attribute keys and commands stay
 * unchanged. Protected-step editing retains its dedicated readable grid. */
#if BB_WITH_EVOLVE
static int bb_pb_evo_active(void) {
    return bb_playbook_enabled&&bb_evo_page&&!bb_evo_rec&&!ui.uboot&&!bb_help_visible;
}
static void bb_pb_evo_plate(int x,int y,unsigned strip,unsigned w,unsigned h) {
    int top=y>(int)strip?y:(int)strip;
    int bottom=y+(int)h<(int)strip+120?y+(int)h:(int)strip+120;
    if(bottom>top)cv_rect(x,top-(int)strip,w,(unsigned)(bottom-top),BB_BG);
}
static void bb_pb_evo_text(int x,int y,unsigned strip,const aafont_t *font,const char *label,uint16_t ink) {
    bb_pb_evo_plate(x-1,y,strip,(unsigned)text_w(font,label)+3u,font->h);
    bb_pb_text(x,y,strip,font,label,ink);
}
static void bb_pb_choice(int x,int y,unsigned strip,unsigned width,const char *label,int on) {
    uint16_t ink=on?bb_instrument_ink():BB_DIM;
    bb_pb_evo_plate(x,y,strip,width,16);
    for(unsigned i=0;i<width;i++){bb_pb_pixel(x+(int)i,y,strip,ink);bb_pb_pixel(x+(int)i,y+15,strip,ink);}
    for(unsigned i=0;i<16;i++){bb_pb_pixel(x,y+(int)i,strip,ink);bb_pb_pixel(x+(int)width-1,y+(int)i,strip,ink);}
    bb_pb_text(x+3,y+1,strip,&AF_S,label,ink);
}
static void bb_pb_evo_draw(void) {
    static const char *const roots[]={"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};
    static const char *const parts[]={"1 KIT A","2 KIT B","3 KIT C","4 KIT D","5 DILLO","6 TELEGR"};
    static const char *const attrs[]={"7 RHY","8 NOTE","9 ACC","10 SLD"};
    static const uint8_t row[3]={85,103,123};
    int queued=0;unsigned applied=bb_evo_applied_amount(&queued);
    const char *status=bb_evo_status(queued);
    if(!queued&&bb_evo_view.ready&&bb_evo_view.enabled&&!bb_evo_view.frozen&&
       !bb_evo_view.pending&&!bb_evo_view.conflict&&!bb_face.rejected&&bb_evo_view.result!=BB_EV_RESULT_REJECTED)
        status=!applied?"ZERO: NO NEW CHANGES":!bb_face.requested.running?"ARMED: PRESS PLAY":status;
    for(unsigned strip=0;strip<240;strip+=120) {
        bb_pb_art(strip);
        bb_evolution_head_at(strip,120,bb_pb_ink(180),1);
        bb_pb_text(7,4,strip,&AF_M,"EVOLVE",bb_instrument_ink());
        bb_pb_text(151,7,strip,&AF_S,bb_face.requested.running?"PLAY":"STOP",BB_GREEN);
        bb_pb_text(7,25,strip,&AF_S,status,BB_TEXT);
        bb_pb_evo_text(7,48,strip,&AF_S,"K2 EVERY",BB_DIM);
        bb_pb_evo_text(91,48,strip,&AF_S,"K3 ROOT",BB_DIM);
        bb_pb_evo_text(165,48,strip,&AF_S,"K4 SCALE",BB_DIM);
        char number[12];fmt_int(number,bb_evo_view.settings.every_phrases);
        bb_pb_evo_text(7,63,strip,&AF_M,number,BB_TEXT);
        bb_pb_evo_text(91,63,strip,&AF_M,roots[bb_evo_view.settings.root%12u],BB_TEXT);
        bb_pb_evo_text(165,65,strip,&AF_S,bb_evo_view.settings.scale==0x5ad?"MINOR":bb_evo_view.settings.scale==0xab5?"MAJOR":"CUSTOM",BB_TEXT);
        for(unsigned i=0;i<6;i++)bb_pb_choice(7+(int)(i%2)*77,row[i/2],strip,72,parts[i],bb_evo_view.settings.parts&(1u<<i));
        for(unsigned i=0;i<4;i++)bb_pb_choice(7+(int)(i%2)*77,143+(int)(i/2)*18,strip,72,attrs[i],bb_evo_view.settings.attributes&(1u<<i));
        /* No extra knob on the logo. K1's requested number is explicit;
         * only the applied amount turns the original head below it. */
        bb_pb_evo_text(166,123,strip,&AF_S,"K1 AMOUNT",BB_DIM);
        fmt_int(number,bb_evo_view.ready?bb_evo_view.settings.amount:0);
        bb_pb_evo_text(207-text_w(&AF_M,number)/2,138,strip,&AF_M,number,BB_TEXT);
        /* The top status already says CONTROL QUEUED. Do not lay a second
         * status plate over the applied head: a queued request is not applied. */
        bb_pb_evo_text(7,181,strip,&AF_S,"REC+LFO: ON / EDIT: CAPTURE",BB_DIM);
        bb_pb_evo_text(7,199,strip,&AF_S,"BLACK 1 FREEZE / 2 RETURN",BB_DIM);
        bb_pb_text(7,224,strip,&AF_S,bb_hint_until?bb_hint:"REC+WHITE: LOCK / HOME: STUDIO",BB_DIM);
        cv_blit(0,strip);
    }
}
static void bb_pb_evo_frame(void) {
    int live=bb_face.animate&&bb_face.requested.running;
    if(!bb_dirty&&!live)return;
    if(bb_pb.seen&&(uint32_t)(fm1_ms-bb_pb.last_draw)<50u)return;
    bb_pb_motion();bb_pb.last_draw=fm1_ms;bb_dirty=0;bb_pb_evo_draw();
}
#endif
