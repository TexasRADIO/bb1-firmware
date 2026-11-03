/* SPDX-License-Identifier: GPL-3.0-only
 * 2.1 Live Play P3b: the Rhythm Conductor (SPEC section 2; home-keymap.png). Main loop only.
 * HOME has two banks (bb_nav): SONGS (the list; power-on lands here) and CONDUCT (the band).
 *  - SONGS: white 1-16 / ALGO move the cursor, OCT the list page, black 5-7 the collection (SONGS / ERAS / ACID),
 *    choosing a song loads its initial section on all three parts in ONE section commit (stopped: at once; playing:
 *    next bar), then shows CONDUCT. Independent substitutions happen later on each instrument's PATTERN page.
 *  - CONDUCT: black 1-11 (OCT pages, up to 55) = section n of the FOCUSED part's song. Every part whose reference is
 *    that song moves to section n; every part on another song HOLDs (P3a keep mask, Joe's choice). All or nothing on
 *    one owner tick; a refusal names the part. Newest key wins (cancel, then resubmit when the handoff is free); the
 *    pulsing key again cancels; the bright (playing) key cancels a waiting cue; STOP cancels (the owner does it).
 *    A LATE cue is retargeted to the following bar once, then MISSED THE BAR. A cancel the full command queue refuses
 *    is kept and retried every scan until the bar (RC1, Duke #64 6592823816); if the cue plays anyway the screen says
 *    TOO LATE TO CANCEL, never that it was cancelled. While a SONG arrangement is armed the
 *    section keys are off (facts: no sections). REC+black stays the focused part only (bb_nav_section_launch).
 * One section path: the existing owner (bb_audio_section_submit) with the existing readers
 * (bb_factory_prepare_rich_drum / _synth_data). Labels: each moving part's existing bb_player gets request =
 * {choice, section ticket}; apply() stamps the same ticket, so bb_player_sync shows QUEUED -> APPLIED, LEDs pulse/
 * light and the HOME lanes read NEXT with no second bookkeeping. Mixes, mutes, kits, sounds and effects are never
 * touched (the section path copies none). New state is in the pool. */
#include "bb/bb_chord.h"
static struct {
    bb_section_bundle stage;                 /* 1,432 B: copied by the owner during submit                        */
    bb_browser list;                         /* the SONGS / ERAS / ACID list                                      */
    bb_player_selection prev[3];             /* each moved part's request before the cue (restored if it ends)    */
    uint32_t ticket;                         /* the cue in flight, 0 = none                                       */
    uint8_t welcome;                         /* one-time start card; dismissed into the normal song list */
    uint8_t take, coll;                      /* take is fixed to all three; collection 0 SONGS 1 ERAS 2 PRESETS     */
    uint8_t moved;                           /* parts carrying the ticket                                         */
    uint8_t key, want;                       /* the cue's section; a newer section waiting for the handoff (+1)    */
    uint8_t retried, chose;                  /* LATE: retargeted once; the cue is a CHOOSE (no retarget)         */
    uint8_t cancel;                          /* a cancel asked for the cue in flight: 1 sent, 2 owed (queue full)  */
    uint8_t vpage, nshelf, shelf_ready;      /* RC2 views: the 16-key page of the view; preset shelves found     */
    uint8_t shelf[16];                       /* RC2: SONGS-group banks that are Roland preset shelves (bit per bank) */
    uint64_t qset, pset, wset;               /* RC3 combining: the cue's sections, the applied combination, a newer one */
    uint32_t pticket;                        /* the ticket that applied pset (still playing while a player carries it) */
    bb_chord chord;                          /* RC3: the PERFORM chord gesture (bb/bb_chord.h)                      */
} bb_cd __attribute__((section(".pool")));
static bb_player *bb_cd_player(unsigned q){return q<2u?&bb_drum_library[q]:&bb_library[0];}
static unsigned bb_cd_focus(void){return bb_nav_row<2u?bb_nav_row:2u;}
/* What a part plays, or what it is about to play (its own pending request). NULL: nothing. */
static const bb_factory_choice *bb_cd_ref(unsigned q) {
    const bb_player *p=bb_cd_player(q);
    if(!p->ready)return 0;
    if(bb_player_pending(p))return &p->request.choice;
    return p->playing.ticket?&p->playing.choice:0;
}
/* A browser at (group, bank, part); part<0 = the bank's initial section. Cursor only. */
static int bb_cd_at(bb_browser *b,unsigned group,unsigned bank,int part,bb_factory_choice *out) {
    if(!bb_browser_group(b,group))return 0;
    int d=(int)bank-(int)b->bank_index;
    if(d&&!bb_browser_move_bank(b,d))return 0;
    if(part>=0){b->part_page=(uint16_t)((unsigned)part/BB_BROWSER_PART_KEYS);b->part_index=(uint16_t)part;}
    return bb_browser_current(b,out);
}
static char bb_cd_text[32] __attribute__((section(".pool")));   /* the refusal notice: pool, not RAM */
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
/* One commit for the parts in `move` (bit per part), each to its choice in ch[]; the others HOLD. 1 = queued. */
static int bb_cd_submit(unsigned move,const bb_factory_choice *ch,uint64_t set) {
    bb_section_bundle *b=&bb_cd.stage;
    bb_section_status st;
    if(!move||!bb_audio_section_status_read(&bb_sound,&st))return 0;
    memset(b,0,sizeof *b);b->dillo.length=16;
    for(unsigned q=0;q<3u;q++) {
        if(!((move>>q)&1u))continue;
        const char *why=0;
        if(q<2u) {
            bb_factory_choice cs[4];unsigned n=0;            /* RC3: a combination merges its rhythms (Studio's rule) */
            for(unsigned s=0;s<64u&&set&(set-1u);s++)if((set>>s)&1u) {
                bb_browser v=bb_cd_player(q)->view;
                if(n==4u||!bb_cd_at(&v,ch[q].group,ch[q].bank_index,(int)s,&cs[n++])){why="TOO MANY TO COMBINE";break;}
            }
            if(!why&&(n?bb_factory_prepare_rich_combo(&bb_factory_catalogue,cs,n,&b->drum[q]):
                       bb_factory_prepare_rich_drum(&bb_factory_catalogue,&ch[q],&b->drum[q]))!=BB_FDRUM_OK)
                why=n?"CAN'T COMBINE":"NO DRUMS HERE";
            else if(bb_sound.drum_patterns.family[q])why="NEEDS A RICH KIT";
            else if(b->drum[q].voices&~bb_audio_phrase_source_voices_owned(&bb_sound,q))why="NEEDS VOICES";
            else if(!bb_audio_phrase_preflight_playable(&b->drum[q],BB_RICH_REPRESENTABLE_VOICES))why="NEEDS TIMING";
        } else if(bb_factory_prepare_synth_data(&bb_factory_catalogue,&ch[2],0,&b->dillo)!=BB_FACTORY_PATTERN_OK)why="NO LINE HERE";
        if(why){bb_cd_refused(q,why);return 0;}                  /* all or nothing: the owner is not touched */
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
    for(unsigned q=0;q<3u;q++)if((move>>q)&1u) {
        bb_player *p=bb_cd_player(q);
        bb_cd.prev[q]=p->request;
        memset(&p->request,0,sizeof p->request);p->request.choice=ch[q];p->request.ticket=t;p->request_seen=0;
#if BB_WITH_RICH_DRUM_BROWSER
        if(q<2u)p->request_rich=1;
#endif
        (void)bb_cd_at(&p->view,ch[q].group,ch[q].bank_index,ch[q].part_index,&p->request.choice);
    }
    bb_dirty=1;return 1;
}
/* CONDUCT: section n of the focused part's song for every part on that song (RC3: with the other sections of `set`,
 * a combination, merged on the drum parts; n is its first). */
static void bb_cd_go_set(unsigned n,uint64_t set) {
    const bb_factory_choice *f=bb_cd_ref(bb_cd_focus());
    bb_factory_choice ch[3];unsigned move=0;
    if(!f){bb_say(BB_TX(TX_CHOOSE_PATTERN));return;}
    bb_cd.key=(uint8_t)n;
    for(unsigned q=0;q<3u;q++) {
        const bb_factory_choice *r=bb_cd_ref(q);
        bb_browser b=bb_cd_player(q)->view;
        if(r&&r->group==f->group&&r->bank_index==f->bank_index&&bb_cd_at(&b,f->group,f->bank_index,(int)n,&ch[q]))move|=1u<<q;
    }
    (void)bb_cd_submit(move,ch,set);
}
static void bb_cd_go(unsigned n){bb_cd_go_set(n,0);}
static void bb_cd_key(unsigned n) {
    if(bb_nav_p1(BB_P1_SONG_ARMED,0,0)){bb_say("SONG PLAYING");return;}
    const bb_player *p=bb_cd_player(bb_cd_focus());
    bb_cd.retried=0;bb_cd.chose=0;                      /* a press is a new gesture: one retarget again */
    if(bb_cd.ticket) {                                   /* a cue waits: same key or the playing key cancels, else replace */
        bb_cd.want=(uint8_t)((n==bb_cd.key&&!bb_cd.qset)||(p->playing.ticket&&p->playing.choice.part_index==n)?0u:n+1u);
        bb_cd.cancel=bb_audio_section_cancel_submit(&bb_sound,bb_cd.ticket)?1u:2u;   /* refused: owed, bb_cd_poll retries */
        return;
    }
    if(p->playing.ticket&&p->playing.choice.part_index==n&&!bb_player_pending(p))return;   /* already playing */
    bb_cd_go(n);
}
/* RC3: the combination now: the cue's, else the applied one while the focused part still plays it, else its section */
static uint64_t bb_cd_cur(void) {
    const bb_player *p=bb_cd_player(bb_cd_focus());
    if(bb_cd.ticket)return bb_cd.qset?bb_cd.qset:(uint64_t)1<<(bb_cd.key&63u);
    if(!p->playing.ticket)return 0;
    return bb_cd.pset&&p->playing.ticket==bb_cd.pticket?bb_cd.pset:(uint64_t)1<<(p->playing.choice.part_index&63u);
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
    bb_cd.retried=0;bb_cd.chose=0;
    if(bb_cd.ticket) {
        bb_cd.wset=m==bb_cd.qset?0u:m;bb_cd.want=bb_cd.wset?0xFFu:0u;
        bb_cd.cancel=bb_audio_section_cancel_submit(&bb_sound,bb_cd.ticket)?1u:2u;
        return;
    }
    if(m==bb_cd_cur())return;                           /* already playing */
    bb_cd_go_set(n,m);
}
/* RC2 (Joe 10:28): HOME's collections are views. SONGS = the named songs and PRESETS = the Roland preset shelves (both
 * in the catalogue's SONGS group; a shelf is titled "Roland <machine>", the firmware catalogue's only such titles), ERAS as it is. No catalogue data moves: a view
 * index (cursor, white keys, "n of N") maps to the group's bank index. */
static int bb_cd_is_shelf(unsigned bank){return bank<128u&&(bb_cd.shelf[bank>>3]>>(bank&7u)&1u);}
static void bb_cd_shelves(void) {
    bb_browser b=bb_cd.list;const uint8_t *t;size_t n;unsigned i,c;
    if(!bb_browser_group(&b,BB_BROWSER_SONGS))return;
    c=(unsigned)bb_browser_bank_count(&b);
    memset(bb_cd.shelf,0,sizeof bb_cd.shelf);bb_cd.nshelf=0;
    for(i=0;i<c&&i<128u;i++) {
        b.bank_page=(uint16_t)(i/BB_BROWSER_BANK_KEYS);
        if(bb_catalogue_text(&bb_factory_catalogue,bb_catalogue_find(&bb_factory_catalogue,
           bb_browser_bank_cell(&b,i%BB_BROWSER_BANK_KEYS),"song"),&t,&n)&&n>7u&&!memcmp(t,"Roland ",7))
            {bb_cd.shelf[i>>3]|=(uint8_t)(1u<<(i&7u));bb_cd.nshelf++;}
    }
    bb_cd.shelf_ready=1;
}
static int bb_cd_in_view(unsigned coll,unsigned group,unsigned bank) {   /* the bank belongs to collection coll */
    return coll==1u?group==BB_BROWSER_ERAS:group==BB_BROWSER_SONGS&&bb_cd_is_shelf(bank)==(coll==2u);
}
static unsigned bb_cd_count_c(unsigned coll) {
    bb_browser b=bb_cd.list;unsigned c;
    (void)bb_browser_group(&b,coll==1u?BB_BROWSER_ERAS:BB_BROWSER_SONGS);c=(unsigned)bb_browser_bank_count(&b);
    return coll==1u?c:coll==2u?bb_cd.nshelf:c-bb_cd.nshelf;
}
static unsigned bb_cd_count(void){return bb_cd_count_c(bb_cd.coll);}
static unsigned bb_cd_view_c(unsigned coll,unsigned group,unsigned bank) {   /* bank index -> its view index */
    unsigned v=0,i;
    for(i=0;i<bank;i++)v+=(unsigned)bb_cd_in_view(coll,group,i);
    return v;
}
static unsigned bb_cd_bank(unsigned v) {                  /* view index -> bank index (count: none) */
    unsigned c=(unsigned)bb_browser_bank_count(&bb_cd.list),i;
    for(i=0;i<c;i++)if(bb_cd_in_view(bb_cd.coll,bb_cd.list.group,i)&&!v--)return i;
    return c;
}
static unsigned bb_cd_view(unsigned bank){return bb_cd_view_c(bb_cd.coll,bb_cd.list.group,bank);}
static void bb_cd_goto(unsigned v) {                      /* the cursor to view index v (clamped), its page in view */
    unsigned c=bb_cd_count();
    if(!c)return;
    if(v>=c)v=c-1u;
    (void)bb_browser_move_bank(&bb_cd.list,(int)bb_cd_bank(v)-(int)bb_cd.list.bank_index);
    bb_cd.vpage=(uint8_t)(v/16u);bb_dirty=1;
}
static void bb_cd_move(int d){int v=(int)bb_cd_view(bb_cd.list.bank_index)+d;bb_cd_goto(v<0?0u:(unsigned)v);}
static void bb_cd_page(int d){int p=(int)bb_cd.vpage+d;unsigned c=bb_cd_count();
    if(p>=0&&(unsigned)p*16u<c)bb_cd_goto((unsigned)p*16u);}
static int bb_cd_open_list(void){if(!bb_cd.welcome)return 0;bb_cd.welcome=0;bb_dirty=1;return 1;}
static void bb_cd_pick(unsigned key){if(bb_cd_open_list())return;unsigned v=bb_cd.vpage*16u+key;if(v<bb_cd_count())bb_cd_goto(v);}
static void bb_cd_collection(unsigned c) {                /* B5 SONGS, B6 ERAS, B7 PRESETS */
    if(!bb_browser_group(&bb_cd.list,c==1u?BB_BROWSER_ERAS:BB_BROWSER_SONGS))return;
    bb_cd.welcome=0;bb_cd.coll=(uint8_t)c;bb_cd_goto(0);
}
/* SONGS: EDIT = CHOOSE. The song's initial section on all three parts. RC2 (Joe 10:26): PLAY on the list is
 * CHOOSE + start in one press (start=1): the start is one more command slot, checked with the others, so a refusal
 * changes nothing and does not start. Either way the screen moves to PERFORM. */
static void bb_cd_choose_start(int start) {
    bb_factory_choice ch[3];unsigned move=0,take=7u;
    if(bb_cd.ticket){bb_say(BB_TX(TX_SOUND_REQUEST_STILL_PENDING));return;}
    for(unsigned q=0;q<3u;q++)if((take>>q)&1u) {
        bb_browser b=bb_cd_player(q)->view;
        if(bb_cd_at(&b,bb_cd.list.group,bb_cd.list.bank_index,-1,&ch[q]))move|=1u<<q;
    }
    bb_cd.key=0;bb_cd.retried=0;bb_cd.chose=1;
    /* All or nothing (RC1, Duke #64 6592823816 item 2): a song brings its sheet tempo and swing, once (as 1.5), so
     * CHOOSE needs 3 command slots (section, TEMPO, SWING_X2), else 1. The main loop is the only producer that takes
     * a slot (the audio owner only frees them), so free room checked here is still there after the section commit:
     * too little room sends nothing and says BUSY - RETRY. */
    unsigned bpm=0,swing=0,tempo=move&&bb_cd.list.group==BB_BROWSER_SONGS&&
        bb_factory_read_song_tempo(&bb_factory_catalogue,&ch[__builtin_ctz(move)],&bpm,&swing);
    if(move&&BB_COMMAND_CAPACITY-bb_sound.commands.count<(tempo?3u:1u)+(start?1u:0u)){bb_say(BB_TX(TX_BUSY_RETRY));return;}
    if(!bb_cd_submit(move,ch,0))return;
    if(tempo){bb_send(BB_CMD_TEMPO,bpm);bb_send(BB_CMD_SWING_X2,swing);}
    if(start)bb_send(BB_CMD_TRANSPORT,1u);               /* stopped: the section applied at once; PLAY starts on it */
    bb_cd.welcome=0;bb_nav.bank=BB_NAV_HOME_CONDUCT;bb_nav_project();
}
static void bb_cd_choose(void){if(!bb_cd_open_list())bb_cd_choose_start(0);}
/* RC2 (Joe 10:26): PLAY on HOME/SONGS while stopped. 1 = handled (playing: PLAY stays start/stop). */
static int bb_cd_play(void) {
    bb_section_status st;
    if(bb_nav.page!=BB_NAV_STUDIO||bb_nav.bank!=BB_NAV_HOME_SONGS||!bb_audio_section_status_read(&bb_sound,&st)||st.running)return 0;
    bb_cd_choose_start(1);return 1;
}
/* Every scan: follow the cue to APPLIED, or put the labels back and say why it ended; resubmit a newer key. */
static void bb_cd_poll(void) {
    bb_section_status st;
    if(bb_nav.page!=BB_NAV_STUDIO||bb_nav.bank!=BB_NAV_HOME_SONGS)bb_cd.welcome=0;
    if(!bb_cd.list.ready&&bb_browser_init(&bb_cd.list,&bb_factory_catalogue)){
        (void)bb_browser_group(&bb_cd.list,BB_BROWSER_SONGS);bb_cd.take=7;
    }
    if(bb_cd.list.ready&&!bb_cd.shelf_ready)bb_cd_shelves();
    if(!bb_cd.ticket){if(bb_cd.want==0xFFu){bb_cd.want=0;bb_cd_go_set((unsigned)__builtin_ctzll(bb_cd.wset),bb_cd.wset);}
                      else if(bb_cd.want){unsigned n=bb_cd.want-1u;bb_cd.want=0;bb_cd_go(n);}return;}
    if(!bb_audio_section_status_read(&bb_sound,&st))return;
    if(st.phase!=BB_SECTION_FREE) {                     /* still waiting for the bar; an owed cancel goes when it can */
        if(bb_cd.cancel==2u&&bb_audio_section_cancel_submit(&bb_sound,bb_cd.ticket))bb_cd.cancel=1;
        return;
    }
    uint32_t t=bb_cd.ticket;unsigned asked=bb_cd.cancel;bb_cd.ticket=0;bb_cd.cancel=0;
    if(st.state.applied!=t)bb_cd.qset=0;
    if(st.state.applied==t) {                           /* APPLIED: every moved part's player shows it (bb_player_sync) */
        bb_cd.pset=bb_cd.qset;bb_cd.pticket=t;bb_cd.qset=0;
        if(asked&&!bb_cd.want)bb_say("TOO LATE TO CANCEL");   /* the cancel did not land before the bar: say so */
        return;
    }
    for(unsigned q=0;q<3u;q++) {                         /* ended without applying: the labels go back */
        bb_player *p=bb_cd_player(q);
        if(((bb_cd.moved>>q)&1u)&&p->request.ticket==t)p->request=bb_cd.prev[q];
    }
    bb_dirty=1;
    if(bb_cd.want)return;                                /* replaced by a newer key: it goes next scan */
    if(st.error==BB_SECTION_ERROR_LATE&&!bb_cd.retried&&!bb_cd.chose)
        {bb_cd.retried=1;bb_cd.want=(uint8_t)(bb_cd.key+1u);return;}   /* the following bar, once */
    bb_say(st.error==BB_SECTION_ERROR_LATE?"MISSED THE BAR":!st.running?bb_notice_text(BB_NOTICE_CANCELLED_BY_STOP):"CUE CANCELLED");
}
/* Facts for the HOME lights (bb_nav's one table): take / collection; on SONGS the list page; on CONDUCT the focused
 * part's song is the one in view, and a SONG arrangement in charge turns the section keys off. */
static void bb_cd_facts(bb_nav_facts *f) {
    f->home_take=7u;f->home_coll=bb_cd.coll;
    if(bb_nav.page!=BB_NAV_STUDIO)return;
    bb_player *p=bb_cd_player(bb_cd_focus());
    if(bb_nav.bank==BB_NAV_HOME_SONGS) {
        unsigned first=bb_cd.vpage*16u,count=bb_cd_count(),v;   /* RC2: in the view's numbering */
        const bb_factory_choice *r=p->playing.ticket?&p->playing.choice:0;
        f->list_count=(uint8_t)(count>first?(count-first>16u?16u:count-first):0u);
        f->list_cursor=f->list_playing=f->list_queued=BB_NAV_NONE_IDX;
        if((v=bb_cd_view(bb_cd.list.bank_index)-first)<16u)f->list_cursor=(uint8_t)v;
        if(r&&bb_cd_in_view(bb_cd.coll,r->group,r->bank_index)&&(v=bb_cd_view(r->bank_index)-first)<16u)f->list_playing=(uint8_t)v;
        if(bb_player_pending(p)&&bb_cd_in_view(bb_cd.coll,p->request.choice.group,p->request.choice.bank_index)&&
           (v=bb_cd_view(p->request.choice.bank_index)-first)<16u&&(!r||r->bank_index!=p->request.choice.bank_index))
            {f->queued|=BB_NAV_Q_PATTERN;f->list_queued=(uint8_t)v;}
        return;
    }
    if(bb_nav_p1(BB_P1_SONG_ARMED,0,0)){f->section_count=0;f->section=BB_NAV_NONE_IDX;f->queued&=(uint8_t)~BB_NAV_Q_SECTION;return;}
    if(bb_cd.ticket&&bb_cd.qset){f->sec_queued=bb_cd.qset;f->queued|=BB_NAV_Q_SECTION;}   /* RC3: every key of a combination */
    if(bb_cd.pset&&p->playing.ticket==bb_cd.pticket)f->sec_lit=bb_cd.pset;
}
