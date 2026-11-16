                                        
                                                                                                    
                                                                                                  
                                                                                
  
                                                                                                   
                                                                                               
                                                                                                  
                                                                                        
                                                                                     
#ifndef BB_UI_V3
#define BB_UI_V3 1
#endif
#if BB_UI_V3
#define BB_UI_FIRMWARE 1
#define BB_UI_HELP 1                         /* HELP (hold HOME) in the shared frame */
#define BB_UI_ALL_PAGES 1                    /* every player-facing page: STEPS, FX, KNOBS, attract included */
#define BB_UI_HOME 1                         /* STUDIO = the approved two-instrument HOME */
#ifndef BB_UI_BACKDROP
#define BB_UI_BACKDROP 1                     /* the large Armadillo behind every page: 1 layout C, 2 layout A, 0 the mini */
#endif
#define bb_ui_init bb_v3_init                /* (bb_ui_init is the firmware's own UI reset) */
#define bb_ui_frame bb_v3_frame
#define bb_ui_invalidate bb_v3_invalidate
#define bb_ui_page_for bb_v3_page_for
#include "bb_ui_v3/bb_ui.c"
#include "bb_ui_v3_route.h"
#undef bb_ui_init
#undef bb_ui_frame
#undef bb_ui_invalidate
#undef bb_ui_page_for

static bb_ui bb_v3;
static bb_ui_in bb_v3_in __attribute__((section(".pool")));   /* not on the stack; 2.1 P1: in the pool, not RAM */
static uint8_t bb_v3_shown;                  /* a v3 page is on the screen */
static uint32_t bb_v3_next, bb_v3_beat, bb_v3_step_seen, bb_v3_last_ms;
static uint8_t bb_v3_kpos[4], bb_v3_gout[4], bb_v3_kpage = BB_UI_NO;
static const char *bb_v3_klab[4];
static uint32_t bb_v3_ctx, bb_v3_ctx_t;     /* what the page shows (name, choice) and when it last changed */
#define BB_V3_SETTLE_MS 400u                /* values that follow such a change are not turns */
static char bb_v3_ktext[4][16], bb_v3_pname[24], bb_v3_title[48], bb_v3_sub[48];
static char bb_v3_items[5][40], bb_v3_secs[BB_UI_SECTIONS][24], bb_v3_now[48], bb_v3_sec[24];   /* RAM: 2.0's addresses */
/* RC2: the chooser's rows 6-10 and footer, in the pool (RAM keeps every 2.0 address: timing identity) */
static struct {                             /* one object: no new merged global (the RAM groups keep their names) */
    char items2[BB_UI_LIST - 5][40], foot[64];
    char secs2[16 - BB_UI_SECTIONS][24];    /* PERFORM's section names 12-16 (1-11: bb_v3_secs, RAM) */
    uint16_t first[2];                      /* the list window: KIT, PATTERN */
} bb_v3_rc2 __attribute__((section(".pool")));
#define bb_v3_items2 bb_v3_rc2.items2
#define bb_v3_foot bb_v3_rc2.foot
#define bb_v3_first bb_v3_rc2.first
static inline __attribute__((always_inline)) char *bb_v3_sec_name(unsigned k)
{ return k < BB_UI_SECTIONS ? bb_v3_secs[k] : bb_v3_rc2.secs2[k - BB_UI_SECTIONS]; }
static inline __attribute__((always_inline)) char *bb_v3_item(unsigned i) { return i < 5u ? bb_v3_items[i] : bb_v3_items2[i - 5u]; }
/* the list window that keeps the cursor in view (scrolls a row at a time) */
static unsigned bb_v3_window(unsigned which, unsigned cur, unsigned count)
{
    unsigned f = bb_v3_first[which];
    if (cur < f) f = cur;
    else if (cur >= f + BB_UI_LIST) f = cur + 1u - BB_UI_LIST;
    if (count > BB_UI_LIST && f > count - BB_UI_LIST) f = count - BB_UI_LIST;
    if (count <= BB_UI_LIST) f = 0;
    bb_v3_first[which] = (uint16_t)f;
    return f;
}
/* "3/71 \xB7 " + tail. The page header already names the list; use the scarce
 * footer width to say which physical control moves it and which one commits. */
static void bb_v3_footer(unsigned n, unsigned count, const char *tail)
{
    char *o = bb_v3_foot; unsigned k = 0, d;
    for (d = 100u; d; d /= 10u) if (n / d || d == 1u) o[k++] = (char)('0' + n / d % 10u);
    o[k++] = '/';
    for (d = 100u; d; d /= 10u) if (count / d || d == 1u) o[k++] = (char)('0' + count / d % 10u);
    if (tail) { o[k++] = ' '; o[k++] = '\xB7'; o[k++] = ' '; while (*tail && k + 1u < sizeof bb_v3_foot) o[k++] = *tail++; }
    o[k] = 0;
}
static char bb_v3_nextname[24];
/* 2.0 D4: the key legend (bb_nav_legend; 61 characters at most) and what shows it, in the pool, not ordinary RAM */
static char bb_v3_legend[64] __attribute__((section(".pool")));
static uint32_t bb_v3_leg_seen __attribute__((section(".pool")));   /* page + legend last shown */
static uint32_t bb_v3_keys_prev __attribute__((section(".pool")));  /* logical keys held at the last frame */
/* BB_EV_LEGEND: the page (or its bank: the legend changes) was entered, or a key was pressed that the page gives no
 * job (bb_nav_key_role NONE, with REC as held). HOME held is HELP (bb_nav), so it is not a legend trigger. */
static uint32_t bb_v3_legend_event(uint8_t page)
{
    uint32_t h = h_s(h_u(H0, page), bb_v3_legend), keys = bb_nav_keys_logical(fm1_in.notes), down = keys & ~bb_v3_keys_prev, ev = 0;
    int rec = !!(fm1_in.buttons & (1u << panel.btn[B_REC]));
    uint8_t k, a;
    if (h != bb_v3_leg_seen) { bb_v3_leg_seen = h; ev = BB_EV_LEGEND; }
    for (k = 0; k < BB_NAV_KEYS; k++)
        if ((down >> k & 1u) && bb_nav_key_role(&bb_nav, k, rec, &a) == BB_NAV_K_NONE)
            ev = BB_EV_LEGEND;
    bb_v3_keys_prev = keys;
    return ev;
}

static void bb_v3_copy(char *d, unsigned n, const char *s)
{
    unsigned k = 0;
    if (s)
        while (s[k] && k + 1u < n) { d[k] = s[k]; k++; }
    d[k] = 0;
}
/* catalogue text into a buffer (printable ASCII; the brand faces fold case themselves) */
static void bb_v3_cat(bb_catalogue_id id, char *out, unsigned n)
{
    const uint8_t *b;
    size_t c, i;
    if (id == BB_CATALOGUE_NONE || !bb_catalogue_text(&bb_factory_catalogue, id, &b, &c)) {
        out[0] = 0;
        return;
    }
    if (c > n - 1u)
        c = n - 1u;
    for (i = 0; i < c; i++)
        out[i] = b[i] >= 32 && b[i] <= 126 ? (char)b[i] : '?';
    out[c] = 0;
}
/* The TR-77 source uses the same name for two different Bass and
 * Bass & Snare patterns. Qualify those labels only in Conductor. */
static void bb_v3_conductor_section(bb_catalogue_id part, char *out, unsigned n)
{
    bb_v3_cat(bb_catalogue_find(&bb_factory_catalogue, part, "name"), out, n);
    if (out[0] != 'B') return;
    const uint8_t *id;
    size_t len;
    static const char prefix[] = "ROLAND-TR77-0";
    if (!bb_catalogue_text(&bb_factory_catalogue, bb_catalogue_find(&bb_factory_catalogue, part, "id"),
                           &id, &len) || len != sizeof prefix + 1u ||
        memcmp(id, prefix, sizeof prefix - 1u)) return;
    switch ((id[sizeof prefix - 1u] << 8) | id[sizeof prefix]) {
    case ('1' << 8) | '9': bb_v3_copy(out, n, "Bass A"); break;
    case ('2' << 8) | '5': bb_v3_copy(out, n, "Bass B"); break;
    case ('2' << 8) | '0': bb_v3_copy(out, n, "Bass & Snare A"); break;
    case ('2' << 8) | '6': bb_v3_copy(out, n, "Bass & Snare B"); break;
    }
}
static void bb_v3_bank_title(const bb_factory_choice *ch, char *out, unsigned n)
{
    bb_v3_cat(bb_catalogue_find(&bb_factory_catalogue, ch->bank, ch->group ? "song" : "name"), out, n);
}

                                                                                       
                                                                                          
static uint8_t bb_v3_page(void)
{
    return bb_v3_route(&bb_nav, ui.uboot || bb_diag_page);
}

/* 2.1 P1: HELP rows from bb_nav_help (navigation's role tables): one page of them, as text the v3 module draws. A
 * key run reads "1-11" / "B1-4"; K1-K4 carry their labels; the control pressed while HOME is held is picked. REC held
 * alone shows the REC rows and turns its pages by itself (REC help is not modal: OCT would be a chord). */
/* In .bss, padded to the 464 bytes bb_v3_in had there in 2.0 (bb_v3_in moved to the pool): RAM data keeps every 2.0
 * address, including CPU1's stack and trace (timing identity), at no RAM cost against 2.0. */
static union {
    struct {
        bb_nav_help_row nav[BB_UI_HELP_ROWS];
        bb_ui_help_row row[BB_UI_HELP_ROWS];
        char keys[BB_UI_HELP_ROWS][8];
        char knobs[40], note[48];
    } h;
    uint32_t slot[116];
} bb_v3_help_slot __attribute__((section(".pool")));
#define bb_v3_help bb_v3_help_slot.h
_Static_assert(sizeof(bb_v3_help_slot) == 464, "the 2.0 bb_v3_in slot");
static const uint8_t bb_v3_kc_button[BB_NAV_BUTTONS] = {
    KC_FX, KC_SCL, KC_ENV, KC_LFO, KC_EDIT, KC_GLO, KC_HOME, KC_SAVE, KC_ARP, KC_SEQ, KC_PLAY, KC_REC, KC_OCTDN, KC_OCTUP
};
static const uint8_t bb_v3_kc_enc[3] = {KC_SELECT, KC_ALGO, KC_PRESETS};
static void bb_v3_keyname(char *o, unsigned a, unsigned b)   /* "1-11", "B1-4", "B6" */
{
    unsigned n = 0, base = a < 16u ? 0u : 16u;
    if (base) o[n++] = 'B';
    n += nav_leg_num(o + n, a - base + 1u);
    if (b > a) { o[n++] = '-'; n += nav_leg_num(o + n, b - base + 1u); }
    o[n] = 0;
}
static void bb_v3_cat2(char *o, unsigned cap, const char *a, const char *b)
{
    unsigned n = 0;
    while (a && *a && n + 1u < cap) o[n++] = *a++;
    while (b && *b && n + 1u < cap) o[n++] = *b++;
    o[n] = 0;
}
static void bb_v3_help_fill(bb_ui_in *in)
{
    const int rec = !bb_nav.help && bb_nav.rec_help;
    unsigned total, page = bb_nav.help_page, i, k;
    int found = -1;
    total = bb_nav_help(&bb_nav, &bb_nav_f, rec, 0, 0, 0, bb_nav.explain, &found);
    in->help_pages = (uint8_t)((total + BB_UI_HELP_ROWS - 1u) / BB_UI_HELP_ROWS);
    if (rec && in->help_pages > 1u)                              /* REC help: a page every 2.5 s */
        page = (fm1_ms - bb_nav.rec_since) / 2500u % in->help_pages;
    if (page >= in->help_pages) page = 0;
    in->help_page = (uint8_t)page;
    (void)bb_nav_help(&bb_nav, &bb_nav_f, rec, bb_v3_help.nav, page * BB_UI_HELP_ROWS, BB_UI_HELP_ROWS, 0, 0);
    k = total - page * BB_UI_HELP_ROWS;                          /* rows on this page (the call returns the total) */
    in->help_rows = (uint8_t)(k > BB_UI_HELP_ROWS ? BB_UI_HELP_ROWS : k);
    {   /* K1-K4's labels, once */
        uint8_t roles[4]; const char *lab[4]; unsigned n = 0;
        bb_nav_knobs(&bb_nav, roles, lab);
        for (k = 0; k < 4u; k++) if (lab[k][0]) {
            if (n && n + 2u < sizeof bb_v3_help.knobs) bb_v3_help.knobs[n++] = ' ';
            for (const char *c = lab[k]; *c && n + 1u < sizeof bb_v3_help.knobs; c++) bb_v3_help.knobs[n++] = *c;
        }
        bb_v3_help.knobs[n] = 0;
    }
    for (i = 0; i < in->help_rows; i++) {
        const bb_nav_help_row *r = &bb_v3_help.nav[i];
        bb_ui_help_row *u = &bb_v3_help.row[i];
        u->kc = u->kc2 = BB_UI_NO; u->rec = r->rec; u->keys = 0;
        u->text = bb_nav_help_text(r->text);
        const char *welcome=bb_cd.welcome?bb_nav_welcome_help(r->kind,r->a,rec):0;
        if(welcome)u->text=welcome;
        switch (r->kind) {
        case BB_NAV_HR_KEYS: bb_v3_keyname(bb_v3_help.keys[i], r->a, r->b); u->keys = bb_v3_help.keys[i]; break;
#if BB_WITH_PHRASE_NUDGE
        case BB_NAV_HR_HELD_K1: u->kc = KC_HOLD; u->kc2 = KC_K1; break;
        case BB_NAV_HR_HELD_K2: u->kc = KC_HOLD; u->kc2 = KC_K2; break;
#endif
#if BB_WITH_STEP_CONDITIONS
        case BB_NAV_HR_HELD_K3: u->kc = KC_HOLD; u->kc2 = KC_K3; break;
#endif
        case BB_NAV_HR_HELD_K4: u->kc = KC_HOLD; u->kc2 = KC_K4; break;
#if BB_WITH_STEP_CONDITIONS
        case BB_NAV_HR_NOTE: break;
#endif
        case BB_NAV_HR_BUTTON:
            u->kc = r->a < BB_NAV_BUTTONS ? bb_v3_kc_button[r->a] : BB_UI_NO;
            if (r->a == BB_NAV_B_OCTDN) u->kc = KC_OCTPM;
            if (r->a == BB_NAV_B_REC) u->rec = 0;
            break;
        case BB_NAV_HR_KNOBS: u->kc = KC_K14; u->keys = bb_v3_help.knobs; break;
        default: u->kc = r->a < 3u ? bb_v3_kc_enc[r->a] : BB_UI_NO; break;
        }
    }
    in->help_row = bb_v3_help.row;
    in->help_sel = BB_UI_NO;
    in->help_note = 0;
    in->help_word = bb_nav_help_word(&bb_nav);                  /* RC1: SONGS / PATCH / BUILD / SAVE AS */
    if (bb_nav.explain) {                                         /* the control pressed while HOME is held */
        const uint8_t ex = bb_nav.explain;
        const char *what = "", *says = "";
        char name[8];
        name[0] = 0;
        if (found >= 0 && (unsigned)found / BB_UI_HELP_ROWS == page) in->help_sel = (uint8_t)((unsigned)found % BB_UI_HELP_ROWS);
        if (ex & BB_NAV_EX_ENCODER) {
            unsigned e = ex & 7u;
            static const char *const enc[BB_NAV_ENCODERS] = {"SELECT", "ALGO", "PRESETS", "K1", "K2", "K3", "K4"};
            uint8_t roles[4]; const char *lab[4];
            what = enc[e < BB_NAV_ENCODERS ? e : 0];
            if (e >= BB_NAV_E_K1) { bb_nav_knobs(&bb_nav, roles, lab); says = lab[e - BB_NAV_E_K1]; }
        } else if (ex & BB_NAV_EX_BUTTON) {
            static const char *const btn[BB_NAV_BUTTONS] = {"FX", "SCL", "ENV", "LFO", "EDIT", "GLO", "HOME", "SAVE", "ARP",
                                                            "SEQ", "PLAY", "REC", "OCT-", "OCT+"};
            uint8_t b = ex & 15u;
            what = b < BB_NAV_BUTTONS ? btn[b] : "";
            says = bb_nav_help_text(bb_nav_button_help(bb_nav_button_role(&bb_nav, &bb_nav_f, b, 0)));
        } else if (ex & BB_NAV_EX_KEY) {
            uint8_t key = ex & 31u, a, role = bb_nav_key_role(&bb_nav, key, 0, &a);
            bb_v3_keyname(name, key, key); what = name;
            if (bb_nav_key_led(&bb_nav, &bb_nav_f, key, 0) != BB_NAV_LED_DARK) says = bb_nav_help_text(bb_nav_key_help(role, a));
        }
        if(bb_cd.welcome) {
            uint8_t kind=ex&BB_NAV_EX_ENCODER?BB_NAV_HR_ENCODER:ex&BB_NAV_EX_BUTTON?BB_NAV_HR_BUTTON:BB_NAV_HR_KEYS;
            uint8_t control=ex&(ex&BB_NAV_EX_ENCODER?7u:ex&BB_NAV_EX_BUTTON?15u:31u);
            const char *welcome=bb_nav_welcome_help(kind,control,rec);
            if(welcome)says=welcome;
        }
        if (!says || !says[0]) says = "nothing here";
        bb_v3_cat2(bb_v3_help.note, sizeof bb_v3_help.note, what, ": ");
        k = (unsigned)str_len(bb_v3_help.note);
        bb_v3_cat2(bb_v3_help.note + k, sizeof bb_v3_help.note - k, says, 0);
        in->help_note = bb_v3_help.note;
    }
}
/* 2.1 P1: tabs on screen, from bb_nav (FX: its five tabs on white 1-5; EVOLVE: the three modes, LFO steps them) */
static const char *bb_v3_tabs[5] __attribute__((section(".pool")));
static void bb_v3_tabs_fill(bb_ui_in *in)
{
    unsigned i;
    if (bb_nav.page == BB_NAV_FX) {
        bb_nav_state t = bb_nav;
        for (i = 0; i < 5u; i++) { t.bank = (uint8_t)i; bb_v3_tabs[i] = bb_nav_bank_name(&t); }
        in->tab = bb_v3_tabs; in->tabs = 5; in->tab_cur = bb_nav.bank;
        in->tab_note = bb_nav.bank == 1u ? "ECHO ONLY \xB7 DRY DRIVE: SOUND" : 0;
    } else if (bb_nav.page == BB_NAV_EVOLVE) {
        for (i = 0; i < BB_NAV_MODES; i++) bb_v3_tabs[i] = bb_nav_mode_name((uint8_t)i);
        in->tab = bb_v3_tabs; in->tabs = BB_NAV_MODES; in->tab_cur = bb_nav_f.gen_mode;
        in->tab_note = "LFO OR B3";
    }
}

static __attribute__((noinline)) const char *bb_v3_silence(unsigned part,unsigned page)
{
    static char text[32] __attribute__((section(".pool")));
    static const char *const names[]={"KIT A","KIT B","DILLO","LEAD"};
    unsigned lead=part==BB_UI_INST_DILLO&&bb_nav_synth();
    unsigned bit=1u<<part, name=part+lead;
    const char *why;
    char key=0;
    if(!song.master_q12)why=" SILENT \xB7 MASTER UP";
    else if(bb_nav_f.instr_solo&&!(bb_nav_f.instr_solo&bit)){
        name=bb_nav_f.instr_solo&1u?0u:bb_nav_f.instr_solo&2u?1u:2u;
        why=" SOLO \xB7 COND REC+B";
        key=(char)('1'+name);
    }else if(bb_nav_f.instr_muted&bit){
        if(part<2u){
            if(!bb_face.requested.drums[part].enabled)why=" OFF \xB7 REC+ARP";
            else if(page==BB_UI_PLAY_DRUMS||bb_nav_top_mode==BB_TOP_BIG_BEAT){
                why=page==BB_UI_PLAY_DRUMS?" MUTED \xB7 B":" MUTED \xB7 HOME B";
                key=(char)('8'+part);
            }else{
                why=" MUTED \xB7 COND B";
                key=(char)('1'+part);
            }
        }else why=lead?" MUTED \xB7 CONDUCTOR":" MUTED \xB7 CONDUCTOR B3";
    }else{
        unsigned level=part<2u?bb_face.requested.drums[part].level:
                       bb_face.requested.band_level[lead?BB_MIX_TELEGRAM:BB_MIX_DILLO];
        if(level)return 0;
        why=" SILENT \xB7 PRESETS K1 UP";
    }
    unsigned n=pf_append(text,0,sizeof text,names[name]);
    n=pf_append(text,n,sizeof text-1u,why);
    if(key)text[n++]=key;
    text[n]=0;
    return text;
}

/* the part every page shares: status, knobs (labels and values from navigation v2.1), key LEDs */
static void bb_v3_common(bb_ui_in *in, uint8_t page)
{
    unsigned k;
    /* N1: name the opening control unless a bank name needs the room ("FX  ECHO", "SOUND  COLOUR"); bb_nav composes
     * it (the capture gate draws the same text) and the status bar fits it beside the instrument (bb_ui.c). */
    (void)bb_nav_status_name(&bb_nav, bb_v3_pname, sizeof bb_v3_pname);
    in->now_ms = fm1_ms;
    in->page = page;
    in->instr = bb_nav_row < 2u ? bb_nav_row : (uint8_t)BB_UI_INST_DILLO;
    in->instr_name = bb_nav_row == 0u ? "KIT A" : bb_nav_row == 1u ? "KIT B" :
                     bb_nav_synth() ? "LEAD" : "BASS";
    /* The top-level Big Beat surface is the drum machine itself.  Name its
     * two decks as drums here; KIT remains the PRESETS/browser terminology. */
    if (page == BB_UI_PLAY_DRUMS && bb_nav.landing && bb_nav_row < 2u)
        in->instr_name = bb_nav_row ? "DRUM B" : "DRUM A";
    /* HOME / BIG BEAT is an instrument, not a generic page. Keep its complete
     * name in the status strip and identify the focused A/B layer there; the
     * two machine plates below already carry the actual kit names. Repeating a
     * long kit name here used to truncate BIG BEAT at the moment of entry. */
    in->page_name = bb_v3_pname;
    in->running = bb_face.requested.running;
    in->ticks = bb_ticks;
    in->beat = (uint8_t)(bb_ticks ? (bb_ticks - 1u) / 4u % 4u : 0u);
    in->bpm_x10 = (uint16_t)(bb_face.requested.bpm * 10u);
    in->accent = bb_face.accent;
    in->queued = bb_nav_f20.b[offsetof(bb_nav_facts, queued)];   /* BB_NAV_Q_* = BB_UI_Q_* (the 2.0 facts copy) */
    {
        uint32_t pk = bb_visual_peak / 64u;
        in->level = (uint8_t)(pk > 255u ? 255u : pk);
    }
    in->fault = (uint8_t)!!bb_sound_pick_audio_failed();
    in->fault_text = "Audio fault: load blocked";
    in->help = (uint8_t)!!bb_help_visible;
    if (in->help) bb_v3_help_fill(in);
    bb_v3_tabs_fill(in);
    /* the firmware's current message (bb_say: BUSY, REFUSED, CANCELLED BY STOP, TIMED OUT ...) */
    in->notice = bb_hint_until && bb_hint && bb_hint[0] && (int32_t)(fm1_ms - bb_hint_until) < 0 ? bb_hint : 0;
    /* A running pattern is not proof of audible output. The reason remains
     * visible after the temporary action notice goes away. */
    if (page == BB_UI_PLAY_DRUMS || page == BB_UI_STEPS || page == BB_UI_PLAY_DILLO)
        in->silence = bb_v3_silence(in->instr, page);
    in->events = 0;
    {   /* any touch of a button, key or encoder wakes attract and counts as input */
        static uint32_t sig_seen;
        uint32_t sig = h_u(h_u(H0, fm1_in.buttons), fm1_in.notes), e;
        for (e = 0; e < FM1_NENC; e++) sig = h_u(sig, fm1_in.enc_last[e]);
        if (sig != sig_seen) { sig_seen = sig; in->events |= BB_EV_INPUT; }
    }
    if (in->running && bb_ticks && (bb_ticks - 1u) / 4u != bb_v3_beat) {
        bb_v3_beat = (bb_ticks - 1u) / 4u;
        in->events |= BB_EV_BEAT;
    }
    in->knob_turning = BB_UI_NO;
    for (k = 0; k < 4u; k++) {
        const char *label = bb_nav_knob_label(k);
        unsigned pos = bb_nav_knob_text(k, bb_v3_ktext[k]);
        bb_ui_knob *kn = &in->knob[k];
        kn->label = label[0] ? label : 0;
        kn->text = bb_v3_ktext[k];
        kn->bar = (uint8_t)(pos > 127u ? 254u : pos * 2u);
        kn->flags = (uint8_t)(!label[0] ? BB_UI_KN_OFF :
                    bb_v3_ktext[k][0] == 'M' && bb_v3_ktext[k][1] == 'U' && bb_v3_ktext[k][2] == 'T' ? BB_UI_KN_MUTE : 0u);
    }
    for (k = 0; k < 27u; k++)
        in->key_led[k] = bb_nav_l.key[k];                     /* BB_NAV_LED_* = BB_UI_LED_* */
    in->keys_down = 0;
    in->keys_red = 0;
    /* 2.0 D4: what the keys do here, from the same role table as the LEDs; black keys only if it would not fit */
    if (text_w(&AF_S, (bb_nav_legend(&bb_nav, bb_v3_legend, sizeof bb_v3_legend, 1), bb_v3_legend)) > 224)
        (void)bb_nav_legend(&bb_nav, bb_v3_legend, sizeof bb_v3_legend, 0);
    in->legend = bb_v3_legend;
}

/* KIT: the picker's list (RC2: ten rows, scrolling; white keys 1-16 pick a drum kit; Dillo's keys stay notes), the
 * loaded one marked, the footer: what, which of how many, what the controls do (or why it cannot load) */
static void bb_v3_kit(bb_ui_in *in)
{
    bb_sound_picker_view view;
    unsigned count = bb_sound_picker_items(&bb_sound_pick), first = bb_v3_window(0, bb_sound_pick.cursor, count), i;
    unsigned drum = bb_sound_pick.page == BB_DRUMS;
    int valid = !in->fault && bb_sound_picker_observe(&bb_sound_pick, &bb_sound, &view);
    in->page_name = drum ? "MACHINES" : "SOUNDS";
#if BB_WITH_USER_KITS && BB_SAMPLE_HOST_INTEGRATION && defined(BB_SAMPLE_HYBRID_TRIAL)
    if (drum && !bb_sound_pick.machine && bb_sound_pick.sample_count &&
        bb_sound_pick.cursor >= bb_sound_picker_machine_items(bb_sound_pick.sample_count))
        in->page_name = "USER KITS";
#endif
    const char *tail = !valid ? BB_TX(TX_STATE_UNAVAILABLE) : !view.enabled ? "LAYER OFF" : view.muted ? "MUTED" :
                       bb_sound_pick.pending ? "LOADING" : drum ? "PRESETS PICK \xB7 EDIT: PATTERNS" :
                                                                  "PRESETS SCROLL \xB7 EDIT KEEP";
    bb_v3_footer(bb_sound_pick.cursor + 1u, count, tail);
    in->footer = bb_v3_foot;
    in->browser_state = valid && view.active_index != (int)bb_sound_pick.cursor ? BB_UI_BROWSER_BROWSE : BB_UI_BROWSER_NONE;
    in->items = 0;
    for (i = 0; i < BB_UI_LIST && first + i < count; i++) {
        bb_v3_copy(bb_v3_item(i), 40, bb_sound_picker_item_name(&bb_sound_pick, first + i));
        in->item[i] = bb_v3_item(i);
        in->item_key[i] = (uint8_t)(drum && first + i < 16u ? first + i + 1u : 0u);
        in->items++;
    }
    in->cursor = (uint8_t)(bb_sound_pick.cursor - first);
    in->playing_item = valid && view.active_index >= (int)first && view.active_index < (int)(first + BB_UI_LIST) ?
                       (uint8_t)(view.active_index - (int)first) : (uint8_t)BB_UI_NO;
    in->queued_item = bb_sound_pick.pending && bb_sound_pick.requested >= first && bb_sound_pick.requested < first + BB_UI_LIST ?
                      (uint8_t)(bb_sound_pick.requested - first) : (uint8_t)BB_UI_NO;
    in->loading = bb_sound_pick.pending;
    in->progress = (uint8_t)(bb_ticks ? ((bb_ticks - 1u) % 16u) * 16u : 0u);   /* the ring fills to the bar */
}

#if BB_WITH_USER_KITS
                                                                                                         
                                                                                                                        
                                                                                                        
static BB_UK_SMALL void bb_uk_v3_lane(char *t, unsigned c)
{
    unsigned v = bb_uk_lane_voice[c % BB_UK_VOICES], sv = bb_uk.draft.v[v].voice % BB_UK_VOICES, n = 0;
    int m = bb_uk_find(bb_uk.draft.key[bb_uk.draft.v[v].machine]);
    const char *w = m < 0 ? "MISSING" : bb_uk_machine_name((unsigned)m);
    t[n++] = bb_uk_code[v][0]; t[n++] = bb_uk_code[v][1]; t[n++] = ' '; t[n++] = ' ';
    while (*w && n < 30u) t[n++] = *w++;
    if (sv != v) { t[n++] = ' '; t[n++] = bb_uk_code[sv][0]; t[n++] = bb_uk_code[sv][1]; }
    if (m >= 0 && bb_uk.sub >> v & 1u) { t[n++] = ' '; t[n++] = 'S'; t[n++] = 'U'; t[n++] = 'B'; }
    t[n] = 0;
}
/* BUILD (black 1) and SAVE AS (EDIT) fill the KIT list: BUILD = the kit's name, the lanes around the focus;
 * SAVE AS = the four slots. 0: the existing KIT fill applies (its USER 1-4 rows come from the sound picker). */
static BB_UK_SMALL int bb_uk_v3_kit(bb_ui_in *in)
{
    unsigned i, first = 0, count = BB_UK_SLOTS, cur = bb_uk.save_slot, build = bb_nav.bank == BB_NAV_KIT_BUILD;
    if (bb_nav.page != BB_NAV_KIT || bb_nav_row || !bb_nav.bank) return 0;
    in->playing_item = (uint8_t)(bb_uk.origin < BB_UK_SLOTS ? bb_uk.origin : BB_UI_NO);
    in->queued_item = (uint8_t)BB_UI_NO;
    if (build) {
        in->footer = bb_uk.miss ? BB_TX(TX_KIT_NOT_INSTALLED_808_STANDS_IN) : bb_uk.edits ? BB_TX(TX_EDITED_EDIT_SAVE_AS) : BB_TX(TX_EDIT_SAVE_AS);
        count = BB_UK_VOICES; cur = bb_uk.lane; first = bb_v3_window(0, cur, count);
        in->playing_item = (uint8_t)BB_UI_NO;
    } else {
        in->footer = BB_TX(TX_KEYS_1_4_SLOT_EDIT_STORE_STOPPED);
    }
    for (i = 0; i < BB_UI_LIST && first + i < count; i++) {
        if (build) bb_uk_v3_lane(bb_v3_item(i), first + i);
        else bb_v3_copy(bb_v3_item(i), 40, bb_sound_picker_user_name(first + i));
        in->item[i] = bb_v3_item(i);
        in->item_key[i] = (uint8_t)(build ? 0u : i + 1u);
    }
    in->items = (uint8_t)i;
    in->cursor = (uint8_t)(cur - first);
    return 1;
}
#endif

#if BB_WITH_BROWSER && BB_WITH_RICH_DRUM_BROWSER && BB_SAMPLE_HOST_INTEGRATION
/* Voice substitutions for the highlighted pattern on the kit it will play with (the chosen NEXT KIT,
 * else the layer's current kit): "SUBST SY>CB +1". Same helper the legacy footer used; cached per
 * selection because it prepares the phrase. Covers sampled-kit fallbacks and drum B's SY/ZAP policy. */
static const char *const bb_v3_voice_code[BB_PHRASE_VOICES] = {
    "BD","SD","LT","MT","HT","LC","MC","HC","RS","CL","CP","MA","CB","CY","OH","HH","RD","SY","ZAP","AC","AC","SH"};
static char bb_v3_subst[24];
static const char *bb_v3_substitutions(const bb_player *p)
{
    static struct { const void *cat; uint16_t bank, part; uint8_t group, kit, machine, valid; } key;
    unsigned kit = BB_SAMPLE_808_ID, count = 0, first = 255, repl = 255;
    if (!p->is_drum || p->target >= BB_MACHINES) return 0;
    if (!bb_library_preview_kit(p, &kit)) kit = BB_SAMPLE_808_ID;
    if (key.valid && key.cat == p->view.catalogue && key.bank == p->view.bank_index && key.part == p->view.part_index &&
        key.group == p->view.group && key.kit == kit && key.machine == p->target)
        return bb_v3_subst[0] ? bb_v3_subst : 0;
    key.cat = p->view.catalogue; key.bank = p->view.bank_index; key.part = p->view.part_index;
    key.group = p->view.group; key.kit = (uint8_t)kit; key.machine = p->target; key.valid = 1;
    bb_v3_subst[0] = 0;
    if (bb_player_preview_substitutions(p, &bb_sound, kit, &count, &first, &repl) && count &&
        first < BB_PHRASE_VOICES && repl < BB_PHRASE_VOICES) {
        unsigned n = 0; const char *t = "SUBST ";
        while (*t) bb_v3_subst[n++] = *t++;
        for (t = bb_v3_voice_code[first]; *t; ) bb_v3_subst[n++] = *t++;
        bb_v3_subst[n++] = '>';
        for (t = bb_v3_voice_code[repl]; *t; ) bb_v3_subst[n++] = *t++;
        if (count > 1u) { bb_v3_subst[n++] = ' '; bb_v3_subst[n++] = '+'; bb_v3_subst[n++] = (char)('0' + (count - 1u > 9u ? 9u : count - 1u)); }
        bb_v3_subst[n] = 0;
    }
    return bb_v3_subst[0] ? bb_v3_subst : 0;
}
#else
static const char *bb_v3_substitutions(const void *p) { (void)p; return 0; }
#endif

#if BB_WITH_BROWSER
/* PATTERN: the browser's bank page as the list, the highlighted pattern as the title, its sections */
static void bb_v3_pattern_of(bb_ui_in *in, const bb_player *p);
static void bb_v3_pattern(bb_ui_in *in)
{
    if (bb_nav.page == BB_NAV_STUDIO) {                       /* 2.1 P3b HOME/SONGS: the conductor's list (RC2: views) */
        static const char *const coll[3] = {"SONGS", "ERAS", "PRESETS"};
        unsigned count = bb_cd_count(), cur = bb_cd.cursor, first = bb_cd.welcome ? (cur ? cur - 1u : 0u) : bb_v3_window(1, cur, count), k;
        bb_v3_footer(cur + 1u, count, "ALGO SCROLL \xB7 PLAY ALL 3");
        in->footer = bb_v3_foot;
        in->tab = coll; in->tabs = 3; in->tab_cur = bb_cd.coll;   /* the collections as tabs in the header (B5-B7) */
        in->items = 0;
        for (k = 0; k < (bb_cd.welcome ? 3u : BB_UI_LIST) && first + k < count; k++) {
            bb_browser b = bb_cd.list;
            unsigned cell = bb_cd_row(first + k), bank = cell >> 8;
            b.bank_page = (uint16_t)(bank / BB_BROWSER_BANK_KEYS);
            bb_catalogue_id id = bb_browser_bank_cell(&b, bank % BB_BROWSER_BANK_KEYS);
            if (bb_cd.coll == 2u) {
                bb_catalogue_id parts = bb_catalogue_find(&bb_factory_catalogue, id, "parts");
                bb_v3_conductor_section(bb_catalogue_at(&bb_factory_catalogue, parts, (uint8_t)cell), bb_v3_item(k), 40);
            } else
                bb_v3_cat(bb_catalogue_find(&bb_factory_catalogue, id, b.group ? "song" : "name"), bb_v3_item(k), 40);
            in->item[k] = bb_v3_item(k);
            in->item_key[k] = (uint8_t)((first + k) / 16u == bb_cd.vpage ? (first + k) % 16u + 1u : 0u);
            in->items++;
        }
        in->welcome = bb_cd.welcome;
        if(in->welcome) {
            in->page_name="CONDUCTOR";
            in->sections=(uint8_t)bb_browser_part_count(&bb_cd.list);
        }
        in->cursor = (uint8_t)(cur - first);
        in->playing_item = in->queued_item = BB_UI_NO;
        if (bb_cd.pticket && (k = bb_cd_choice_row(&bb_cd.group_choice) - first) < BB_UI_LIST)
            in->playing_item = (uint8_t)k;
        if ((k = bb_cd_list_queued() - first) < BB_UI_LIST && k != in->playing_item) {
            in->queued_item = (uint8_t)k;
            in->queued |= BB_UI_Q_PATTERN;
        }
    } else
        bb_v3_pattern_of(in, bb_library_current());
}
static void bb_v3_pattern_of(bb_ui_in *in, const bb_player *p)
{
    /* RC2: the whole list (ten rows, scrolling), the white key of each row on the 16-key page in view, the footer:
     * which of how many, then what is waiting (NEXT KIT, a voice substitution, QUEUED) or what the controls do */
    unsigned first, k, pend = bb_player_pending(p), count_banks = (unsigned)bb_browser_bank_count(&p->view);
    const char *tail = "ALGO SCROLL \xB7 EDIT LOAD", *sub = bb_v3_substitutions(p);
    unsigned n = 0;
    /* The footer now teaches the physical controls, so the status strip names
     * the musical collection instead of repeating ALGO. */
    in->page_name = p->view.group == BB_BROWSER_ACID ? "PATTERN  ACID" :
                    p->view.group ? "PATTERN  SONGS" : "PATTERN  ERAS";
    if (p->is_drum && p->target < BB_MACHINES && bb_kit_intent[p->target].valid) {   /* never silent: the next kit */
        const char *g = "NEXT KIT: ", *kit = bb_library_kit_name(bb_kit_intent[p->target].id);
        while (*g && n + 1u < sizeof bb_v3_sub) bb_v3_sub[n++] = *g++;
        while (*kit && n + 1u < sizeof bb_v3_sub) bb_v3_sub[n++] = *kit++;
        bb_v3_sub[n] = 0; tail = bb_v3_sub;
    } else if (sub) tail = sub;                                   /* the kit's voice substitution for this pattern */
    else if (pend && p->request.choice.group == p->view.group) tail = pend == 2u ? "QUEUED \xB7 NEXT BAR" : "REQUESTED";
    bb_v3_footer(p->view.bank_index + 1u, count_banks, tail);
    in->footer = bb_v3_foot;
    first = bb_v3_window(1, p->view.bank_index, count_banks);
    in->items = 0;
    for (k = 0; k < BB_UI_LIST; k++) {
        unsigned idx = first + k;
        bb_browser page = p->view;                            /* the 16-key page that holds idx, not the one in view */
        bb_catalogue_id bank;
        page.bank_page = (uint16_t)(idx / BB_BROWSER_BANK_KEYS);
        bank = idx < count_banks ? bb_browser_bank_cell(&page, idx % BB_BROWSER_BANK_KEYS) : BB_CATALOGUE_NONE;
        if (bank == BB_CATALOGUE_NONE)
            break;
        bb_v3_cat(bb_catalogue_find(&bb_factory_catalogue, bank, p->view.group ? "song" : "name"), bb_v3_item(k), 40);
        in->item[k] = bb_v3_item(k);
        in->item_key[k] = (uint8_t)(idx / BB_BROWSER_BANK_KEYS == p->view.bank_page ? idx % BB_BROWSER_BANK_KEYS + 1u : 0u);
        in->items++;
    }
    in->cursor = (uint8_t)(p->view.bank_index - first);
    in->playing_item = p->playing.ticket && p->playing.choice.group == p->view.group &&
                       p->playing.choice.bank_index >= first && p->playing.choice.bank_index < first + BB_UI_LIST ?
                       (uint8_t)(p->playing.choice.bank_index - first) : (uint8_t)BB_UI_NO;
    in->queued_item = pend && p->request.choice.group == p->view.group && p->request.choice.bank_index >= first &&
                      p->request.choice.bank_index < first + BB_UI_LIST ?
                      (uint8_t)(p->request.choice.bank_index - first) : (uint8_t)BB_UI_NO;
    in->progress = (uint8_t)(bb_ticks ? ((bb_ticks - 1u) % 16u) * 16u : 0u);
}
#endif

/* the drum layer's steps for one bar, by group (gg_808_sound: the firmware's own group map) */
static void bb_v3_lanes(bb_ui_in *in, unsigned bar)
{
    const bb_machine *m = &bb_face.requested.drums[bb_face.machine];
    unsigned k, t, g;
    for (g = 0; g < 4u; g++)
        in->gsteps[g] = in->gacc[g] = 0;
    for (k = 0; k < 16u; k++) {
        unsigned step = bar * 16u + k;
#if BB_WITH_RICH_DRUM_BROWSER
        if (bb_rich_active()) {
            for (t = 0; t < 17u; t++) {
                bb_phrase_cell cell = {0};
                int gg;
                if (!bb_phrase_view_cell(bb_rich_view(bb_face.machine), bb_rich_sources[t], step, &cell) || !cell.hits)
                    continue;
                if ((gg = gg_808_sound(bb_rich_sounds[t])) < 0)
                    continue;
                in->gsteps[gg] |= (uint16_t)(1u << k);
                if (cell.accents)
                    in->gacc[gg] |= (uint16_t)(1u << k);
            }
            continue;
        }
#endif
        if (step >= m->length)
            continue;
        for (t = 0; t < BB_DRUM_TRACKS; t++) {
            int gg;
            if (!((m->steps[step].hits >> t) & 1u))
                continue;
            if ((gg = gg_808_sound(bb_drum_sound_index(&m->sound, t))) < 0)
                continue;
            in->gsteps[gg] |= (uint16_t)(1u << k);
            if ((m->steps[step].accents >> t) & 1u)
                in->gacc[gg] |= (uint16_t)(1u << k);
        }
    }
}

/* PLAY drums: what is playing, the four group lanes, OUT and DSP */
static void bb_v3_play_drums(bb_ui_in *in)
{
    unsigned machine = bb_face.machine, length, pos, g, dt = fm1_ms - bb_v3_last_ms;
    bb_group_view view;
#if BB_WITH_BROWSER
    bb_player *p = bb_library_current();
    const bb_factory_choice *now = p->playing.ticket ? &p->playing.choice : 0;
    bb_factory_choice cur;
    if (!now && bb_browser_current(&p->view, &cur))
        now = &cur;
    bb_v3_now[0] = bb_v3_sec[0] = bb_v3_nextname[0] = 0;
    if (now) {
        bb_v3_bank_title(now, bb_v3_now, sizeof bb_v3_now);
        bb_v3_cat(bb_catalogue_find(&bb_factory_catalogue, now->part, "name"), bb_v3_sec, sizeof bb_v3_sec);
    }
    if (bb_player_pending(p)) {
        if (bb_nav_f.queued & BB_NAV_Q_SECTION)
            bb_v3_cat(bb_catalogue_find(&bb_factory_catalogue, p->request.choice.part, "name"), bb_v3_nextname,
                      sizeof bb_v3_nextname);
        else
            bb_v3_bank_title(&p->request.choice, bb_v3_nextname, sizeof bb_v3_nextname);
    }
#endif
    in->pattern_name = bb_v3_now[0] ? bb_v3_now : BB_TX(TX_CURRENT_PATTERN);
    in->section_name = bb_v3_sec;
    in->next_name = bb_v3_nextname[0] ? bb_v3_nextname : 0;
    /* position: the phrase playhead (rich) or the layer's playhead, in bars of 16 */
    length = bb_drum_visible_length(&bb_face.requested.drums[machine]);
#if BB_WITH_RICH_DRUM_BROWSER
    pos = bb_rich_active() ? bb_audio_phrase_playhead(&bb_sound, machine) : bb_playhead[machine];
#else
    pos = bb_playhead[machine];
#endif
    if (length)
        pos %= length;
    in->step = (uint8_t)(pos % 16u);
    in->bar = (uint8_t)(pos / 16u + 1u);
    in->bars = (uint8_t)(length > 16u ? (length + 15u) / 16u : 0u);
    if (!in->bars)
        in->bar = 0;
#if !BB_UI_PLAY                                              /* 2.2: PADS has no lanes, OUT or DSP */
    bb_v3_lanes(in, pos / 16u);
#endif
    /* group levels and mutes as the K1-K4 route has them; live level: each group's hit at its
     * level, falling over ~120 ms (the firmware publishes no per-group peak; README §6) */
    if (bb_group_input(&bb_sound.commands, bb_sound.groups, machine, BB_GROUP_READ, 0, 0, &view) != BB_GROUP_REFUSED) {
        for (g = 0; g < 4u; g++) {
            in->group_level[g] = (uint8_t)(view.level[g] > 127u ? 254u : view.level[g] * 2u);
            if (!view.level[g]) in->group_zero |= (uint8_t)(1u << g);
        }
        in->group_muted = (uint8_t)(view.muted & 15u);
    }
    for (g = 0; g < 4u; g++) {
        unsigned v = bb_v3_gout[g];
        v = dt >= 120u ? 0u : v * (120u - dt) / 120u;
        if (in->running && pos != bb_v3_step_seen && (in->gsteps[g] >> in->step & 1u) && !(in->group_muted >> g & 1u))
            v = in->group_level[g];
        bb_v3_gout[g] = (uint8_t)v;
        in->group_out[g] = (uint8_t)v;
    }
    bb_v3_step_seen = pos;
#if !BB_UI_PLAY
    {
        uint32_t pk = bb_visual_peak / 128u * 2u;            /* one published peak: L = R (README §6) */
        in->out_peak[0] = in->out_peak[1] = (uint8_t)(pk > 255u ? 255u : pk);
    }
#ifdef BB_DIAGNOSTICS_WHOLE_HALF
    {
        uint32_t total, budget, d;
        uintptr_t token = bb_target_enter(0);
        total = bb_target_timing.last_total_ticks;
        budget = bb_target_timing.budget_ticks;
        bb_target_leave(0, token);
        d = bb_deadline_percent(total, budget);
        in->dsp = (uint8_t)(d >= 100u ? 255u : d * 255u / 100u);
    }
#else
    in->dsp = (uint8_t)(BB_METER_CPU_Q8 > 255u ? 255u : BB_METER_CPU_Q8);
#endif
#endif
}

/* The focused Dillo side's sound name (Duo: 0 BASS, 1 LEAD). */
static const char *bb_v3_patch_name(unsigned sy)
{
    if (bb_patch_from_scene[sy] == 2) return "Era sound";
    if (bb_patch_from_scene[sy]) return BB_TX(TX_SCENE_SOUND);
#if BB_WITH_TELEGRAM
    if (sy) return bb_telegram_bank[bb_patch_index[1] % BB_TELEGRAM_PATCHES].name;
#endif
    return bb_dillo_bank[bb_patch_index[0] % BB_DILLO_PATCHES].name;
}
/* Dillo PLAY (BASS or LEAD): the patch, the sequence as a piano roll, the filter */
static void bb_v3_play_dillo(bb_ui_in *in)
{
    const unsigned sy = bb_nav_synth();
#if !BB_UI_PLAY
    const bb_sequence *seq = &bb_face.requested.synths[sy];
    unsigned k, lo = 255, len = seq->length ? seq->length : 16u, lim0, lim1, v;
#else
    unsigned lim0, lim1, v;
#endif
    in->patch = bb_v3_patch_name(sy);
    in->scale = bb_nav_scale_hint();
    if (!in->scale)
        in->scale = bb_patch_edited[sy] ? "EDITED" : "";
#if !BB_UI_PLAY                                              /* 2.2: the sheet reads its bank (bb_ui_play_firmware.inc) */
    for (k = 0; k < 16u && k < len; k++)
        if (seq->notes[k].gate && seq->notes[k].note < lo)
            lo = seq->notes[k].note;
    in->note_acc = in->note_slide = 0;
    for (k = 0; k < 16u; k++) {
        const bb_note *n = &seq->notes[k];
        in->note[k] = (int8_t)(k < len && n->gate ? (int)(n->note - lo) : -1);
        if (in->note[k] > 12) in->note[k] = (int8_t)(12 + (in->note[k] - 12) % 12 / 6);
        if (n->accent) in->note_acc |= (uint16_t)(1u << k);
        if (n->slide) in->note_slide |= (uint16_t)(1u << k);
    }
    in->step = (uint8_t)(bb_synth_playhead[sy] % 16u);
#endif
    v = bb_synth_parameter_get(&bb_face.requested, sy, BB_PARAM_CUTOFF);
    if (bb_synth_parameter_limits(sy, BB_PARAM_CUTOFF, &lim0, &lim1) && lim1 > lim0)
        in->cutoff = (uint8_t)((v < lim0 ? 0u : v > lim1 ? lim1 - lim0 : v - lim0) * 255u / (lim1 - lim0));
    v = bb_synth_parameter_get(&bb_face.requested, sy, BB_PARAM_RESONANCE);
    if (bb_synth_parameter_limits(sy, BB_PARAM_RESONANCE, &lim0, &lim1) && lim1 > lim0)
        in->reso = (uint8_t)((v < lim0 ? 0u : v > lim1 ? lim1 - lim0 : v - lim0) * 255u / (lim1 - lim0));
#if !BB_UI_PLAY
    in->octave = bb_nav.key_octave;
#endif
}

static char bb_v3_home_status[96], bb_v3_home_section[3][24], bb_v3_queued_name[48];
static uint32_t bb_v3_queued_name_ticket;
static bb_catalogue_id bb_v3_home_part[3], bb_v3_grid_bank;
static uint8_t bb_v3_grid_page, bb_v3_grid_group;
static char bb_v3_grid_song[48];
static char bb_v3_lane_song[3][48];
static bb_catalogue_id bb_v3_lane_bank[3];
static uint8_t bb_v3_lane_group[3];
/* Summarize the pending or applied mask independently of the displayed keys. If
 * the full names exceed the LCD line, every section number still fits. */
static void bb_v3_combo_name(bb_catalogue_id bank, uint64_t mask)
{
    char *out = bb_v3_queued_name;
    const unsigned cap = sizeof bb_v3_queued_name;
    bb_catalogue_id parts = bb_catalogue_find(&bb_factory_catalogue, bank, "parts");
    for (unsigned numbers = 0; numbers < 2u; numbers++) {
        unsigned used = 0;
        for (uint64_t set = mask; set; set &= set - 1u) {
            unsigned index = (unsigned)__builtin_ctzll(set);
            char name[24];
            if (numbers) name[nav_leg_num(name, index + 1u)] = 0;
            else bb_v3_conductor_section(bb_catalogue_at(&bb_factory_catalogue, parts, index), name, sizeof name);
            if (used) used = pf_append(out, used, cap, " + ");
            used = pf_append(out, used, cap, pf_section_short(name));
        }
        out[used] = 0;
        if (used < cap - 1u && text_w(&AF_S, "KIT A COMBO > ") + text_w(&AF_S, out) <= 224) break;
    }
}
/* The arrangement owns these parts only after the audio owner has
 * acknowledged its exact row and section revision. Cache source choices
 * across LCD frames, including the lineage of an edited working part. */
static struct {
    uint32_t ticket, revision;
    bb_factory_choice part[3];
} bb_v3_song_sources __attribute__((section(".pool")));
static uint8_t bb_v3_song_row;
static const bb_arr_section *bb_v3_song_parts(void)
{
    const bb_arr_controller *c = &bb_arr_workspace.controller;
    const bb_arrangement *m = &bb_arr_workspace.model;
    const bb_arr_sources *s = &bb_arr_workspace.sources;
    bb_v3_song_row = 0;
    if (!c->seen_applied || c->seen_applied != c->view.state.applied ||
        c->playing_row >= m->row_count || m->rows[c->playing_row].section != c->view.playing_section)
        return NULL;
    const bb_arr_section *section = bb_arr_find(m, c->view.playing_section);
    if (!section || section->revision != c->view.playing_revision) return NULL;
    bb_v3_song_row = (uint8_t)(c->playing_row + 1u);
    if (bb_v3_song_sources.ticket != c->seen_applied || bb_v3_song_sources.revision != section->revision) {
        bb_v3_song_sources.ticket = c->seen_applied;
        bb_v3_song_sources.revision = section->revision;
        for (unsigned r = 0; r < 3u; r++) {
            const unsigned kind = r < 2u ? BB_SOURCE_DRUM : BB_SOURCE_DILLO;
            bb_arr_part ref = section->part[r];
            for (unsigned depth = 0; ref.source == BB_ARR_WORKING && depth < BB_ARR_WORK_PARTS; depth++) {
                if (!ref.id || ref.id > s->work_count) break;
                const bb_arr_work_part *work = &s->work[ref.id - 1u];
                if (work->revision != ref.revision || work->kind != kind) break;
                ref = work->origin;
            }
            bb_factory_choice *part = &bb_v3_song_sources.part[r];
            if (bb_arr_source_choice(s, ref, kind, part)) part->catalogue = NULL;
        }
    }
    return section;
}
/* Conductor shows applied song/section for each player. The section grid
 * follows the band reference, independent of the selected redirect player. */
static void bb_v3_home(bb_ui_in *in)
{
    unsigned r, k, scope=bb_nav.conduct_scope, f = scope ? scope-1u : (bb_nav_row < 2u ? bb_nav_row : 2u),
             pg = bb_nav_section_page(&bb_nav);
    const bb_factory_choice *ref = bb_cd_group_ref();
    const bb_arr_section *arranged = bb_v3_song_parts();
    unsigned mixed_songs = 0;
    /* Plain keys always cue all players. The outlined row and footer
     * identify the separate REC+key redirect target. */
    in->instr_name = "ALL";
    for (r = 0; r < 3u; r++) {
        const unsigned sy = r < 2u ? 0u : bb_nav_synth();   /* lane 3: Dillo, the focused side */
        bb_player *p = r < 2u ? &bb_drum_library[r] : &bb_library[sy];
        bb_ui_lane *l = &in->lane[r];
        const bb_player_selection *applied=bb_cd_applied(p,r);
        const bb_factory_choice *playing=applied?&applied->choice:NULL;
        if (arranged) {
            const bb_factory_choice *source = &bb_v3_song_sources.part[r];
            playing = source->catalogue ? source : NULL;
        }
        bb_catalogue_id part=playing?playing->part:BB_CATALOGUE_NONE;
        if (bb_v3_home_part[r] != part || (part != BB_CATALOGUE_NONE && !bb_v3_home_section[r][0])) {
            if (part != BB_CATALOGUE_NONE)
                bb_v3_conductor_section(part, bb_v3_home_section[r], sizeof bb_v3_home_section[r]);
            else
                bb_v3_copy(bb_v3_home_section[r], sizeof bb_v3_home_section[r], "");
            bb_v3_home_part[r] = part;
        }
        if (ref && playing && (playing->bank != ref->bank || playing->group != ref->group)) mixed_songs = 1;
        if (!playing) {
            bb_v3_lane_song[r][0] = 0;
        } else if (bb_v3_lane_bank[r] != playing->bank ||
                   bb_v3_lane_group[r] != playing->group || !bb_v3_lane_song[r][0]) {
            bb_v3_bank_title(playing, bb_v3_lane_song[r], sizeof bb_v3_lane_song[r]);
            bb_v3_lane_bank[r] = playing->bank;
            bb_v3_lane_group[r] = playing->group;
        }
        l->name = bb_v3_home_section[r];
        l->pattern = bb_v3_lane_song[r];
        unsigned edited=r<2u&&applied&&applied!=&p->playing;
        l->part = r < 2u ? (!applied ? "NO KIT" : edited ? "EDITED" :
                            bb_library_kit_name(applied->kit)) : bb_v3_patch_name(sy);
#if BB_WITH_PROJECT_FILES
        if(r==2u&&applied)edited=(bb_sound.commands.pattern_edited>>sy)&1u;
#endif
        if (arranged) {
            edited = arranged->part[r].source == BB_ARR_WORKING;
            l->part = !arranged->part[r].id ? "NO PART" : edited ? "WORKING" : "SONG PART";
            if (!playing) l->name = arranged->part[r].id ? "UNKNOWN" : "NO PART";
        }
        l->flags = (uint8_t)((scope && r == f ? BB_UI_LN_FOCUS : 0u) |
                            (bb_cd.ticket && ((bb_cd.moved >> r) & 1u) ? BB_UI_LN_NEXT : 0u) |
                            (bb_nav_f.instr_solo & (1u << r) ? BB_UI_LN_SOLO : 0u) |
                            (edited ? BB_UI_LN_EDITED : 0u) |
                            (r < 2u ? (!bb_face.requested.drums[r].enabled ? BB_UI_LN_OFF : bb_face.requested.drums[r].muted ? BB_UI_LN_MUTED : 0u) :
                                      (bb_face.requested.synths[sy].muted ? BB_UI_LN_MUTED : 0u)));
    }
    /* the buttons: white keys 1-16 on the section page in view (names read once per song and page: the pending keys
     * are the cache's) */
    in->tile_page = (uint8_t)pg;
    {
        bb_catalogue_id bank = ref ? ref->bank : BB_CATALOGUE_NONE;
        uint8_t group = (uint8_t)(ref ? ref->group : 0u);
        if (bb_v3_grid_bank != bank || bb_v3_grid_group != group) {
            if (ref) bb_v3_bank_title(ref, bb_v3_grid_song, sizeof bb_v3_grid_song);
            else bb_v3_grid_song[0] = 0;
        }
        if (bb_v3_grid_bank != bank || bb_v3_grid_page != pg || bb_v3_grid_group != group) {
            bb_catalogue_id parts = ref ? bb_catalogue_find(&bb_factory_catalogue, ref->bank, "parts") : BB_CATALOGUE_NONE;
            for (k = 0; k < 16u; k++)
                bb_v3_conductor_section(bb_catalogue_at(&bb_factory_catalogue, parts, pg * 16u + k),
                                        bb_v3_sec_name(k), 24);
            bb_v3_grid_bank = bank;
            bb_v3_grid_page = (uint8_t)pg;
            bb_v3_grid_group = group;
        }
    }
    for (k = 0; k < 16u; k++) {
        unsigned s = pg * 16u + k;
        in->tile[k] = 0; in->tile_state[k] = BB_UI_TILE_NONE;
        if (!ref || s >= bb_nav_f.section_count || !bb_v3_sec_name(k)[0]) continue;
        in->tile[k] = bb_v3_sec_name(k);
        in->tile_state[k] = (bb_nav_f.queued & BB_NAV_Q_SECTION) &&
                            (s == bb_nav_f.section_queued || ((bb_nav_f.sec_queued >> s) & 1u)) ? BB_UI_TILE_QUEUED :
                            s == bb_nav_f.section || ((bb_nav_f.sec_lit >> s) & 1u) ? BB_UI_TILE_PLAYING : BB_UI_TILE_HALF;
    }
    /* the conductor in words, from these buttons and lanes (RC2 r2: one source, so it names what is lit) */
    in->perf_combo_playing = arranged ? 0 : (uint8_t)bb_cd_combo_players();
    in->perf_combo_queued = bb_cd.ticket && bb_cd.qset;
    /* Cues keep their accepted names on every grid page. */
    if (bb_cd.ticket) {
        if (bb_v3_queued_name_ticket != bb_cd.ticket) {
            if (bb_cd.chose)
                bb_v3_bank_title(&bb_cd.group_pending, bb_v3_queued_name, sizeof bb_v3_queued_name);
            else if (bb_cd.qset) bb_v3_combo_name(bb_cd.group_pending.bank, bb_cd.qset);
            else
                bb_v3_conductor_section(bb_cd.pending_part, bb_v3_queued_name, sizeof bb_v3_queued_name);
            bb_v3_queued_name_ticket = bb_cd.ticket;
        }
        if (bb_cd.chose) in->perf_song = bb_v3_queued_name;
        else in->perf_cue_name = bb_v3_queued_name;
    } else if (in->perf_combo_playing) {
        if (bb_v3_queued_name_ticket != bb_cd.pticket) {
            bb_v3_combo_name(bb_cd.group_choice.bank, bb_cd.pset);
            bb_v3_queued_name_ticket = bb_cd.pticket;
        }
        in->perf_cue_name = bb_v3_queued_name;
    } else {
        bb_v3_queued_name_ticket = 0;
        in->perf_song = mixed_songs ? bb_v3_grid_song : 0;
    }
    if (arranged && !bb_cd.ticket) {
        char row[4];
        fmt_int(row, bb_v3_song_row);
        unsigned used = pf_append(bb_v3_home_status, 0, sizeof bb_v3_home_status, "SONG ROW ");
        used = pf_append(bb_v3_home_status, used, sizeof bb_v3_home_status, row);
        used = pf_append(bb_v3_home_status, used, sizeof bb_v3_home_status, " / ");
        used = pf_append(bb_v3_home_status, used, sizeof bb_v3_home_status, arranged->name);
        bb_v3_home_status[used] = 0;
    } else bb_ui_perform_status(bb_v3_home_status, sizeof bb_v3_home_status, in);
    in->perf_status = bb_v3_home_status;
    /* The numbered grid already identifies the page. Keep the cue or mix visible
     * through an OCT page change while leaving refusal notices intact. */
    if ((bb_cd.ticket || in->perf_combo_playing) &&
        in->notice == bb_nav_message_text((uint8_t)(BB_NAV_MSG_PERFORM_1 + pg)))
        in->notice = 0;
    {
        static const char *const scope_footer[BB_NAV_CONDUCT_SCOPES] = {
            "KEYS ALL \xB7 K4 PICK PLAYER",
            "KEYS ALL \xB7 REC+KEY A",
            "KEYS ALL \xB7 REC+KEY B",
            "KEYS ALL \xB7 REC+KEY DILLO"
        };
        static const char *const redirect_footer[] = {
            "KEYS CUE KIT A ONLY", "KEYS CUE KIT B ONLY", "KEYS CUE DILLO ONLY"
        };
        if (scope >= BB_NAV_CONDUCT_SCOPES) scope = BB_NAV_CONDUCT_ALL;
        static const char *const stage_footer[BB_NAV_CONDUCT_SCOPES] = {
            "KEYS ALL \xB7 K4 PLAYER \xB7 B4 BAND",
            "ALL \xB7 REC+KEY A \xB7 B4 BAND",
            "ALL \xB7 REC+KEY B \xB7 B4 BAND",
            "ALL \xB7 REC+KEY DILLO \xB7 B4 BAND"
        };
        if (arranged && !in->tile[0]) in->footer = "B1-B3 MIX";
        else if (!in->accent && (bb_nav_f.song & BB_NAV_SONG_F_STAGE_BAND))
            in->footer = stage_footer[scope];
        else in->footer = scope && in->accent ? redirect_footer[scope - 1u] : scope_footer[scope];
    }
}

/* STEPS (drums): every lane x 16 steps of the shown bank, the selected voice big. Rich phrases
 * fold their 17 source voices onto the firmware's own 11 lanes (bb_rich_lanes); the selected lane
 * shows exactly the selected source. */
/* bb_ratchet_get: declared in bb_nav_firmware.inc, included earlier by bb_target_ui.c */
static void bb_v3_ratchets(bb_ui_in *in, unsigned base)   /* the selected voice's counts on the 16 visible steps */
{
    unsigned v = bb_ratchet_voice();                         /* lane, or the rich source: not in->voice (a lane) */
    for (unsigned k = 0; k < 16u; k++) {
        int n = bb_ratchet_get(bb_face.machine, v, base + k);   /* 0: copies of this hit disagree (or no cell) */
        in->ratchet[k] = (uint8_t)(n >= 1 && n <= 4 ? n : BB_UI_RATCHET_MIXED);
    }
}
#if BB_WITH_STEP_CONDITIONS
_Static_assert(BB_PC_COUNT==BB_UI_CONDITION_COUNT,"condition UI values track phrase codec");
#endif
static void bb_v3_steps(bb_ui_in *in)
{
    const bb_machine *m = &bb_face.requested.drums[bb_face.machine];
    unsigned len = bb_nav_length(), k, v, base = bb_face.bank * 16u;
    unsigned head = bb_playhead[bb_face.machine];
    in->voice_name = bb_ui_voice_name();
    in->bank = bb_face.bank;
    in->step_length = (uint8_t)len;
    in->banks = (uint8_t)(len ? (len + 15u) / 16u : 1u);
    in->step = (uint8_t)(head / 16u == in->bank ? head - base : 16u + head / 16u);
#if BB_WITH_RICH_DRUM_BROWSER
    if (bb_rich_active()) {
        unsigned sel = bb_rich_voices[bb_face.machine] % 17u;
        in->voice = bb_rich_lanes[sel];
        for (v = 0; v < 17u; v++)
            for (k = 0; k < 16u; k++) {
                bb_phrase_cell cell = {0};
                if (base + k < len && bb_phrase_view_cell(bb_rich_view(bb_face.machine), bb_rich_sources[v], base + k, &cell) &&
                    cell.hits) {
                    unsigned lane = bb_rich_lanes[v];
                    if (lane == in->voice && v != sel) continue;          /* the big row is the selected source */
                    in->vsteps[lane] |= (uint16_t)(1u << k);
                    if (cell.accents) in->vacc[lane] |= (uint16_t)(1u << k);
                }
            }
        in->voice_name = bb_rich_names[sel];
        /* Highlight the existing fader for this source. Use the exact source,
         * since several distinct sounds share the same visible step lane. */
        int group = gg_808_sound(bb_rich_sounds[sel]);
        if (group >= 0 && bb_nav_knob_role((unsigned)group) == BB_NAV_R_GROUP_KICK + (unsigned)group)
            in->knob[group].flags |= BB_UI_KN_FOCUS;
        bb_v3_ratchets(in, base);
#if BB_WITH_STEP_CONDITIONS
        for(k=0;k<16u;k++){
            unsigned condition=BB_PC_ALWAYS;
            int state=bb_phrase_view_condition(bb_rich_view(bb_face.machine),bb_rich_sources[sel],base+k,&condition);
            in->condition[k]=(uint8_t)(state==2?BB_UI_CONDITION_MIXED:state==1?condition:BB_PC_ALWAYS);
        }
#endif
        return;
    }
#endif
    in->voice = (uint8_t)(bb_face.voice < 11u ? bb_face.voice : 0u);
    for (k = 0; k < 16u; k++) {
        unsigned step = base + k;
        if (step >= m->length) break;
        for (v = 0; v < 11u; v++) {
            if (m->steps[step].hits >> v & 1u) in->vsteps[v] |= (uint16_t)(1u << k);
            if (m->steps[step].accents >> v & 1u) in->vacc[v] |= (uint16_t)(1u << k);
        }
    }
    bb_v3_ratchets(in, base);
}

#if BB_WITH_EFFECTS
/* FX: the echo machine (ECHO, TAPE, MOTION banks) from the requested effect parameters */
static unsigned bb_v3_fx_scaled(unsigned p)
{
    unsigned lo = 0, hi = 0, v = bb_fx_parameter_get(&bb_face.requested, p);
    bb_fx_parameter_limits(p, &lo, &hi);
    return hi > lo ? (v < lo ? 0u : v > hi ? 255u : (v - lo) * 255u / (hi - lo)) : 0u;
}
#if !BB_UI_PLAY
static void bb_v3_fx(bb_ui_in *in)
{
    in->echo_ms = (uint16_t)bb_fx_parameter_get(&bb_face.requested, BB_FX_ECHO_MS);
    in->repeats = (uint8_t)bb_v3_fx_scaled(BB_FX_ECHO_FEEDBACK);
    in->heads = (uint8_t)(bb_fx_parameter_get(&bb_face.requested, BB_FX_ECHO_HEADS) & 7u);
    in->banks_fx = bb_nav_bank_count(&bb_nav, BB_NAV_FX);
    in->bank_fx = bb_nav.bank;
}
#endif
#endif

#include "bb_ui_play_firmware.inc"          /* 2.2 playability: PADS, STEPS, Dillo KEYS / STEPS, FX */

/* SOUND, SAVE, EVOLVE, GLOBAL, SPRING, SENDS: what is being set as the title, what EDIT does as the
 * grey line, the four values as meters over their own knobs (labels and values: navigation v2.1) */
static void bb_v3_knobs(bb_ui_in *in)
{
    const char *edit = bb_nav_edit_label(&bb_nav, &bb_nav_f);
    unsigned n = 0;
    const char *t = "";
    switch (bb_nav.page) {
    case BB_NAV_SOUND:
        t = bb_nav_is_drum() ? bb_ui_voice_name() : bb_v3_patch_name(bb_nav_synth());
        break;
#if BB_WITH_SESSION
    case BB_NAV_SAVE: {
        char *o = bb_v3_title; const char *w = "SCENE ";
        while (*w) o[n++] = *w++;
        o[n++] = (char)('1' + bb_scene_slot % 9u);
        if (bb_claim_armed(&bb_session_claim, bb_scene_slot)) {   /* other data: the held-SAVE prompt */
            w = BB_TX(TX_HAS_OTHER_DATA);
            while (*w) o[n++] = *w++;
            edit = bb_claim_prompt(&bb_session_claim);
        }
        o[n] = 0;
        if (bb_face.requested.running) edit = BB_TX(TX_STOP_TO_STORE_OR_LOAD);
        break;
    }
#endif
    case BB_NAV_EVOLVE: t = bb_nav_mode_name(bb_nav_f.gen_mode);   /* 2.1 P1: the mode is the title; EDIT: CAPTURE / APPLY */
#if BB_WITH_EVOLVE && BB_WITH_RICH_DRUM_RENDER
        /* 2.0 D4: EUCLID / RANDOM (black 3) turn the generator's own values (bb_gen_knob), not EVOLVE's: name and
         * show those, so the knob bar and the turned-knob pop-up say what the knob really changes */
        if (bb_gen_state.mode) {
            static const char *const eu[4] = {"HITS", "STEPS", "ROTATE", "ACCENTS"};
            static const char *const ra[4] = {"DENSITY", "ACCENT", "ANCHOR", "SEED"};
            static const uint16_t top[2][4] = {{64, 64, 64, 64}, {100, 100, 100, 9999}};
            unsigned e = bb_gen_state.mode == 1u, k, v[4];
            if (e) { v[0] = bb_gen_state.hits; v[1] = bb_gen_state.steps; v[2] = bb_gen_state.rotation; v[3] = bb_gen_state.accents; }
            else { v[0] = bb_gen_state.density; v[1] = bb_gen_state.accent; v[3] = bb_gen_state.seed;
                   v[2] = bb_face.requested.page == BB_DRUMS ? bb_gen_state.anchor : bb_gen_state.slide; }
            for (k = 0; k < 4u; k++) {
                bb_ui_knob *kn = &in->knob[k];
                kn->label = e ? eu[k] : k == 2u && bb_face.requested.page != BB_DRUMS ? "SLIDE" : ra[k];
                fmt_int(bb_v3_ktext[k], (int32_t)v[k]);
                kn->text = bb_v3_ktext[k];
                kn->bar = (uint8_t)(v[k] >= top[!e][k] ? 255u : v[k] * 255u / top[!e][k]);
                kn->flags = 0;
            }
        }
#endif
        break;
    case BB_NAV_GLOBAL: t = "GLOBAL"; break;
    default: t = 0; break;                                        /* FX SPRING / SENDS: 2.1 P1: the tabs name them */
    }
    if (!n) bb_v3_copy(bb_v3_title, sizeof bb_v3_title, t ? t : "");
    in->title = bb_v3_title[0] ? bb_v3_title : 0;
    if (bb_nav.page == BB_NAV_EVOLVE && edit && edit[0]) bb_v3_cat2(bb_v3_sub, sizeof bb_v3_sub, "EDIT: ", edit);
    else bb_v3_copy(bb_v3_sub, sizeof bb_v3_sub, edit && edit[0] ? edit : "");
    in->subtitle = bb_v3_sub[0] ? bb_v3_sub : 0;
}

/* a turned knob: its pop-up. A value that changes because the page, the focus (labels) or the
 * chosen kit / pattern / patch changed is not a turn; only the same knob on the same thing is.
 * 2.0 D4: shared with SONG (bb_arr_draw), so every page's knobs show NAME + value when turned. */
static void bb_v3_turns(bb_ui_in *in, uint8_t page)
{
    unsigned k;
    uint32_t ctx = h_s(h_s(h_s(h_u(H0, page), in->title), in->pattern_name), in->patch);
    for (k = 0; k < 4u; k++) {
        const bb_ui_knob *kn = &in->knob[k];
        if (bb_v3_shown && page == bb_v3_kpage && ctx == bb_v3_ctx && fm1_ms - bb_v3_ctx_t >= BB_V3_SETTLE_MS &&
            kn->label && kn->label == bb_v3_klab[k] && kn->bar != bb_v3_kpos[k]) {
            in->events |= BB_EV_KNOB;
            in->ev_knob = (uint8_t)k;
        }
        bb_v3_kpos[k] = kn->bar;
        bb_v3_klab[k] = kn->label;
    }
    if (page != bb_v3_kpage || ctx != bb_v3_ctx || !bb_v3_shown)
        bb_v3_ctx_t = fm1_ms;
    bb_v3_kpage = page;
    bb_v3_ctx = ctx;
}

/* bb_target_draw: draw this frame with v3; 0 = a service screen (update boot, diagnostics) */
static int bb_v3_draw(void)
{
    uint8_t page = bb_v3_page();
    bb_ui_in *in = &bb_v3_in;
    if (page == BB_UI_NO) {
        if (bb_v3_shown) {                                    /* back to the existing renderer: all of it */
            bb_v3_shown = 0;
            bb_dirty = 1;
        }
        return 0;
    }
    if (!bb_v3_shown) {
        bb_v3_init(&bb_v3, fm1_ms, bb_face.requested.running);
        bb_v3_invalidate(&bb_v3);
    } else if ((int32_t)(fm1_ms - bb_v3_next) < 0) {
        return 1;                                             /* ~30 frames a second */
    }
    bb_v3_next = fm1_ms + 33u;
    if (ux.light || ux.mono) {                                /* text ramps on a dark, colour palette */
        ux.light = 0;
        ux.mono = 0;
        ux.gen++;
    }
    memset(in, 0, sizeof *in);
    bb_v3_common(in, page);
    switch (page) {
    case BB_UI_STUDIO: bb_v3_home(in); break;
    case BB_UI_KIT:
#if BB_WITH_USER_KITS
        if (bb_sound_pick_visible() && bb_uk_v3_kit(in)) break;
#endif
        if (bb_sound_pick_visible()) bb_v3_kit(in);
        else in->footer = BB_TX(TX_SOUND_LIST_NOT_READY);
        break;
#if BB_WITH_BROWSER
    case BB_UI_PATTERN:
        if ((bb_library_page || bb_nav.page == BB_NAV_STUDIO) && bb_library_current()->ready) bb_v3_pattern(in);
        else in->footer = BB_TX(TX_PATTERNS_NOT_READY);
        break;
#endif
    case BB_UI_PLAY_DRUMS: bb_v3_play_drums(in); break;
    case BB_UI_STEPS: bb_v3_steps(in); break;
#if BB_WITH_EFFECTS && !BB_UI_PLAY
    case BB_UI_FX: bb_v3_fx(in); break;
#endif
    case BB_UI_KNOBS: bb_v3_knobs(in); break;
    default: bb_v3_play_dillo(in); break;
    }
    bb_pl_fill(in, page);                                     /* 2.2: the playing pages' own state */
    in->events |= bb_v3_legend_event(page);
    bb_v3_turns(in, page);
    bb_v3_last_ms = fm1_ms;
    bb_v3_shown = 1;
    bb_dirty = 0;
    (void)bb_v3_frame(&bb_v3, in);
    return 1;
}
#else
static int bb_v3_draw(void) { return 0; }
#endif
