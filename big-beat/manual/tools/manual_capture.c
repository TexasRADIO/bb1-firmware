/* SPDX-License-Identifier: GPL-3.0-only
 * Lane LP-P4 (BB1 2.1 manual): screens for the player's manual, drawn by the firmware's own renderer
 * (gfx.c, fonts, art, bb_ui_v3) through the tree's own capture harness (capture.c up to main(), copied into
 * cap_prelude.h unchanged: layout lint, generated HELP rows, FX / EVOLVE tabs and bb_nav's LEDs included).
 * Page names, knob labels, key legends, EDIT captions, help rows, the key strip and notices come from the tree's
 * own bb_nav.c (2.1 P1: one table) and BBTX text block; the music shown (songs, sections, kits) is real catalogue
 * names used as example state. Built with the release recipe's flags (composition-20-sloop extra_flags + the
 * front command's -D set, release_flags.txt) and the firmware's feature mask on every navigation state.
 * Host only: example data, not device photos. Base: the 2.0 manual's tool (claude/2.0/manual/tools/). Synced to RC1 525d2bcf. */
#include "cap_prelude.h"

static char pname[32];
/* as bb_v3_common (RC1 bb_nav_status_name): page + bank, the bank alone when that would be cut, else the page label */
static void page_name(const bb_nav_state *s)
{
    (void)bb_nav_status_name(s, pname, sizeof pname);   /* RC1: the one composition the firmware and the gate use */
    in.page_name = pname;
}
#define FEAT (BB_NAV_F_TAP | BB_NAV_F_SCALE | BB_NAV_F_THROW | BB_NAV_F_STEP_PITCH)   /* bb_nav_firmware.inc BB_NAV_FEATURES */
static bb_nav_state nav(uint8_t row, uint8_t page, uint8_t keys, uint8_t bank)
{
    bb_nav_state s; bb_nav_out o;
    bb_nav_init(&s, BB_NAV_STUDIO, 400);
    bb_nav_set_features(&s, FEAT);
    bb_nav_set_instrument(&s, row < 2 ? BB_NAV_INSTR_DRUM : BB_NAV_INSTR_DILLO, row < 2 ? 2 : 1);
    bb_nav_set_focus(&s, row);
    memset(&o, 0, sizeof o); bb_nav_goto(&s, page, &o);
    s.play_keys = keys; s.bank = bank;
    return s;
}
/* the band as the firmware's HOME lanes show it (bb_v3_home): section name, song, kit / sound */
static void band(const char *song, const char *sec)
{
    in.lane[0].name = sec; in.lane[0].pattern = song; in.lane[0].part = "LinnDrum"; in.lane[0].flags = 0; in.lane[0].level = 180;
    in.lane[1].name = sec; in.lane[1].pattern = song; in.lane[1].part = "DrumTraks"; in.lane[1].flags = 0; in.lane[1].level = 150;
    in.lane[2].name = sec; in.lane[2].pattern = song; in.lane[2].part = "Square Acid"; in.lane[2].level = 140;
}
static void real_base(bb_nav_state *s, uint8_t row, uint8_t page)
{
    base(s, row, page);
    page_name(s);
    band("Blue Monday", "Main");
    in.lane[0].flags = (uint8_t)(row == 0 ? BB_UI_LN_FOCUS : 0); in.lane[1].flags = (uint8_t)(row == 1 ? BB_UI_LN_FOCUS : 0);
    in.lane[2].flags = (uint8_t)(row == 2 ? BB_UI_LN_FOCUS : 0);
    in.pattern_name = "Blue Monday"; in.section_name = "Main";
    in.patch = "Square Acid"; in.scale = "A MIN";
    pf.section = 3; pf.section_count = 6;
    real_leds(s);
}
static void legend_shot(const char *name)
{
    uint32_t ev = in.events; in.events |= BB_EV_LEGEND; shot(name); in.events = ev;
    printf("legend %-24s %s\n", name, in.legend ? in.legend : "");
}
static void help_shot(bb_nav_state *s, const char *name, uint8_t explain)
{
    s->help = 1; s->explain = explain; help_fill(s, 0, 0); real_leds(s); in.help = 1;
    if (explain & BB_NAV_EX_KEY) {      /* the note line as bb_v3_help_fill writes it: "B5: <its text>" (or "nothing here") */
        uint8_t key = explain & 31u, a, role = bb_nav_key_role(s, key, 0, &a);
        char nm[8]; const char *says = "";
        keyname(nm, key, key);
        if (bb_nav_key_led(s, &pf, key, 0) != BB_NAV_LED_DARK) says = bb_nav_help_text(bb_nav_key_help(role, a));
        snprintf(hnote, sizeof hnote, "%s: %s", nm, says && says[0] ? says : "nothing here"); in.help_note = hnote;
    }
    shot(name);
    in.help = 0; s->help = 0; s->explain = 0; real_leds(s);
}
static void kit_list(unsigned first, unsigned cursor, unsigned playing)
{
    /* the KIT list as bb_sound_picker_item_name orders it on drum A: 808, the sampled machines, USER 1-4 */
    static const char *const K[] = {"808", "LinnDrum", "DrumTraks", "DMX", "Linn LM-1", "Simmons", "Rhythm King", "Drumulator",
        "USER 1 ", "USER 2 ", "USER 3 ", "USER 4 "};
    in.items = 5; for (unsigned i = 0; i < 5; i++) in.item[i] = K[first + i];
    in.cursor = (uint8_t)(cursor - first); in.playing_item = playing >= first && playing < first + 5 ? (uint8_t)(playing - first) : BB_UI_NO;
    in.queued_item = BB_UI_NO;
    in.title = K[cursor]; in.subtitle = "LOADED LinnDrum";
}
/* a list page as bb_v3_pattern_of fills it: five around the highlighted bank, the highlighted one's sections */
static void list_page(const char *title, const char *sub, const char *const *items, unsigned n, unsigned cursor, unsigned playing,
                      const char *const *secs, unsigned nsec, unsigned sec_cur)
{
    in.title = title; in.subtitle = sub; in.items = (uint8_t)n;
    for (unsigned i = 0; i < n; i++) in.item[i] = items[i];
    in.cursor = (uint8_t)cursor; in.playing_item = (uint8_t)playing; in.queued_item = BB_UI_NO;
    in.sections = (uint8_t)nsec; for (unsigned i = 0; i < nsec; i++) { in.sec_name[i] = secs[i]; in.sec_bars[i] = 1; }
    in.sec_cur = (uint8_t)sec_cur; in.sec_next = BB_UI_NO; in.sec_pos = 90;
    in.browser_state = BB_UI_BROWSER_BROWSE;
}
static const char *const BM[6] = {"Break", "Intro 1", "Intro 2", "Main", "Fill 1", "Fill 2"};   /* Blue Monday's sections */
static const char *const KITS[] = {"808", "LinnDrum", "DrumTraks", "DMX", "Linn LM-1", "Simmons", "Rhythm King", "Drumulator",
        "USER 1 ", "USER 2 ", "USER 3 ", "USER 4 "};   /* KIT A's picker order */
static const char *const DILLO[9] = {"Silver Pulse", "Liquid Wire", "Dry Jack", "Open Circuit", "Rubber Acid", "Long Fuse",
                                     "Hot Valve", "Hollow Dub", "Square Acid"};   /* bb_dillo_bank order */
/* a chooser list as RC2 fills it: rows, cursor, the playing one, white-key numbers from key0 (0 = none, as Dillo's PATCH) */
static void clist(const char *const *it, unsigned n, unsigned cursor, unsigned playing, unsigned key0)
{
    in.title = in.subtitle = 0; in.items = (uint8_t)n;
    for (unsigned i = 0; i < n; i++) { in.item[i] = it[i]; in.item_key[i] = (uint8_t)(key0 && key0 + i <= 16u ? key0 + i : 0u); }
    in.cursor = (uint8_t)cursor; in.playing_item = (uint8_t)playing; in.queued_item = BB_UI_NO;
}
/* PERFORM as bb_v3_home fills it: the focused part's song, one button per section on the page, status, the footer */
static void perf(const char *const *names, unsigned n, unsigned playing, unsigned queued, unsigned page, const char *song, const char *status)
{
    static char shown[48], identity[64];
    (void)status;
    snprintf(identity, sizeof identity, "%s / %s", in.instr == BB_UI_INST_DILLO ? "RUBBER ACID" :
             in.instr == BB_UI_INST_B ? "DRUMTRAKS" : "LINNDRUM", song);
    in.perf_song = identity; in.tile_page = (uint8_t)page;
    for (unsigned k = 0; k < 16; k++) {
        in.tile[k] = k < n ? names[k] : 0;
        in.tile_state[k] = (uint8_t)(k >= n ? BB_UI_TILE_NONE : k == playing ? BB_UI_TILE_PLAYING : k == queued ? BB_UI_TILE_QUEUED : BB_UI_TILE_HALF);
    }
    in.footer = "ARP PLAY \xB7 SEQ STEPS \xB7 EDIT SONGS";
    for (unsigned r = 0; r < 3; r++) { in.lane[r].name = playing < n ? names[playing] : ""; in.lane[r].flags = (uint8_t)(in.instr == r || (r == 2 && in.instr == BB_UI_INST_DILLO) ? BB_UI_LN_FOCUS : 0); }
    if (queued != BB_UI_NO)
        for (unsigned r = 0; r < 3; r++) in.lane[r].flags |= BB_UI_LN_NEXT;
    bb_ui_perform_status(shown, sizeof shown, &in);
    in.perf_status = shown;
}
static void knobs(const char *const L[4], const char *const V[4])
{
    for (int k = 0; k < 4; k++) { in.knob[k].label = L[k]; in.knob[k].text = V[k]; in.knob[k].flags = L[k] ? 0 : BB_UI_KN_OFF; }
}
/* the KNOBS pages' title and grey line as bb_v3_knobs makes them (EVOLVE: "EDIT: " + the caption) */
static void knobs_head(const bb_nav_state *s, const char *title)
{
    static char sub[32];
    const char *e = bb_nav_edit_label(s, &pf);
    in.title = title;
    if (s->page == BB_NAV_EVOLVE && e[0]) { snprintf(sub, sizeof sub, "EDIT: %s", e); in.subtitle = sub; }
    else in.subtitle = e[0] ? e : 0;
}

int main(int argc, char **argv)
{
    out = fopen(argc > 1 ? argv[1] : "manual.raw", "wb");
    bb_ui_init(&u, 0, 1);
    bb_nav_state s;
    /* ---- HOME: the song list (power-on; RC2 bb_v3_pattern on STUDIO: tabs SONGS · ERAS · PRESETS, ten rows, footer) */
    {
        static const char *const coll[3] = {"SONGS", "ERAS", "PRESETS"};
        static const char *const SG[10] = {"Planet Rock", "Blue Monday", "Sexual Healing", "In the Air Tonight", "Voodoo Ray",
                                          "Paul Revere", "Clear", "Egypt, Egypt", "It's Yours", "Posse On Broadway"};
        static const char *const ER[10] = {"Motorik Electronics", "Late-70s Machine Pop", "New Romantic", "Futurist Pop", "Synth Pop",
                                          "Electro Boogie", "Hi-NRG", "Industrial Pop", "Acid House", "Hip-Hop"};
        static const char *const PR[4] = {"Roland CR-8000", "Roland CR-78", "Roland TR-66", "Roland TR-77"};
        s = nav(0, BB_NAV_STUDIO, 0, BB_NAV_HOME_SONGS); real_base(&s, 0, BB_UI_PATTERN); in.running = 0;
        pf.running = 0; pf.list_count = 16; pf.list_cursor = 0; pf.list_playing = BB_NAV_NONE_IDX; pf.home_take = 7; pf.home_coll = 0; real_leds(&s);
        clist(SG, 10, 0, BB_UI_NO, 1); in.tab = coll; in.tabs = 3; in.tab_cur = 0;
        in.footer = "SONGS 1/67 \xB7 PLAY LOADS ALL 3"; shot("studio-songs"); legend_shot("studio-songs-legend");
        in.welcome = 1; in.page_name = "CONDUCTOR"; in.cursor = 1; in.sections = 6;
        in.footer = "SONGS 2/67 \xB7 PLAY LOADS ALL 3"; shot("welcome");
        in.welcome = 0; page_name(&s);
        help_shot(&s, "help-songs", 0);
        real_base(&s, 0, BB_UI_PATTERN); in.running = 0; pf.running = 0; pf.list_count = 16; pf.list_cursor = 1; pf.home_coll = 1; real_leds(&s);
        clist(ER, 10, 5, BB_UI_NO, 1); in.tab = coll; in.tabs = 3; in.tab_cur = 1;
        in.footer = "ERAS 6/21 \xB7 PLAY LOADS ALL 3"; shot("studio-songs-eras");
        real_base(&s, 0, BB_UI_PATTERN); in.running = 0; pf.running = 0; pf.list_count = 4; pf.list_cursor = 2; pf.home_coll = 2; real_leds(&s);
        clist(PR, 4, 2, BB_UI_NO, 1); in.tab = coll; in.tabs = 3; in.tab_cur = 2;
        in.footer = "PRESETS 3/4 \xB7 PLAY LOADS ALL 3"; shot("studio-songs-presets");
    }
    /* ---- RHYTHM CONDUCTOR: HOME's coordinated three-player destination. */
    s = nav(0, BB_NAV_STUDIO, 0, BB_NAV_HOME_CONDUCT); s.landing = 1; real_base(&s, 0, BB_UI_STUDIO);
    perf(BM, 6, 3, BB_UI_NO, 0, "Blue Monday", "Playing Main"); shot("studio"); legend_shot("studio-legend");
    /* ---- HOME destinations: SELECT changes the visible instrument here, not merely the focus outline. */
    s = nav(0, BB_NAV_PLAY, BB_NAV_PLAY_LIVE, 0); s.landing = 1; real_base(&s, 0, BB_UI_PLAY_DRUMS);
    in.bar = 2; in.bars = 4; shot("home-big-beat");
    s = nav(2, BB_NAV_PLAY, BB_NAV_PLAY_LIVE, 0); s.landing = 1; real_base(&s, 2, BB_UI_PLAY_DILLO);
    shot("home-dillo");
    s = nav(2, BB_NAV_STUDIO, 0, BB_NAV_HOME_CONDUCT); real_base(&s, 2, BB_UI_STUDIO);
    perf(BM, 6, 3, BB_UI_NO, 0, "Blue Monday", "Playing Main"); shot("studio-dillo-focus");
    s = nav(0, BB_NAV_STUDIO, 0, BB_NAV_HOME_CONDUCT); real_base(&s, 0, BB_UI_STUDIO);
    help_shot(&s, "help-studio", 0);
    help_shot(&s, "help-explain", (uint8_t)(BB_NAV_EX_KEY | BB_NAV_WHITE(5)));
    real_base(&s, 0, BB_UI_STUDIO); pf.queued |= BB_NAV_Q_SECTION; pf.section_queued = 4; real_leds(&s);
    perf(BM, 6, 3, 4, 0, "Blue Monday", "Fill 1 next bar \xB7 press again to cancel"); shot("studio-queued");
    in.notice = "TOO LATE TO CANCEL"; shot("notice-too-late"); in.notice = 0;
    real_base(&s, 0, BB_UI_STUDIO); pf.running = 0; in.running = 0; real_leds(&s);
    perf(BM, 6, 0, BB_UI_NO, 0, "Blue Monday", "Ready: Break \xB7 PLAY starts"); shot("studio-ready");
    s = nav(1, BB_NAV_STUDIO, 0, BB_NAV_HOME_CONDUCT); real_base(&s, 1, BB_UI_STUDIO);   /* mixed songs: KIT A holds Sexual Healing */
    pf.queued |= BB_NAV_Q_SECTION; pf.section_queued = 4; real_leds(&s);
    perf(BM, 6, 3, 4, 0, "Blue Monday", "Fill 1 next bar \xB7 press again to cancel");
    in.lane[0].name = "Part 2"; in.lane[0].flags = 0; in.lane[1].flags = BB_UI_LN_FOCUS; shot("studio-hold");
    s = nav(0, BB_NAV_STUDIO, 0, BB_NAV_HOME_CONDUCT); real_base(&s, 0, BB_UI_STUDIO);
    perf(BM, 6, 3, BB_UI_NO, 0, "Blue Monday", "Playing Main"); in.lane[1].flags = BB_UI_LN_MUTED;
    pf.instr_muted = 2; real_leds(&s); shot("studio-muted-b");
    real_base(&s, 0, BB_UI_STUDIO); perf(BM, 6, 3, BB_UI_NO, 0, "Blue Monday", "Playing Main");
    in.notice = "CUE REFUSED: B NEEDS VOICES"; shot("notice-cue-refused"); in.notice = 0;
    {   /* the last section page of the TR-66 (54 sections, pages of 16: 49-54) and the CR-78's 11 on one page */
        static const char *const TR[6] = {"4 Beat / Swing 3 A", "4 Beat / Swing 3 B", "4 Beat / Shuffle A", "4 Beat / Shuffle B",
                                           "4 Beat / Tango A", "4 Beat / Tango B"};
        static const char *const CR[11] = {"Rock 1", "Rock 2", "Disco 1", "Disco 2", "Waltz", "Shuffle", "Slow Rock", "Swing",
                                           "Foxtrot", "Tango", "Bossa Nova"};
        s = nav(0, BB_NAV_STUDIO, 0, BB_NAV_HOME_CONDUCT); s.sec_count = 54; s.sec_page = 3; real_base(&s, 0, BB_UI_STUDIO);
        pf.section_count = 54; pf.section = 49; real_leds(&s);
        perf(TR, 6, 1, BB_UI_NO, 3, "Roland TR-66", "Playing 4 Beat / Swing 3 B"); shot("studio-p4");
        s = nav(0, BB_NAV_STUDIO, 0, BB_NAV_HOME_CONDUCT); real_base(&s, 0, BB_UI_STUDIO); pf.section_count = 11; pf.section = 0; real_leds(&s);
        perf(CR, 11, 0, BB_UI_NO, 0, "Roland CR-78", "Playing Rock 1"); shot("studio-cr78");
    }
    real_base(&s, 0, BB_UI_STUDIO); perf(BM, 6, 3, BB_UI_NO, 0, "Blue Monday", "Playing Main");
    in.events = 0; u.idle_t = in.now_ms - (BB_UI_ATTRACT_MS + 1000u); shot("attract");
    bb_ui_init(&u, 0, 1);
    /* ---- PLAY drums and STEPS */
    s = nav(0, BB_NAV_PLAY, 0, 0); real_base(&s, 0, BB_UI_PLAY_DRUMS); in.bar = 2; in.bars = 4; shot("play-a"); legend_shot("play-a-legend");
    help_shot(&s, "help-play", 0);
    real_base(&s, 0, BB_UI_PLAY_DRUMS); in.knob_turning = 1; in.events |= BB_EV_KNOB; in.ev_knob = 1; shot("play-a-turn"); in.knob_turning = BB_UI_NO;
    real_base(&s, 0, BB_UI_PLAY_DRUMS); in.notice = bb_tx(TX_KIT_APPLIED); shot("notice-kit-applied"); in.notice = 0;
    s = nav(0, BB_NAV_PLAY, 1, 0); bb_nav_set_voice(&s, 10); real_base(&s, 0, BB_UI_STEPS); in.voice_name = "CLOSED HAT"; in.voice = 10;
    for (int i = 0; i < 16; i++) in.ratchet[i] = 1;
    in.ratchet[6] = 2; in.ratchet[14] = 4; in.vsteps[10] = 0x5555; shot("steps-a"); legend_shot("steps-a-legend");
    help_shot(&s, "help-steps", 0);
    s.step_hold = 1u << 14; s.step_hold_key = 14; s.step_hold_arg = 14;
    real_base(&s, 0, BB_UI_STEPS); in.voice_name = "CLOSED HAT"; in.voice = 10; in.vsteps[10] = 0x5555;
    for (int i = 0; i < 16; i++) in.ratchet[i] = 1;
    in.ratchet[6] = 2; in.ratchet[14] = 4; in.keys_down = 1u << 14;
    ktext[3][0] = 'x'; ktext[3][1] = '4'; ktext[3][2] = 0; in.knob[3].text = ktext[3];
    in.knob_turning = 3; in.events |= BB_EV_KNOB; in.ev_knob = 3; shot("steps-a-ratchet"); in.knob_turning = BB_UI_NO;
    /* ---- PLAY / STEPS Dillo */
    s = nav(2, BB_NAV_PLAY, 0, 0); real_base(&s, 2, BB_UI_PLAY_DILLO); shot("play-dillo"); legend_shot("play-dillo-legend");
    s = nav(2, BB_NAV_PLAY, 1, 0); real_base(&s, 2, BB_UI_PLAY_DILLO); shot("steps-dillo"); legend_shot("steps-dillo-legend");
    /* ---- KIT (RC2 bb_v3_kit: the list, ten rows, the loaded one marked, keys 1-16, footer) */
    s = nav(0, BB_NAV_KIT, 0, 0); real_base(&s, 0, BB_UI_KIT); pf.list_count = 12; pf.list_cursor = 2; pf.list_playing = 1; real_leds(&s);
    clist(KITS, 10, 2, 1, 1); in.footer = "KITS 3/12 \xB7 keys 1-16 \xB7 EDIT choose"; shot("kit-a"); legend_shot("kit-a-legend");
    help_shot(&s, "help-kit", 0);
    real_base(&s, 0, BB_UI_KIT); clist(KITS + 2, 10, 8, BB_UI_NO, 0); in.footer = "KITS 11/12 \xB7 keys 1-16 \xB7 EDIT choose"; shot("kit-a-user");
    real_base(&s, 0, BB_UI_KIT); clist(KITS, 10, 2, 1, 1); in.queued_item = 2; in.loading = 1; in.progress = 140;
    in.footer = "KITS 3/12 \xB7 LOADING"; in.notice = bb_tx(TX_KIT_QUEUED); shot("notice-kit-queued"); in.notice = 0; in.loading = 0;
    s = nav(1, BB_NAV_KIT, 0, 0); real_base(&s, 1, BB_UI_KIT); clist(KITS, 8, 2, 2, 1); in.footer = "KITS 3/8 \xB7 keys 1-16 \xB7 EDIT choose"; shot("kit-b");
    s = nav(2, BB_NAV_KIT, 0, 0); real_base(&s, 2, BB_UI_KIT);   /* Dillo PATCH: the keys stay notes (2.1 P2); no key numbers */
    clist(DILLO, 9, 6, 8, 0); in.footer = "SOUNDS 7/9 \xB7 PRESETS \xB7 EDIT keep"; shot("kit-dillo"); legend_shot("kit-dillo-legend");
    help_shot(&s, "help-patch", 0);
    clist(DILLO, 9, 8, 8, 0); in.footer = "SOUNDS 9/9 \xB7 PRESETS \xB7 EDIT keep"; in.notice = "SOUND RESTORED"; shot("notice-sound-restored"); in.notice = 0;
    /* ---- KIT: BUILD and SAVE AS (drum A; bb_uk_v3_kit: the list, the footer says what EDIT does) */
    {
        static const char *const LN[] = {"BD  LinnDrum", "SD  DMX", "LT  Simmons", "MT  Simmons", "HT  Simmons", "RS  LinnDrum",
                                         "CP  DMX", "CB  808", "CY  LinnDrum", "OH  DrumTraks"};
        s = nav(0, BB_NAV_KIT, 0, BB_NAV_KIT_BUILD); real_base(&s, 0, BB_UI_KIT);
        clist(LN, 10, 1, BB_UI_NO, 0); in.footer = bb_tx(TX_EDITED_EDIT_SAVE_AS);
        static const char *const KL[4] = {"LEVEL", "TUNE", "DECAY", "VOICE"}, *const KV[4] = {"100", "64", "64", "SD"};
        knobs(KL, KV);
        shot("kit-build"); legend_shot("kit-build-legend");
        help_shot(&s, "help-build", 0);
        s = nav(0, BB_NAV_KIT, 0, BB_NAV_KIT_SAVE_AS); real_base(&s, 0, BB_UI_KIT); pf.running = 0; real_leds(&s);
        static const char *const SL[] = {"USER 1 ", "USER 2 ", "USER 3 ", "USER 4 "};
        clist(SL, 4, 0, BB_UI_NO, 1); in.footer = bb_tx(TX_KEYS_1_4_SLOT_EDIT_STORE_STOPPED); in.running = 0;
        shot("kit-save-as"); legend_shot("kit-save-as-legend");
    }
    /* ---- PATTERN (RC2 bb_v3_pattern_of: the list, footer) */
    {
        static const char *const ER[10] = {"Motorik Electronics", "Late-70s Machine Pop", "New Romantic", "Futurist Pop", "Synth Pop",
                                          "Electro Boogie", "Hi-NRG", "Industrial Pop", "Acid House", "Hip-Hop"};
        s = nav(0, BB_NAV_PATTERN, 0, 0); real_base(&s, 0, BB_UI_PATTERN); clist(ER, 10, 5, 5, 1); in.footer = "ERAS 6/21 \xB7 keys 1-16 \xB7 EDIT choose";
        shot("pattern-a"); legend_shot("pattern-a-legend");
        help_shot(&s, "help-pattern", 0);
        real_base(&s, 0, BB_UI_PATTERN); clist(ER, 10, 6, 5, 1); in.queued_item = 6; in.footer = "ERAS 7/21 \xB7 NEXT KIT: DrumTraks"; shot("pattern-a-next");
        s = nav(2, BB_NAV_PATTERN, 0, 0); real_base(&s, 2, BB_UI_PATTERN);
        static const char *const AC[10] = {"Alamo", "San Jacinto", "Austin", "San Antonio", "El Paso", "Rio Grande", "Brazos", "Colorado",
                                          "Davis Mtns", "Chisos"};
        clist(AC, 10, 0, 0, 1); in.footer = "ACID 1/27 \xB7 keys 1-16 \xB7 EDIT choose"; shot("pattern-dillo-acid");
    }
    /* ---- SOUND */
    s = nav(0, BB_NAV_SOUND, 0, 0); real_base(&s, 0, BB_UI_KNOBS); in.title = "KICK"; in.subtitle = 0; shot("sound-a"); legend_shot("sound-a-legend");
    help_shot(&s, "help-sound", 0);
    s = nav(2, BB_NAV_SOUND, 0, 0); real_base(&s, 2, BB_UI_KNOBS); in.title = "Square Acid"; in.subtitle = 0; shot("sound-dillo");
    /* ---- FX: five named tabs on white 1-5 */
    { static const char *const B[] = {"echo", "tape", "spring", "motion", "sends"};
      for (uint8_t b = 0; b < 5; b++) {
          char nm[32]; s = nav(0, BB_NAV_FX, 0, b); uint8_t v3 = bb_v3_route(&s, 0); real_base(&s, 0, v3);
          if (v3 == BB_UI_KNOBS) { in.title = 0; in.subtitle = 0; }
          snprintf(nm, sizeof nm, "fx-%s", B[b]);
          shot(nm);
          if (b == 0) legend_shot("fx-legend");
          if (b == 1) help_shot(&s, "help-fx", 0);
      } }
    /* ---- SAVE (stopped) */
    s = nav(0, BB_NAV_SAVE, 0, 0); real_base(&s, 0, BB_UI_KNOBS); in.running = 0; pf.running = 0; real_leds(&s);
    knobs_head(&s, "SCENE 2"); shot("save"); legend_shot("save-legend");
    help_shot(&s, "help-save", 0);
    real_base(&s, 0, BB_UI_KNOBS); in.running = 0; pf.running = 0; real_leds(&s); knobs_head(&s, "SCENE 2");
    in.notice = "SAVED / FLASH VERIFIED"; shot("notice-saved"); in.notice = 0;
    /* ---- EVOLVE: LFO steps EVOLVE, EUCLID, RANDOM (the mode is the title) */
    { static const char *const EU[4] = {"HITS", "STEPS", "ROTATE", "ACCENTS"}, *const RA[4] = {"DENSITY", "ACCENT", "ANCHOR", "SEED"};
      static const char *const EUV[4] = {"5", "16", "0", "2"}, *const RAV[4] = {"70", "30", "100", "1"};
      for (uint8_t m = 0; m < BB_NAV_MODES; m++) {
          static const char *const NM[3] = {"evolve", "evolve-euclid", "evolve-random"};
          s = nav(0, BB_NAV_EVOLVE, 0, 0); s.gen_mode = m; real_base(&s, 0, BB_UI_KNOBS); pf.gen_mode = m; pf.gen_voice = 10; real_leds(&s);
          knobs_head(&s, bb_nav_mode_name(m));
          if (m == 1) knobs(EU, EUV); else if (m == 2) knobs(RA, RAV);
          shot(NM[m]);
          if (m == 0) { legend_shot("evolve-legend"); help_shot(&s, "help-evolve", 0); }
          if (m == 1) { in.notice = bb_tx(TX_VARIATION_APPLIED); shot("notice-variation-applied"); in.notice = 0; }
      } }
    /* ---- GLOBAL and its MIDI banks (bb_midi_ui_draw.inc) */
    s = nav(0, BB_NAV_GLOBAL, 0, 0); real_base(&s, 0, BB_UI_KNOBS); knobs_head(&s, "GLOBAL"); in.subtitle = in.legend;
    { static const char *const L[4] = {"BPM", "SWING", "A/B BAL", "TRANSPOS"}, *const V[4] = {"120", "56", "0", "0"}; knobs(L, V); }
    shot("global"); legend_shot("global-legend");
    help_shot(&s, "help-global", 0);
    s = nav(0, BB_NAV_GLOBAL, 0, BB_NAV_GLOBAL_MIDI); real_base(&s, 0, BB_UI_KNOBS); in.page_name = bb_tx(TX_GLOBAL_MIDI);
    in.title = bb_tx(TX_MIDI_CHANNELS); in.subtitle = in.legend; in.patch = bb_tx(TX_OMNI_OFF_LEAD_RESERVED);
    { static const char *const L[4] = {"KIT A CH", "KIT B CH", "BASS CH", "LEAD RSV"}, *const V[4] = {"10", "11", "1", "2"}; knobs(L, V); }
    shot("midi-channels");
    s = nav(0, BB_NAV_GLOBAL, 0, BB_NAV_GLOBAL_CLOCK); real_base(&s, 0, BB_UI_KNOBS); in.page_name = bb_tx(TX_GLOBAL_MIDI);
    in.title = "MIDI CLOCK"; in.subtitle = "USB OUT / STOP TO EDIT / SAVE"; in.patch = "USB OUT / NO USB THRU";
    { static const char *const L[4] = {"CLOCK IN", "FOLLOW", "TRANSPORT", "CLOCK OUT"}, *const V[4] = {"USB", "ON", "ON", "OFF"}; knobs(L, V); }
    shot("midi-clock");
    help_shot(&s, "help-midi", 0);
    /* ---- SONG (bb_arranger_draw.inc): the title line is bb_nav's legend */
    {
        static const char *const ROWS[] = {"01 INTRO", "02 VERSE", "03 HOOK", "04 VERSE"}, *const L[4] = {"ROW", "REPEATS", "BARS", "LOOP"},
                          *const V[4] = {"2", "2", "4", "ON"};
        static char leg[64];
        s = nav(0, BB_NAV_SONG, 0, BB_NAV_SONG_ROWS); real_base(&s, 0, BB_UI_ARRANGE); in.page_name = bb_tx(TX_SONG_HOME); in.legend = 0;
        (void)bb_nav_legend(&s, leg, sizeof leg, 1);
        if (text_w(&AF_S, leg) > 224) (void)bb_nav_legend(&s, leg, sizeof leg, 2);
        if (text_w(&AF_S, leg) > 224) (void)bb_nav_legend(&s, leg, sizeof leg, 3);
        in.pattern_name = "VERSE"; in.next_name = "HOOK"; in.subtitle = bb_tx(TX_EDITED_RAM_PROJECT_FILE_TOOLS);
        in.title = leg; in.items = 4;
        for (int i = 0; i < 4; i++) in.item[i] = ROWS[i];
        in.cursor = 1; in.playing_item = 1; in.queued_item = 2; knobs(L, V);
        shot("song");
        in.knob_turning = 2; in.events |= BB_EV_KNOB; in.ev_knob = 2; shot("song-turn"); in.knob_turning = BB_UI_NO;
        help_shot(&s, "help-song", 0);
        static const char *const AL[4] = {"TARGET", "GROUP", "PART", "BARS"}, *const AV[4] = {"KIT A", "1", "3", "4"};
        s = nav(0, BB_NAV_SONG, 0, BB_NAV_SONG_ADD); real_base(&s, 0, BB_UI_ARRANGE); in.page_name = "SONG / ADD"; in.legend = 0;
        (void)bb_nav_legend(&s, leg, sizeof leg, 1);
        in.pattern_name = "VERSE"; in.next_name = "";
        in.title = leg; in.subtitle = bb_tx(TX_A_B_DILLO_PATTERNS_ONLY);
        in.items = 2; in.item[0] = "Planet Rock"; in.item[1] = "Part 2"; in.cursor = 1; in.playing_item = BB_UI_NO; in.queued_item = BB_UI_NO;
        knobs(AL, AV); shot("song-add");
    }
    /* ---- RECORD: a pattern take from PLAY / STEPS (REC+SEQ), as bb_arranger_draw.inc draws it on this tree */
    {
        static const char *const L[4] = {"TARGET", "START", "ACCENT", "NEW HITS"};
        static char swing[48];
        snprintf(swing, sizeof swing, "%s%d", bb_tx(TX_K3_ACCENT_SWING), 58);
        const uint16_t sub[3] = {TX_COUNT_IN_RECORD_NEXT_BAR, TX_RECORDING_ORIGINAL_KEPT, TX_EDIT_KEEPS_AS_A_COPY};
        const char *nm[3] = {"record-count-in", "record-live", "record-review"};
        for (int p = 0; p < 3; p++) {
            s = nav(0, BB_NAV_PLAY, 0, 0); base(&s, 0, BB_UI_ARRANGE); real_leds(&s);
            in.page_name = "RECORD A"; in.instr = BB_UI_INST_A; in.instr_name = "KIT A";
            in.pattern_name = bb_tx(TX_PLAYING_PATTERN); in.next_name = p == 2 ? bb_tx(TX_KEEP_OR_DISCARD) : "";
            in.subtitle = bb_tx(sub[p]);
            in.title = p == 2 ? bb_tx(TX_EDIT_KEEP_REC_DISCARD) : bb_tx(TX_PADS_1_11_REC_FINISHES);
            in.small_items = 1; in.items = 3; in.item[0] = bb_tx(TX_KIT_A_NEAREST_STEP); in.item[1] = swing;
            in.item[2] = p == 2 ? bb_tx(TX_REC_EDIT_UNDOES_A_KEPT_TAKE) : bb_tx(TX_PAD_HITS_OVERDUB_NO_ERASE);
            in.cursor = BB_UI_NO; in.playing_item = BB_UI_NO; in.queued_item = BB_UI_NO;
            static const char *V[3][4] = {{"A", "NEXT", "OFF", "0"}, {"A", "NEXT", "OFF", "7"}, {"A", "NEXT", "OFF", "12"}};
            knobs(L, V[p]);
            shot(nm[p]);
        }
    }
    fclose(out);
    printf("manual captures: %u frames, %u lint failures\n", shots, failures); (void)known;
    return failures != 0;
}
