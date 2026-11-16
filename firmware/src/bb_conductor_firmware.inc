                                        
                                                                                             
                                                                                             
                                                                                                                     
                                                                                                                    
                                                                                                              
                                                                                                                  
                                                                                                                      
                                                                                                                     
                                                                                                                  
                                                                                                                      
                                                                                                                      
                                                                                            
                                                                                                           
                                                                                           
                                                                                                             
                                                                                                                   
                                                                                                                  
                                                                       
#include "bb/bb_chord.h"
static struct {
    bb_section_bundle stage;                 /* 1,432 B: copied by the owner during submit                        */
    bb_browser list;                         /* the SONGS / ERAS / PRESETS list                                      */
    bb_player_selection prev[3];             /* each moved part's request before the cue (restored if it ends)    */
    uint32_t ticket;                         /* the cue in flight, 0 = none                                       */
    bb_catalogue_id pending_part;            /* section label bound to that ticket */
    uint8_t welcome, was_running, performed; /* start card; transport edge; a band heard at least once */
    uint8_t take, coll;                      /* take is fixed to all three; collection 0 SONGS 1 ERAS 2 PRESETS     */
    uint8_t moved;                           /* parts carrying the ticket                                         */
    uint8_t key, want;                       /* the cue's section; a newer section waiting for the handoff (+1)    */
    uint8_t retried, chose;                  /* LATE: retargeted once; the cue is a CHOOSE (no retarget)         */
    uint8_t cancel;                          /* a cancel asked for the cue in flight: 1 sent, 2 owed (queue full)  */
    uint8_t vpage, cursor;                   /* 16-key page and selected row in the active collection */
    uint64_t qset, pset, wset;               /* RC3 combining: the cue's sections, the applied combination, a newer one */
    uint32_t pticket;                        /* the ticket that applied pset (still playing while a player carries it) */
    bb_factory_choice group_choice;         /* last acknowledged whole-band cue */
    bb_factory_choice group_pending;        /* choice bound to the current whole-band ticket */
    bb_chord chord;                          /* RC3: the PERFORM chord gesture (bb/bb_chord.h)                      */
} bb_cd __attribute__((section(".pool")));
/* Scene/project restore replaces the players and their audio ownership. Keep
 * the catalogue cursor, but discard every cue associated with the old audio. */
static void bb_cd_reset_playback(void) {
    bb_cd.ticket=bb_cd.pticket=0;
    bb_cd.moved=bb_cd.key=bb_cd.want=bb_cd.cancel=0;
    bb_cd.retried=bb_cd.chose=0;
    bb_cd.performed=0;
    bb_cd.qset=bb_cd.pset=bb_cd.wset=0;
    bb_cd.chord.active=0;
}
static bb_player *bb_cd_player(unsigned q){return q<2u?&bb_drum_library[q]:&bb_library[0];}
static unsigned bb_cd_focus(void){return bb_nav_row<2u?bb_nav_row:2u;}
/* A seamless drum edit changes the playback ticket but retains its launch source. */
static const bb_player_selection *bb_cd_applied(const bb_player *p,unsigned lane){
    if(p->playing.ticket)return &p->playing;
    if(lane<2u&&bb_rich_views[lane].valid){
        uint32_t root=bb_rich_views[lane].root;
        if(root&&p->request.ticket==root)return &p->request;
        if(root&&bb_cd.ticket&&(bb_cd.moved&(1u<<lane))&&bb_cd.prev[lane].ticket==root)
            return &bb_cd.prev[lane];
    }
    return NULL;
}
/* What a part plays, or what it is about to play (its own pending request). NULL: nothing. */
static const bb_factory_choice *bb_cd_ref(unsigned q) {
    const bb_player *p=bb_cd_player(q);
    if(!p->ready)return 0;
    if(bb_player_pending(p))return &p->request.choice;
    const bb_player_selection *playing=bb_cd_applied(p,q);
    return playing?&playing->choice:0;
}
/* Set a canonical cursor. All callers use the same verified catalogue. */
static void bb_cd_cursor(bb_browser *b,unsigned group,unsigned bank,unsigned part){
    *b=(bb_browser){.catalogue=&bb_factory_catalogue,.ready=1,.group=(uint8_t)group,
        .bank_index=(uint16_t)bank,.part_index=(uint16_t)part,
        .bank_page=(uint16_t)(bank/BB_BROWSER_BANK_KEYS),
        .part_page=(uint16_t)(part/BB_BROWSER_PART_KEYS)};
}
static char bb_cd_text[32];   /* main-loop refusal notice */
static void bb_cd_refused(unsigned q,const char *why) {
    char *text=bb_cd_text;
    static const char *const name[3]={"A","B","DILLO"};
    const char *s="CUE REFUSED: ";unsigned n=0;
    while(*s)text[n++]=*s++;
    for(s=name[q];*s;)text[n++]=*s++;
    text[n++]=' ';
    while(*why&&n<31u)text[n++]=*why++;
    text[n]=0;bb_say(text);
}
/* SONG uses the same conversion as the live cue. */
static const char *bb_cd_prepare_drums(const bb_factory_choice *anchor,
                                       uint64_t set,bb_phrase_index *out){
    if(!(set&(set-1u)))
        return bb_factory_prepare_rich_drum(&bb_factory_catalogue,anchor,out)==BB_FDRUM_OK?NULL:"NO DRUMS HERE";
    bb_factory_choice parts[4];
    unsigned count=0;
    while(set){
        unsigned index=(unsigned)__builtin_ctzll(set);
        set&=set-1u;
        if(count==4u||!bb_factory_choice_at(&bb_factory_catalogue,anchor->group,anchor->bank_index,index,&parts[count++]))
            return "TOO MANY TO COMBINE";
    }
    return bb_factory_prepare_rich_combo(&bb_factory_catalogue,parts,count,out)==BB_FDRUM_OK?NULL:"CAN'T COMBINE";
}
/* One commit to the chosen section for the parts in `move`; the others HOLD. */
static int bb_cd_submit(unsigned move,const bb_factory_choice *ch,uint64_t set) {
    bb_section_bundle *b=&bb_cd.stage;
    bb_section_status st;
    if(!move||!bb_audio_section_status_read(&bb_sound,&st))return 0;
    memset(b,0,sizeof *b);b->dillo.length=16;
    for(unsigned q=0;q<3u;q++) {
        if(!((move>>q)&1u))continue;
        const char *why=0;
        if(q<2u) {
            /* A shared cue uses the same immutable phrase on both kits.
             * Its timing admission is shared; each kit still owns its voices. */
            int shared=q==1u&&(move&1u);
            if(shared)b->drum[1]=b->drum[0];
            else why=bb_cd_prepare_drums(ch,set,&b->drum[q]);
            if(!why){
                if(bb_sound.drum_patterns.family[q])why="NEEDS A RICH KIT";
                else if(b->drum[q].voices&~bb_audio_phrase_source_voices_owned(&bb_sound,q))why="NEEDS VOICES";
                else if(!shared&&!bb_audio_phrase_preflight_playable(&b->drum[q],BB_RICH_REPRESENTABLE_VOICES))why="NEEDS TIMING";
            }
        } else if(bb_factory_prepare_synth_data(&bb_factory_catalogue,ch,0,&b->dillo)!=BB_FACTORY_PATTERN_OK)why="NO LINE HERE";
        if(why){bb_cd_refused(q,why);return 0;}                  /* all or nothing: the owner is not touched */
    }
    if(bb_cd.chose&&move==7u){
        unsigned bpm,swing;
        if(bb_factory_read_song_tempo(&bb_factory_catalogue,ch,&bpm,&swing)){
            b->bpm=(uint16_t)bpm;
            if(swing<=150u)b->swing=bb_swing_encode(swing);
        }
    }
    b->keep=(uint8_t)(7u&~move);b->section=1u+bb_cd.key;b->revision=1;b->project_revision=1;
    if(st.running){b->at_tick=1;b->target_tick=(st.current_tick|15u)+1u;}   /* the next bar (16 steps from Start) */
    uint32_t t;
    if(!bb_audio_section_submit(&bb_sound,b,&t)) {
        /* The audio owner can cross the prepared next-bar boundary between
         * the status snapshot above and publication here. That is not a busy
         * instrument and must not turn a playable key press into CUE REFUSED.
         * The failed submit is atomic; refresh the clock and publish once more
         * for the following bar. Persistent refusal still reports BUSY. */
        if(!st.running||!bb_audio_section_status_read(&bb_sound,&st))
            {bb_cd_refused(bb_cd_focus(),"BUSY");return 0;}
        b->target_tick=(st.current_tick|15u)+1u;
        if(!bb_audio_section_submit(&bb_sound,b,&t))
            {bb_cd_refused(bb_cd_focus(),"BUSY");return 0;}
    }
    bb_cd.ticket=t;bb_cd.moved=(uint8_t)move;bb_cd.qset=set;
    if(move==7u)bb_cd.group_pending=*ch;
    bb_cd.pending_part=ch->part;
    for(unsigned q=0;q<3u;q++)if((move>>q)&1u) {
        bb_player *p=bb_cd_player(q);
        bb_cd.prev[q]=p->request;
        memset(&p->request,0,sizeof p->request);p->request.choice=*ch;p->request.ticket=t;p->request_seen=0;
#if BB_WITH_RICH_DRUM_BROWSER
        if(q<2u)p->request_rich=1;
#endif
        bb_cd_cursor(&p->view,ch->group,ch->bank_index,ch->part_index);
    }
    bb_dirty=1;return 1;
}
/* A focused player can diverge without changing the band's section grid. */
static const bb_factory_choice *bb_cd_group_ref(void) {
    /* A chooser cue keeps the playing song's keys until it commits. */
    if(bb_cd.ticket&&bb_cd.moved==7u&&(!bb_cd.chose||!bb_cd.pticket))return &bb_cd.group_pending;
    return bb_cd.pticket?&bb_cd.group_choice:bb_cd_ref(0);
}
/* An empty return has the same PLAY promise as power-on. Keep the
 * current instrument or song in control whenever it owns playback. */
static void bb_cd_restore_empty_landing(int running) {
    if(running||bb_cd.performed)return;
    bb_nav.bank=BB_NAV_HOME_SONGS;
    bb_cd.welcome=1;
}
static int bb_cd_cue_on_grid(const bb_factory_choice *group) {
    return !bb_cd.chose || (group && group->bank == bb_cd.group_pending.bank);
}
static int bb_cd_group_linked(void) {
    uint32_t t=bb_cd.pticket;
    return t&&bb_cd_player(0)->playing.ticket==t&&bb_cd_player(1)->playing.ticket==t&&
           bb_cd_player(2)->playing.ticket==t;
}
/* Dillo takes the lead section of a combination. A drum kit retains its
 * mix independently after a redirect and keeps its source after a step edit. */
static unsigned bb_cd_combo_players(void) {
    unsigned mask=0;
    if(bb_cd.pset)
        for(unsigned q=0;q<2u;q++){
            const bb_player_selection *playing=bb_cd_applied(bb_cd_player(q),q);
            if(playing&&playing->ticket==bb_cd.pticket)mask|=1u<<q;
        }
    return mask;
}
static void bb_cd_go_mask(unsigned n,uint64_t set,unsigned move) {
    const bb_factory_choice *f=bb_cd_group_ref();
    bb_factory_choice ch;
    if(!f){bb_say(BB_TX(TX_CHOOSE_PATTERN));return;}
    if(!bb_factory_choice_at(&bb_factory_catalogue,f->group,f->bank_index,n,&ch)){
        bb_say(BB_TX(TX_NO_SUCH_SECTION));return;
    }
    bb_cd.key=(uint8_t)n;bb_cd.chose=0;
    (void)bb_cd_submit(move,&ch,set);
}
static void bb_cd_go_set(unsigned n,uint64_t set){bb_cd_go_mask(n,set,7u);}
static void bb_cd_redirect(unsigned n) {
    if(bb_nav.conduct_scope==BB_NAV_CONDUCT_ALL)return;
    if(bb_nav_p1(BB_P1_SONG_ARMED,0,0)){bb_say("SONG PLAYING");return;}
    if(bb_cd.ticket||bb_cd.want||bb_cd.chord.active){bb_say(BB_TX(TX_SOUND_REQUEST_STILL_PENDING));return;}
    bb_cd.retried=1;                       /* a retry must never widen a redirect */
    bb_cd_go_mask(n,0,1u<<(bb_nav.conduct_scope-1u));
}
static void bb_cd_go(unsigned n){bb_cd_go_set(n,0);}
static void bb_cd_key(unsigned n) {
    if(bb_nav_p1(BB_P1_SONG_ARMED,0,0)){bb_say("SONG PLAYING");return;}
    const bb_factory_choice *group=bb_cd_group_ref();
    bb_cd.retried=0;                                   /* a press is a new gesture: one retarget again */
    if(bb_cd.ticket) {                                   /* a cue waits: same key or the playing key cancels, else replace */
        int cancel=bb_cd.moved==7u&&((n==bb_cd.key&&!bb_cd.qset&&bb_cd_cue_on_grid(group))||
            (bb_cd_group_linked()&&bb_cd.group_choice.part_index==n));
        bb_cd.want=(uint8_t)(cancel?0u:n+1u);
        bb_cd.cancel=bb_audio_section_cancel_submit(&bb_sound,bb_cd.ticket)?1u:2u;   /* refused: owed, bb_cd_poll retries */
        return;
    }
    if(bb_cd_group_linked()&&group&&group->part_index==n&&!bb_cd.pset)return;   /* already playing */
    bb_cd_go(n);
}
/* RC3: the combination now: the cue's, else the applied one while the focused part still plays it, else its section */
static uint64_t bb_cd_cur(void) {
    const bb_factory_choice *group=bb_cd_group_ref();
    if(bb_cd.ticket&&bb_cd.moved==7u&&bb_cd_cue_on_grid(group))
        return bb_cd.qset?bb_cd.qset:(uint64_t)1<<(bb_cd.key&63u);
    return group?(bb_cd_combo_players()?bb_cd.pset:(uint64_t)1<<(group->part_index&63u)):0;
}
/* RC3: the PERFORM chord, every scan: `held` = the white keys down, `now` = ms. Commits once per gesture. */
static void bb_cd_set(uint64_t m);
static void bb_cd_chord(uint32_t held,uint32_t now){uint64_t m=bb_chord_poll(&bb_cd.chord,held,now,bb_cd_cur());if(m)bb_cd_set(m);}
/* RC3: one gesture's sections. One section: bb_cd_key (every RC1/RC2 rule). Several: one cue for the combination; the
 * same combination again while it waits cancels it; another replaces it (cancel, then this one, as for a key). */
static void bb_cd_set(uint64_t m) {
    unsigned n=(unsigned)__builtin_ctzll(m);
    if(!(m&(m-1u))){bb_cd_key(n);return;}
    if(bb_nav_p1(BB_P1_SONG_ARMED,0,0)){bb_say("SONG PLAYING");return;}
    bb_cd.retried=0;
    if(bb_cd.ticket) {
        bb_cd.wset=bb_cd.moved==7u&&m==bb_cd.qset?0u:m;bb_cd.want=bb_cd.wset?0xFFu:0u;
        bb_cd.cancel=bb_audio_section_cancel_submit(&bb_sound,bb_cd.ticket)?1u:2u;
        return;
    }
    if(bb_cd_group_linked()&&m==bb_cd_cur())return;                           /* already playing */
    bb_cd_go_set(n,m);
}
/* The four Roland banks are consecutive in the compiled catalogue.
 * The catalogue audit binds these indices and part counts to the packed bytes.
 * PRESETS exposes each rhythm in order while preserving its bank and part IDs. */
enum { BB_CD_PRESET_FIRST = 21, BB_CD_PRESET_BANKS = 4 };
static const uint8_t bb_cd_preset_start[] = {0, 43, 54, 108, 138};

static unsigned bb_cd_count(void)
{
    if (bb_cd.coll == 2u)
        return bb_cd.list.ready ? bb_cd_preset_start[BB_CD_PRESET_BANKS] : 0u;
    unsigned count = (unsigned)bb_browser_bank_count(&bb_cd.list);
    return bb_cd.coll == 0u && count >= BB_CD_PRESET_BANKS ? count - BB_CD_PRESET_BANKS : count;
}

/* Packed bank and part for a valid display row. SONGS and ERAS use the
 * initial part when chosen, so only PRESETS needs the low byte. */
static unsigned bb_cd_row(unsigned row)
{
    if (bb_cd.coll != 2u) {
        if (!bb_cd.coll && row >= BB_CD_PRESET_FIRST) row += BB_CD_PRESET_BANKS;
        return row << 8;
    }
    unsigned shelf = 0;
    while (row >= bb_cd_preset_start[shelf + 1u]) shelf++;
    return ((BB_CD_PRESET_FIRST + shelf) << 8) | (row - bb_cd_preset_start[shelf]);
}

static unsigned bb_cd_choice_row(const bb_factory_choice *choice)
{
    if (!choice || choice->group != bb_cd.list.group) return BB_NAV_NONE_IDX;
    unsigned bank = choice->bank_index;
    if (bb_cd.coll == 1u) return bank;
    unsigned shelf = bank - BB_CD_PRESET_FIRST;
    if (bb_cd.coll == 2u)
        return shelf < BB_CD_PRESET_BANKS ? bb_cd_preset_start[shelf] + choice->part_index : BB_NAV_NONE_IDX;
    if (shelf < BB_CD_PRESET_BANKS) return BB_NAV_NONE_IDX;
    return bank < BB_CD_PRESET_FIRST ? bank : bank - BB_CD_PRESET_BANKS;
}

/* Only a whole-band cue belongs in the song chooser. */
static unsigned bb_cd_list_queued(void)
{
    return bb_cd.ticket && bb_cd.moved == 7u ? bb_cd_choice_row(&bb_cd.group_pending) : BB_NAV_NONE_IDX;
}

static void bb_cd_goto(unsigned row)
{
    unsigned count = bb_cd_count();
    if (!count) return;
    if (row >= count) row = count - 1u;
    unsigned cell = bb_cd_row(row);
    if (!bb_browser_move_bank(&bb_cd.list, (int)(cell >> 8) - (int)bb_cd.list.bank_index)) return;
    if (bb_cd.coll == 2u) {
        bb_cd.list.part_index = (uint8_t)cell;
        bb_cd.list.part_page = bb_cd.list.part_index / BB_BROWSER_PART_KEYS;
    }
    bb_cd.cursor = (uint8_t)row;
    bb_cd.vpage = (uint8_t)(row / 16u);
    bb_dirty = 1;
}

static void bb_cd_move(int d){int v=(int)bb_cd.cursor+d;bb_cd_goto(v<0?0u:(unsigned)v);}
static void bb_cd_page(int d){int p=(int)bb_cd.vpage+d;unsigned c=bb_cd_count();
    if(p>=0&&(unsigned)p*16u<c)bb_cd_goto((unsigned)p*16u);}
static int bb_cd_open_list(void){if(!bb_cd.welcome)return 0;bb_cd.welcome=0;bb_dirty=1;return 1;}
static void bb_cd_pick(unsigned key){if(bb_cd_open_list())return;unsigned v=bb_cd.vpage*16u+key;if(v<bb_cd_count())bb_cd_goto(v);}
static void bb_cd_collection(unsigned c) {                /* B5 SONGS, B6 ERAS, B7 PRESETS */
    if(!bb_browser_group(&bb_cd.list,c==1u?BB_BROWSER_ERAS:BB_BROWSER_SONGS))return;
    bb_cd.welcome=0;bb_cd.coll=(uint8_t)c;bb_cd_goto(0);
}
                                                                                                            
                                                                                                                   
                                                                                 
static void bb_cd_choose_start(int start) {
    bb_factory_choice ch;
    if(bb_cd.ticket){bb_say(BB_TX(TX_SOUND_REQUEST_STILL_PENDING));return;}
    /* The list holds the catalogue's initial section. Player cursors can differ. */
    if(!bb_browser_current(&bb_cd.list,&ch)){bb_say(BB_TX(TX_NO_SUCH_SECTION));return;}
    /* A song brings its sheet tempo and swing in the section ticket.
     * Reserve the extra queue slot only for a stopped PLAY. */
    if(BB_COMMAND_CAPACITY-bb_sound.commands.count<1u+(unsigned)start){bb_say(BB_TX(TX_BUSY_RETRY));return;}
    bb_cd.key=(uint8_t)ch.part_index;bb_cd.retried=0;bb_cd.chose=1;
    if(!bb_cd_submit(7u,&ch,0))return;
#if BB_WITH_FX_ANCHOR
    if(bb_nav_fx_marked)bb_panel_fx_recall(&bb_face,&bb_sound.commands,&bb_nav_fx_anchor);
#endif
    if(start)bb_send(BB_CMD_TRANSPORT,1u);               /* stopped: the section applied at once; PLAY starts on it */
    bb_cd.welcome=0;bb_nav.bank=BB_NAV_HOME_CONDUCT;bb_nav_project();
}
static void bb_cd_choose(void){if(!bb_cd_open_list())bb_cd_choose_start(0);}
/* PLAY on HOME/SONGS always means the promise printed by the list: load the
 * selected song on all three players.  When stopped it also starts; when
 * already playing it keeps transport running and cues the new song.  Return
 * HOME first when the player intends the ordinary start/stop control. */
static int bb_cd_play(void) {
    bb_section_status st;
    if(bb_nav.page!=BB_NAV_STUDIO||bb_nav.bank!=BB_NAV_HOME_SONGS||!bb_audio_section_status_read(&bb_sound,&st))return 0;
    bb_cd_choose_start(!st.running);return 1;
}
/* Every scan: follow the cue to APPLIED, or put the labels back and say why it ended; resubmit a newer key. */
static void bb_cd_poll(void) {
    bb_section_status st;
    uint8_t running=(uint8_t)!!bb_face.requested.running;
    if(running&&bb_cd.pticket)bb_cd.performed=1;
    if(bb_nav.page!=BB_NAV_STUDIO||bb_nav.bank!=BB_NAV_HOME_SONGS)bb_cd.welcome=0;
    /* BB1 2.3: return to the welcome card when stopped on SONGS.
     * The welcome card is the resting state — the RC mark, the song name,
     * PRESS PLAY. It comes back whenever transport stops on the HOME page. */
    if(bb_nav.page==BB_NAV_STUDIO&&bb_nav.bank==BB_NAV_HOME_SONGS&&
       bb_cd.was_running&&!running&&!bb_cd.ticket&&bb_cd.list.ready)
        bb_cd.welcome=1;
    bb_cd.was_running=running;
    if(!bb_cd.list.ready&&bb_browser_init(&bb_cd.list,&bb_factory_catalogue)){
        (void)bb_browser_group(&bb_cd.list,BB_BROWSER_SONGS);bb_cd.take=7;
    }
    if(!bb_cd.ticket){if(bb_cd.want==0xFFu){bb_cd.want=0;bb_cd_go_set((unsigned)__builtin_ctzll(bb_cd.wset),bb_cd.wset);}
                      else if(bb_cd.want){unsigned n=bb_cd.want-1u;bb_cd.want=0;bb_cd_go(n);}return;}
    if(!bb_audio_section_status_read(&bb_sound,&st))return;
    if(st.phase!=BB_SECTION_FREE) {                     /* still waiting for the bar; an owed cancel goes when it can */
        if(bb_cd.cancel==2u&&bb_audio_section_cancel_submit(&bb_sound,bb_cd.ticket))bb_cd.cancel=1;
        return;
    }
    uint32_t t=bb_cd.ticket;unsigned asked=bb_cd.cancel;bb_cd.ticket=0;bb_cd.cancel=0;
    uint64_t set=bb_cd.qset;bb_cd.qset=0;
    if(st.state.applied==t) {                           /* APPLIED: every moved part's player shows it (bb_player_sync) */
        /* A key from the old grid cannot be replayed on a different song. */
        if(asked&&!bb_cd_cue_on_grid(bb_cd_group_ref()))bb_cd.want=0;
        if(bb_cd.moved==7u){bb_cd.group_choice=bb_cd.group_pending;bb_cd.pset=set;bb_cd.pticket=t;}
        if(asked&&!bb_cd.want)bb_say("TOO LATE TO CANCEL");   /* the cancel did not land before the bar: say so */
        return;
    }
    for(unsigned q=0;q<3u;q++) {                         /* ended without applying: the labels go back */
        bb_player *p=bb_cd_player(q);
        if(((bb_cd.moved>>q)&1u)&&p->request.ticket==t)p->request=bb_cd.prev[q];
    }
    bb_dirty=1;
    if(bb_cd.want)return;                                /* replaced by a newer key: it goes next scan */
    if(st.error==BB_SECTION_ERROR_LATE&&st.running&&!asked&&!bb_cd.retried&&!bb_cd.chose)
        {bb_cd.retried=1;bb_cd_go_set(bb_cd.key,set);return;}   /* the complete cue, one more bar */
    bb_say(st.error==BB_SECTION_ERROR_LATE?"MISSED THE BAR":!st.running?bb_notice_text(BB_NOTICE_CANCELLED_BY_STOP):"CUE CANCELLED");
}
/* Facts for the HOME lights (bb_nav's one table): take / collection; on SONGS the list page; on CONDUCT the band's
 * song is the one in view, and a SONG arrangement in charge turns the section keys off. */
static void bb_cd_facts(bb_nav_facts *f) {
    f->home_take=7u;f->home_coll=bb_cd.coll;
    if(bb_nav.page!=BB_NAV_STUDIO)return;
    if(bb_nav.bank==BB_NAV_HOME_SONGS) {
        unsigned first=bb_cd.vpage*16u,count=bb_cd_count(),v;   /* RC2: in the view's numbering */
        f->list_count=(uint8_t)(count>first?(count-first>16u?16u:count-first):0u);
        f->list_cursor=f->list_playing=f->list_queued=BB_NAV_NONE_IDX;
        f->list_cursor=(uint8_t)(bb_cd.cursor-first);
        /* The list indicates the band grid even when an individual part diverges. */
        if(bb_cd.pticket&&(v=bb_cd_choice_row(&bb_cd.group_choice)-first)<16u)f->list_playing=(uint8_t)v;
        if((v=bb_cd_list_queued()-first)<16u&&v!=f->list_playing)
            {f->queued|=BB_NAV_Q_PATTERN;f->list_queued=(uint8_t)v;}
        return;
    }
    f->section_count=0;f->section=f->section_queued=BB_NAV_NONE_IDX;
    f->queued&=(uint8_t)~BB_NAV_Q_SECTION;
    if(bb_nav_p1(BB_P1_SONG_ARMED,0,0))return;
    const bb_factory_choice *group=bb_cd_group_ref();
    if(group) {
        bb_catalogue_id parts=bb_catalogue_find(&bb_factory_catalogue,group->bank,"parts");
        f->section_count=(uint8_t)bb_catalogue_count(&bb_factory_catalogue,parts);
    }
    if(group&&group->bank==bb_cd.group_choice.bank){
        if(bb_cd_group_linked())f->section=(uint8_t)bb_cd.group_choice.part_index;
        if(bb_cd_combo_players())f->sec_lit=bb_cd.pset;
    }
    if(bb_cd.ticket&&bb_cd_cue_on_grid(group)){
        f->section_queued=bb_cd.key;f->sec_queued=bb_cd.qset;f->queued|=BB_NAV_Q_SECTION;
    }
}
