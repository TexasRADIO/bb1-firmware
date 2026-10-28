/* Main-loop source display. No catalogue decoding, audio mutation or UI clock. */
#if BB_WITH_RICH_DRUM_BROWSER
static bb_phrase_view bb_rich_views[BB_RICH_DRUMS];
static bb_source_tone_view bb_rich_tones[BB_RICH_DRUMS];
static uint8_t bb_rich_voices[BB_RICH_DRUMS];
static const uint8_t bb_rich_sources[17]={
    BB_FDV_BD,BB_FDV_SD,BB_FDV_LT,BB_FDV_MT,BB_FDV_HT,BB_FDV_LC,BB_FDV_MC,BB_FDV_HC,
    BB_FDV_RS,BB_FDV_C,BB_FDV_CP,BB_FDV_MA,BB_FDV_CB,BB_FDV_CY,BB_FDV_OHH,BB_FDV_HH,BB_PHRASE_SHAKER};
static const uint8_t bb_rich_lanes[17]={0,1,2,3,4,2,3,4,5,5,6,6,7,8,9,10,6};
/* Canonical16-sound pot order, not physical lane mode. SH deliberately shares
 * the retained maraca tone; this does not invent another DSP voice. */
static const uint8_t bb_rich_sounds[17]={0,1,2,3,4,5,6,7,8,9,11,10,12,15,14,13,10};
static const char *const bb_rich_names[17]={
    "KICK","SNARE","LOW TOM","MID TOM","HIGH TOM","LOW CONGA","MID CONGA","HIGH CONGA",
    "RIM","CLAVES","CLAP","MARACAS","COWBELL","CYMBAL","OPEN HAT","CLOSED HAT","SHAKER"};
static void bb_rich_reset(void){memset(bb_rich_views,0,sizeof bb_rich_views);memset(bb_rich_tones,0,sizeof bb_rich_tones);memset(bb_rich_voices,0,sizeof bb_rich_voices);}
static int bb_rich_active(void){return bb_face.machine<BB_RICH_DRUMS&&bb_rich_views[bb_face.machine].valid;}
static unsigned bb_drum_visible_length(const bb_machine *m){return bb_rich_active()?bb_rich_views[bb_face.machine].phrase.length:m->length;}
static void bb_rich_refresh(void){
    for(unsigned m=0;m<BB_RICH_DRUMS;m++){
        if(bb_audio_phrase_view_read(&bb_sound,m,&bb_rich_views[m])==2)bb_dirty=1;
        if(bb_audio_source_tone_view_read(&bb_sound,m,&bb_rich_tones[m])==2){
            /* Reconcile accepted OR refused requests only once the real queue
             * is idle. Never leave a rejected optimistic knob value on screen. */
            bb_face.requested.drums[m].sound=bb_rich_tones[m].sound;
            bb_face.requested.drums[m].sound_revision=bb_rich_tones[m].revision;bb_dirty=1;
        }
    }
    if(bb_face.requested.page==BB_DRUMS&&bb_rich_active()&&bb_face.bank*16u>=bb_drum_visible_length(&bb_face.requested.drums[bb_face.machine]))bb_face.bank=0;
}
static void bb_rich_select(unsigned choice){
    if(!bb_rich_active()||choice>=17)return;
    bb_rich_voices[bb_face.machine]=(uint8_t)choice;
    bb_face.voice=bb_rich_lanes[choice];bb_dirty=1;
}
static void bb_rich_move(int delta){
    if(!bb_rich_active())return;
    int choice=(int)bb_rich_voices[bb_face.machine]+delta;
    if(choice<0)choice=0;if(choice>16)choice=16;
    bb_rich_select((unsigned)choice);
}
static int bb_rich_cell(unsigned step,bb_phrase_cell *cell){
    if(!bb_rich_active())return 0;
    return bb_phrase_view_cell(&bb_rich_views[bb_face.machine],bb_rich_sources[bb_rich_voices[bb_face.machine]],step,cell);
}
#else
static void bb_rich_reset(void){}
static void bb_rich_refresh(void){}
static int bb_rich_active(void){return 0;}
static unsigned bb_drum_visible_length(const bb_machine *m){return m->length;}
#endif
