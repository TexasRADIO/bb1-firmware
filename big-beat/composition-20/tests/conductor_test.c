/* SPDX-License-Identifier: GPL-3.0-only
 * 2.1 Live Play P3b: the Rhythm Conductor (SPEC 2, acceptance 8.1.1-8.1.8 at the controller), on the REAL
 * firmware file bb_conductor_firmware.inc, the real audio owner (the oracle's compile line, ASan/UBSan), the real
 * bb_player labels, the real catalogue readers and the real bb_nav state. The firmware environment it needs is a few
 * lines below (notices captured, the P1 hook's SONG-armed answer, the focus row). usage: test-conductor [c1..c9]
 *  c1 coordinated section change on one tick, labels APPLIED          c6 refused part named, owner untouched; key beyond
 *  c2 mixed songs: HOLD keeps the other song; individual edits never     the song: nothing; LATE: retarget once, then
 *     re-link (64 random)                                               MISSED THE BAR
 *  c3 newest wins inside a bar; bar between; 400 random sequences     c7 mixes, mutes, levels, kits, sounds, effects
 *     never show a mixed ticket                                          byte-identical across 300 cues
 *  c4 press again cancels; the playing key cancels a waiting cue      c8 SONG armed: keys refused, no command
 *  c5 STOP cancels (notice); stopped cue applies at once; PLAY        c9 SONGS: CHOOSE always loads all three;
 *     starts on it                                                       ACID: Dillo only) in one commit
 *  RC1 (Duke #64 6592823816), the real owner under queue pressure:
 *  c11 cancel refused by a full 16-slot queue: retained and retried before the bar (nothing applies, CUE CANCELLED);
 *      still refused past the bar: the cue plays and the screen says TOO LATE TO CANCEL; the next press works
 *  c12 CHOOSE is all or nothing across section, tempo and swing: 15 queued (one free) refuses with nothing sent;
 *      13 queued (three free) loads the song with its sheet tempo and swing; the refused CHOOSE then works
 *  c18 a next-bar race that rejects the first owner publication is refreshed and accepted without asking the player
 *      to press the section again */
#define main oracle_main
#include ORACLE_C
#undef main
#include "bb_player.h"
#include "bb_factory_recipe.h"
#include "bb_fault_notice.h"
#include "bb_nav.h"
/* ---------------------------------------------------------------- the firmware environment of the conductor */
#define bb_sound audio
static bb_player bb_drum_library[2], bb_library[2];
static bb_nav_state bb_nav;
static uint8_t bb_nav_row;
static int bb_dirty;
static char said[64];
static unsigned says;
static void bb_say(const char *t) { snprintf(said, sizeof said, "%s", t); says++; }
static int bb_send(bb_command_type type, unsigned value)
{
    bb_command c = {0}; c.type = type; c.value = (uint16_t)value; return bb_command_push(&audio.commands, &c);
}
#define BB_TX(x) #x
enum { BB_P1_SONG_ARMED = 11 };
static int song_armed;
static int bb_nav_p1(unsigned op, unsigned a, int b) { (void)a; (void)b; return op == BB_P1_SONG_ARMED ? song_armed : 0; }
static void bb_nav_project(void) {}
static unsigned bb_cd_test_refuse_once;
static int bb_cd_test_section_submit(bb_audio *a,const bb_section_bundle *b,uint32_t *ticket) {
    if(bb_cd_test_refuse_once){
        /* Model the audio owner crossing the bar edge after the conductor's
         * status snapshot but before the bundle is published. */
        bb_cd_test_refuse_once=0;
        a->core.ticks++;
        return 0;
    }
    return bb_audio_section_submit(a,b,ticket);
}
#define bb_audio_section_submit bb_cd_test_section_submit
#include "bb_conductor_firmware.inc"
#undef bb_audio_section_submit
/* ---------------------------------------------------------------- harness */
static unsigned checks;
static uint64_t fnv(uint64_t h, const void *p, size_t n) { const uint8_t *b = p; while (n--) h = (h ^ *b++) * 1099511628211ull; return h; }
#define CK(x) do { checks++; if (!(x)) { fprintf(stderr, "FAIL line %u %s (said \"%s\")\n", __LINE__, #x, said); if (!getenv("C15_DEBUG")) exit(1); } } while (0)
static uint32_t rng = 0x2545f491u;
static uint32_t rnd(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }
static unsigned songs[64], nsongs;              /* SONGS banks whose sections all prepare on both layers and Dillo */
static unsigned sections_of(unsigned bank)
{
    bb_browser b; bb_browser_init(&b, &bb_factory_catalogue); bb_browser_group(&b, BB_BROWSER_SONGS);
    bb_browser_move_bank(&b, (int)bank - (int)b.bank_index); return (unsigned)bb_browser_part_count(&b);
}
static void players_sync(void) { for (unsigned q = 0; q < 3; q++) (void)bb_player_sync(bb_cd_player(q), &audio); }
static void drain(void) { CK(render()); players_sync(); }
static void scan(void) { bb_cd_poll(); drain(); bb_cd_poll(); }
static void world(void)
{
    setup(0); drain();
    bb_browser b; CK(bb_browser_init(&b, &bb_factory_catalogue)); CK(bb_browser_group(&b, BB_BROWSER_SONGS));
    CK(bb_player_init_drum(&bb_drum_library[0], &b, 0)); CK(bb_player_init_drum(&bb_drum_library[1], &b, 1));
    CK(bb_player_init(&bb_library[0], &b, 0));
    memset(&bb_cd, 0, sizeof bb_cd); bb_cd_poll(); CK(bb_cd.list.ready && bb_cd.take == 7);
    bb_nav_init(&bb_nav, BB_NAV_STUDIO, 600); bb_nav.bank = BB_NAV_HOME_SONGS; bb_nav_row = 0; song_armed = 0;
}
static void play(void) { push((bb_command){.type = BB_CMD_TRANSPORT_RESTART}); drain(); CK(audio.core.running); }
static void stop(void) { push((bb_command){.type = BB_CMD_TRANSPORT}); }
static void run_to(uint32_t t) { unsigned n = 0; while (audio.core.ticks < t) { scan(); CK(++n < 100000); } }
static unsigned playing_part(unsigned q) { const bb_player *p = bb_cd_player(q); return p->playing.ticket ? p->playing.choice.part_index : 255u; }
static void key(unsigned focus, unsigned n) { bb_nav_row = (uint8_t)focus; bb_nav.page = BB_NAV_STUDIO; bb_nav.bank = BB_NAV_HOME_CONDUCT; bb_cd_key(n); }
/* SONGS: choose song k. A non-7 mask is test setup for mixed-song HOLD proofs; the public chooser always uses 7. */
static void choose(unsigned k, unsigned take)
{
    bb_nav.page = BB_NAV_STUDIO; bb_nav.bank = BB_NAV_HOME_SONGS;
    CK(bb_browser_group(&bb_cd.list, BB_BROWSER_SONGS)); bb_cd.coll = 0;
    CK(bb_browser_move_bank(&bb_cd.list, (int)songs[k] - (int)bb_cd.list.bank_index)); bb_cd.take = (uint8_t)take;
    unsigned before = audio.commands.count;
    if (take == 7u) bb_cd_choose();
    else {
        bb_factory_choice ch[3];
        for (unsigned q = 0; q < 3; q++) if ((take >> q) & 1u) {
            bb_browser b = bb_cd_player(q)->view;
            CK(bb_cd_at(&b, bb_cd.list.group, bb_cd.list.bank_index, -1, &ch[q]));
        }
        bb_cd.key = 0; bb_cd.retried = 0; bb_cd.chose = 1;
        CK(bb_cd_submit(take, ch, 0));
        bb_nav.bank = BB_NAV_HOME_CONDUCT;
    }
    CK(bb_cd.ticket && bb_nav.bank == BB_NAV_HOME_CONDUCT && audio.commands.count >= before + 1);
    uint32_t t = bb_cd.ticket; unsigned n = 0;
    while (bb_cd.ticket) { scan(); CK(++n < 4000); }
    for (unsigned q = 0; q < 3; q++) if ((take >> q) & 1u) CK(bb_cd_player(q)->playing.ticket == t);
    /* CHOOSE loads the song's initial section (not always the first); start these proofs from section 1 */
    unsigned f = (unsigned)__builtin_ctz(take);
    if (!audio.core.running && playing_part(f) != 0) {
        key(f, 0); CK(bb_cd.ticket); n = 0; while (bb_cd.ticket) { scan(); CK(++n < 4000); }
        for (unsigned q = 0; q < 3; q++) if ((take >> q) & 1u) CK(playing_part(q) == 0);
    }
}
static unsigned playing_bank(unsigned q) { return bb_cd_player(q)->playing.choice.bank_index; }
static uint32_t applied(unsigned q) { return q < 2 ? audio.rich.status[q].applied : audio.patterns.status[0].applied; }
static void collect(void)
{
    CK(bb_factory_init());
    bb_browser b; CK(bb_browser_init(&b, &bb_factory_catalogue)); CK(bb_browser_group(&b, BB_BROWSER_SONGS));
    for (unsigned bank = 0; bank < bb_browser_bank_count(&b) && nsongs < 64; bank++) {
        unsigned n = sections_of(bank), ok = n >= 6;
        for (unsigned s = 0; ok && s < n; s++) {
            bb_browser c = b; bb_factory_choice ch; bb_phrase_index ph; bb_pattern_data d;
            ok = bb_cd_at(&c, BB_BROWSER_SONGS, bank, (int)s, &ch) && bb_factory_prepare_rich_drum(&bb_factory_catalogue, &ch, &ph) == BB_FDRUM_OK &&
                 bb_audio_phrase_preflight_playable(&ph, BB_RICH_808_VOICES) && !(ph.voices & ~BB_RICH_808_VOICES) &&
                 ph.phrase.lattice == BB_FDL_SIXTEENTHS && bb_factory_prepare_synth_data(&bb_factory_catalogue, &ch, 0, &d) == BB_FACTORY_PATTERN_OK;
        }
        if (ok) songs[nsongs++] = bank;
    }
    CK(nsongs >= 3);
}
/* ---------------------------------------------------------------- c1 coordinated change */
static void c1(void)
{
    world(); choose(0, 7); play(); run_to(5);
    unsigned before = audio.commands.count; key(0, 3);
    CK(bb_cd.ticket && bb_cd.moved == 7 && audio.commands.count == before + 1);
    CK(audio.commands.items[(audio.commands.write + BB_COMMAND_CAPACITY - 1u) % BB_COMMAND_CAPACITY].type == BB_CMD_SECTION_PATTERN);
    uint32_t t = bb_cd.ticket;
    for (unsigned q = 0; q < 3; q++) CK(bb_player_pending(bb_cd_player(q)));   /* QUEUED labels: the key pulses */
    while (audio.core.ticks < 24) {
        scan(); unsigned n = 0; for (unsigned q = 0; q < 3; q++) n += applied(q) == t;
        CK(n == 0 || n == 3); if (!n) CK(audio.core.ticks <= 16);
    }
    CK(audio.section.status.applied_tick == 16 && !bb_cd.ticket);
    for (unsigned q = 0; q < 3; q++) CK(playing_part(q) == 3 && bb_cd_player(q)->playing.ticket == t && !bb_player_pending(bb_cd_player(q)));
    fprintf(stderr, "c1 coordinated: one command, A/B/DILLO applied together on tick 16, labels APPLIED\n");
}
/* ---------------------------------------------------------------- c2 mixed songs, HOLD, never re-linked */
static void c2(void)
{
    world(); choose(0, 1); choose(1, 6);                  /* A on song 0; B and Dillo on song 1 */
    play(); run_to(5);
    uint64_t a_hash = fnv(1469598103934665603ull, &audio.rich.active[0], sizeof audio.rich.active[0]);
    uint32_t a_ticket = bb_cd_player(0)->playing.ticket;
    key(1, 4); CK(bb_cd.moved == 6 && bb_cd.stage.keep == 1);   /* focus B: B and Dillo move, A HOLDs */
    run_to(20);
    CK(playing_part(1) == 4 && playing_part(2) == 4 && playing_bank(1) == songs[1] && playing_bank(2) == songs[1]);
    CK(bb_cd_player(0)->playing.ticket == a_ticket && playing_bank(0) == songs[0]);
    CK(a_hash == fnv(1469598103934665603ull, &audio.rich.active[0], sizeof audio.rich.active[0]));
    /* focus A: only A moves (B and Dillo are on another song) */
    key(0, 2); CK(bb_cd.moved == 1 && bb_cd.stage.keep == 6); run_to(40);
    CK(playing_part(0) == 2 && playing_part(1) == 4 && playing_part(2) == 4);
    /* 64 random individual edits (PATTERN / PLAY section keys) on one part: the others' references never change */
    for (unsigned k = 0; k < 64; k++) {
        unsigned q = rnd() % 3, s = rnd() % 6;
        bb_factory_choice ref[3]; for (unsigned r = 0; r < 3; r++) { CK(bb_cd_ref(r)); ref[r] = *bb_cd_ref(r); }
        bb_player *p = bb_cd_player(q); bb_browser v = p->view; bb_factory_choice ch;   /* as PATTERN / PLAY 6-7 do */
        if (bb_cd_at(&p->view, BB_BROWSER_SONGS, rnd() % 2 ? songs[0] : songs[1], (int)s, &ch) &&
            bb_player_part_key(p, &audio, s % BB_BROWSER_PART_KEYS) == BB_FACTORY_PATTERN_OK) {} else p->view = v;
        for (unsigned i = 0; i < (rnd() % 60); i++) scan();
        for (unsigned r = 0; r < 3; r++) if (r != q) {   /* the other parts' references (playing or their own request) */
            const bb_factory_choice *now = bb_cd_ref(r);
            CK(now && now->group == ref[r].group && now->bank_index == ref[r].bank_index && now->part_index == ref[r].part_index);
        }
    }
    fprintf(stderr, "c2 mixed songs: HOLD keeps the other song and its label; 64 individual edits never re-link\n");
}
/* ---------------------------------------------------------------- c3 newest wins, random sequences */
static void c3(void)
{
    world(); choose(0, 7); play(); run_to(3);
    key(0, 2); uint32_t t2 = bb_cd.ticket; key(0, 5);       /* inside one bar: 2 cancelled, 5 queued when free */
    unsigned seen5 = 0;
    while (audio.core.ticks < 40) { scan(); CK(applied(0) != t2); if (playing_part(0) == 5) seen5 = 1; }
    CK(seen5 && playing_part(1) == 5 && playing_part(2) == 5);
    /* bar between: 2 at bar n+1, then 5 at the following */
    world(); choose(0, 7); play(); run_to(5); key(0, 2); run_to(17); CK(playing_part(0) == 2); key(0, 5); run_to(33);
    CK(playing_part(0) == 5 && audio.section.status.applied_tick == 32);
    /* 400 random sequences: keys, repeats, STOP, PLAY, drains; on every drain the parts carrying the newest applied
     * section ticket are all three (one song) */
    world(); choose(0, 7);
    for (unsigned k = 0; k < 400; k++) {
        unsigned op = rnd() % 10;
        if (op < 6) key(rnd() % 3, rnd() % 6);
        else if (op == 6) stop();
        else if (op == 7 && !audio.core.running) play();
        unsigned d = rnd() % 30;
        for (unsigned i = 0; i < d; i++) {
            scan(); uint32_t s = audio.section.status.state.applied; unsigned n = 0;
            for (unsigned q = 0; q < 3; q++) n += applied(q) == s;
            CK(!s || n == 3);
        }
    }
    fprintf(stderr, "c3 replace: newest wins inside a bar; bar between 16/32; 400 random sequences, never a mixed ticket\n");
}
/* ---------------------------------------------------------------- c4 cancel */
static void c4(void)
{
    world(); choose(0, 7); play(); run_to(3);
    key(0, 3); key(0, 3);                                   /* the pulsing key again: cancel */
    run_to(40); for (unsigned q = 0; q < 3; q++) CK(playing_part(q) == 0 && !bb_player_pending(bb_cd_player(q)));
    CK(!bb_cd.ticket && !bb_cd.want);
    { unsigned c = audio.commands.count; uint32_t nt = audio.patterns.next_ticket; key(0, 0);   /* idle: the bright key stays */
      CK(!bb_cd.ticket && audio.commands.count == c && audio.patterns.next_ticket == nt); }
    run_to(50); key(0, 4); key(0, 0);                       /* the bright (playing) key: cancel the waiting cue */
    run_to(90); for (unsigned q = 0; q < 3; q++) CK(playing_part(q) == 0 && !bb_player_pending(bb_cd_player(q)));
    fprintf(stderr, "c4 cancel: pulsing key again and the playing key cancel; labels back, nothing applied\n");
}
/* ---------------------------------------------------------------- c5 STOP, stopped */
static void c5(void)
{
    world(); choose(0, 7); play(); run_to(3);
    key(0, 3); stop(); for (unsigned i = 0; i < 40; i++) scan();
    CK(!audio.core.running && !bb_cd.ticket && !strcmp(said, "CANCELLED BY STOP"));
    for (unsigned q = 0; q < 3; q++) CK(playing_part(q) == 0 && !bb_player_pending(bb_cd_player(q)));
    key(0, 3); for (unsigned i = 0; i < 4; i++) scan();     /* stopped: at once */
    for (unsigned q = 0; q < 3; q++) CK(playing_part(q) == 3);
    play(); for (unsigned i = 0; i < 4; i++) scan(); CK(audio.core.running && playing_part(0) == 3);
    fprintf(stderr, "c5 STOP: cancelled with its notice; stopped cue applies at once; PLAY starts on it\n");
}
/* ---------------------------------------------------------------- c6 refused, absent, LATE */
static void c6(void)
{
    world(); choose(0, 7); play(); run_to(3);
    audio.drum_patterns.family[1] = 1;                      /* drum B is not a rich layer now: B must refuse */
    unsigned c = audio.commands.count; uint32_t nt = audio.patterns.next_ticket; key(0, 3);
    CK(!bb_cd.ticket && audio.commands.count == c && audio.patterns.next_ticket == nt && !strcmp(said, "CUE REFUSED: B NEEDS A RICH KIT"));
    for (unsigned q = 0; q < 3; q++) CK(!bb_player_pending(bb_cd_player(q)));
    audio.drum_patterns.family[1] = 0;
    c = audio.commands.count; key(0, sections_of(songs[0]) + 2);   /* beyond the song: nothing */
    CK(!bb_cd.ticket && audio.commands.count == c);
    /* LATE: the owner cancels a cue it met after its bar; the conductor retargets the following bar once */
    key(0, 2); CK(bb_cd.ticket); for (unsigned i = 0; i < 2; i++) drain();
    CK(audio.section.phase == BB_SECTION_PENDING);
    bb_audio_section_cancel_owned(&audio, BB_SECTION_ERROR_LATE); bb_cd_poll();
    CK(!bb_cd.ticket && bb_cd.want == 3 && bb_cd.retried); bb_cd_poll(); CK(bb_cd.ticket);
    for (unsigned i = 0; i < 2; i++) drain();
    bb_audio_section_cancel_owned(&audio, BB_SECTION_ERROR_LATE);
    bb_cd_poll();
    CK(!bb_cd.ticket && !bb_cd.want && !strcmp(said, "MISSED THE BAR"));
    for (unsigned q = 0; q < 3; q++) CK(playing_part(q) == 0 && !bb_player_pending(bb_cd_player(q)));
    fprintf(stderr, "c6 refused / absent / late: B named, owner untouched; beyond the song nothing; LATE retarget once then MISSED THE BAR\n");
}
/* ---------------------------------------------------------------- c7 mixes untouched */
static void c7(void)
{
    world(); choose(0, 7); play();
    for (unsigned k = 0; k < 300; k++) {
        for (unsigned m = 0; m < 2; m++) { audio.core.drums[m].muted = (uint8_t)(rnd() & 1); audio.core.drums[m].level = (uint8_t)(rnd() & 127); }
        audio.core.synths[0].muted = (uint8_t)(rnd() & 1);
        bb_machine d0 = audio.core.drums[0], d1 = audio.core.drums[1];
        uint8_t sm = audio.core.synths[0].muted; bb_dillo_patch dp = audio.core.dillo;
        uint16_t fx[sizeof audio.core.effects.value / sizeof audio.core.effects.value[0]]; memcpy(fx, audio.core.effects.value, sizeof fx);
        key(rnd() % 3, rnd() % 6); for (unsigned i = 0; i < 30; i++) scan();
        CK(d0.muted == audio.core.drums[0].muted && d0.level == audio.core.drums[0].level && d0.kit == audio.core.drums[0].kit);
        CK(d1.muted == audio.core.drums[1].muted && d1.level == audio.core.drums[1].level && d1.kit == audio.core.drums[1].kit);
        CK(!memcmp(&d0.sound, &audio.core.drums[0].sound, sizeof d0.sound) && !memcmp(&d1.sound, &audio.core.drums[1].sound, sizeof d1.sound));
        CK(sm == audio.core.synths[0].muted && !memcmp(&dp, &audio.core.dillo, sizeof dp) && !memcmp(fx, audio.core.effects.value, sizeof fx));
    }
    fprintf(stderr, "c7 mix: 300 cues, mutes / levels / kits / drum sounds / Dillo patch / effects byte-identical\n");
}
/* ---------------------------------------------------------------- c8 SONG exclusivity */
static void c8(void)
{
    world(); choose(0, 7); play(); run_to(3); song_armed = 1;
    unsigned c = audio.commands.count; key(0, 3);
    CK(!bb_cd.ticket && audio.commands.count == c && !strcmp(said, "SONG PLAYING"));
    bb_nav_facts f; memset(&f, 0, sizeof f); f.section_count = 6; bb_nav.bank = BB_NAV_HOME_CONDUCT; bb_cd_facts(&f);
    CK(f.section_count == 0);                               /* the section keys are dark, so inert (bb_nav, P1) */
    fprintf(stderr, "c8 SONG armed: CONDUCT keys off and refused, no command\n");
}
/* ---------------------------------------------------------------- c9 SONGS CHOOSE */
static void c9(void)
{
    world(); choose(0, 7);
    for (unsigned q = 0; q < 3; q++) CK(playing_bank(q) == songs[0]);
    play(); run_to(5);
    bb_cd.take = 7; CK(bb_browser_move_bank(&bb_cd.list, (int)songs[1] - (int)bb_cd.list.bank_index));
    bb_nav.bank = BB_NAV_HOME_SONGS; bb_cd_choose(); uint32_t t = bb_cd.ticket; CK(t && bb_cd.stage.keep == 0);
    while (audio.core.ticks < 24) { scan(); unsigned n = 0; for (unsigned q = 0; q < 3; q++) n += applied(q) == t; CK(n == 0 || n == 3); }
    for (unsigned q = 0; q < 3; q++) CK(playing_bank(q) == songs[1]);
    /* A stale legacy mask may remain in RAM, but a new song still replaces all three. */
    bb_cd.take = 5; CK(bb_browser_move_bank(&bb_cd.list, (int)songs[2] - (int)bb_cd.list.bank_index));
    bb_nav.bank = BB_NAV_HOME_SONGS; bb_cd_choose(); CK(bb_cd.stage.keep == 0 && bb_cd.moved == 7); run_to(60);
    for (unsigned q = 0; q < 3; q++) CK(playing_bank(q) == songs[2]);
    bb_cd.take = 4; CK(bb_browser_move_bank(&bb_cd.list, (int)songs[3] - (int)bb_cd.list.bank_index));
    bb_nav.bank = BB_NAV_HOME_SONGS; bb_cd_choose(); CK(bb_cd.stage.keep == 0 && bb_cd.moved == 7); run_to(90);
    for (unsigned q = 0; q < 3; q++) CK(playing_bank(q) == songs[3]);
    fprintf(stderr, "c9 SONGS: every song replaces A, B and Dillo atomically; stale partial masks cannot hold one behind\n");
}
/* ---------------------------------------------------------------- c10 every written section reachable (Duke #64) */
static unsigned bank_named(const char *name)
{
    bb_browser b; bb_browser_init(&b, &bb_factory_catalogue); bb_browser_group(&b, BB_BROWSER_SONGS);
    for (unsigned k = 0; k < bb_browser_bank_count(&b); k++) {
        bb_browser c = b; char t[48]; c.bank_page = (uint16_t)(k / BB_BROWSER_BANK_KEYS);
        if (bb_browser_bank_title(&c, k % BB_BROWSER_BANK_KEYS, t, sizeof t) && !strcmp(t, name)) return k;
    }
    return 9999;
}
static void c10(void)
{
    /* RC2 (Joe 10:32): on PERFORM the white keys 1-16 are the sections, OCT pages by 16 (CR-8000 43 = 3 pages, TR-66 54
     * = 4); keys past the song's last section are dark and inert; black B1-B3 are the instruments. The PATTERN page keeps
     * its black-key sections, 11 a page, and RC1's page notices (34-44, 45-55). */
    static const struct { const char *name; unsigned sections, pages; } shelf[] = {{"Roland CR-8000", 43, 3}, {"Roland TR-66", 54, 4}};
    static const char *const perform_text[4] = {"SECTIONS 1-16", "SECTIONS 17-32", "SECTIONS 33-48", "SECTIONS 49-64"};
    for (unsigned i = 0; i < 2; i++) {
        unsigned bank = bank_named(shelf[i].name), n = shelf[i].sections, last = n - 1;
        CK(bank != 9999 && sections_of(bank) == n);
        bb_nav_state s; bb_nav_init(&s, BB_NAV_STUDIO, 600); s.bank = BB_NAV_HOME_CONDUCT;
        bb_nav_facts f; memset(&f, 0, sizeof f);
        f.kit_slot = f.kit_slot_queued = f.section_queued = f.section = f.list_cursor = f.list_playing = f.list_queued = BB_NAV_NONE_IDX;
        f.section_count = (uint8_t)n;
        for (unsigned p = 0; p <= shelf[i].pages; p++) {      /* one OCT+ more than there are pages: no page beyond */
            bb_nav_in in; bb_nav_out o; memset(&in, 0, sizeof in); in.facts = f; in.now_ms = 1000 + 20 * p;
            in.pressed = in.held = 1u << BB_NAV_B_OCTUP; bb_nav_input(&s, &in, &o);
            if (p + 1u < shelf[i].pages) CK(!strcmp(bb_nav_message_text(o.message), perform_text[p + 1u]));
            memset(&in, 0, sizeof in); in.facts = f; in.now_ms = 1010 + 20 * p; bb_nav_input(&s, &in, &o);
        }
        uint8_t arg, k = (uint8_t)(last % BB_NAV_CONDUCT_KEYS);   /* white key */
        CK(s.sec_page == shelf[i].pages - 1u && last / BB_NAV_CONDUCT_KEYS == shelf[i].pages - 1u);
        CK(bb_nav_key_role(&s, k, 0, &arg) == BB_NAV_K_CONDUCT && arg == last);
        CK(bb_nav_key_role(&s, k, 1, &arg) == BB_NAV_K_SECTION && arg == last);          /* REC+white: the focused part */
        for (unsigned b = 0; b < 3; b++) CK(bb_nav_key_role(&s, (uint8_t)BB_NAV_BLACK(1 + b), 0, &arg) == BB_NAV_K_INSTR_MUTE && arg == b);
        for (unsigned b = 3; b < 11; b++) CK(bb_nav_key_role(&s, (uint8_t)BB_NAV_BLACK(1 + b), 0, &arg) == BB_NAV_K_NONE);
        bb_nav_led_out l; bb_nav_leds(&s, &f, 0, &l); CK(l.key[k] == BB_NAV_LED_DIM);
        for (unsigned w = k + 1u; w < 16u; w++) {
            bb_nav_in in; bb_nav_out o;
            CK(l.key[w] == BB_NAV_LED_DARK);
            memset(&in, 0, sizeof in); in.facts = f; in.keys = in.keys_held = 1u << w; in.now_ms = 5000 + w; bb_nav_input(&s, &in, &o);
            CK(o.kev_count == 0);
            memset(&in, 0, sizeof in); in.facts = f; in.now_ms = 5100 + w; bb_nav_input(&s, &in, &o);
        }
        /* PATTERN: REC+OCT pages its black section keys, 11 a page; pages 4-5 name their own sections (RC1) */
        {
            static const char *const pat_text[2] = {"SECTIONS 34-44", "SECTIONS 45-55"};
            bb_nav_state t; bb_nav_out o; bb_nav_init(&t, BB_NAV_STUDIO, 600); memset(&o, 0, sizeof o); bb_nav_goto(&t, BB_NAV_PATTERN, &o);
            for (unsigned p = 0; p < last / BB_NAV_SECTION_KEYS; p++) {
                bb_nav_in in; memset(&in, 0, sizeof in); in.facts = f; in.now_ms = 7000 + 40 * p;
                in.pressed = 1u << BB_NAV_B_OCTUP; in.held = in.pressed | 1u << BB_NAV_B_REC; bb_nav_input(&t, &in, &o);
                if (p + 1u >= 3u) CK(!strcmp(bb_nav_message_text(o.message), pat_text[p + 1u - 3u]));
                memset(&in, 0, sizeof in); in.facts = f; in.now_ms = 7020 + 40 * p; bb_nav_input(&t, &in, &o);
            }
            CK(t.sec_page == last / BB_NAV_SECTION_KEYS);
        }
        /* the conductor plays that section on every part (stopped: at once) */
        world();
        bb_nav.page = BB_NAV_STUDIO; bb_nav.bank = BB_NAV_HOME_SONGS; CK(bb_browser_move_bank(&bb_cd.list, (int)bank - (int)bb_cd.list.bank_index));
        bb_cd.take = 7; bb_cd_choose(); for (unsigned j = 0; j < 8 && bb_cd.ticket; j++) scan(); CK(!bb_cd.ticket);
        key(0, last); CK(bb_cd.ticket); for (unsigned j = 0; j < 8 && bb_cd.ticket; j++) scan();
        for (unsigned q = 0; q < 3; q++) CK(playing_bank(q) == bank && playing_part(q) == last);
    }
    fprintf(stderr, "c10 reach: PERFORM white keys, 16 a page (CR-8000 43 on page 3, TR-66 54 on page 4, SECTIONS 17-32 ...); B1-B3 = A/B/DILLO; past the end dark and inert; PATTERN keeps 11 a page (34-44, 45-55)\n");
}
/* ---------------------------------------------------------------- c11 cancel under a full queue (Duke 6592823816 item 1) */
static void fill_queue(void)
{
    bb_command c = {0}; c.type = BB_CMD_TEMPO; c.value = audio.core.bpm;   /* neutral: the tempo it already has */
    while (audio.commands.count < BB_COMMAND_CAPACITY) CK(bb_command_push(&audio.commands, &c));
}
static void c11(void)
{
    /* Duke's case: queue at tick 3, drain to PENDING, fill the queue, press the pulsing key again */
    world(); choose(0, 7); play(); run_to(3);
    key(0, 3); uint32_t t = bb_cd.ticket; CK(t);
    for (unsigned i = 0; i < 2; i++) drain();
    CK(audio.section.phase == BB_SECTION_PENDING);
    fill_queue(); key(0, 3);                                /* the cancel push is refused */
    CK(audio.commands.count == BB_COMMAND_CAPACITY && bb_cd.ticket == t);
    run_to(40);
    for (unsigned q = 0; q < 3; q++) CK(applied(q) != t && playing_part(q) == 0 && !bb_player_pending(bb_cd_player(q)));
    CK(!bb_cd.ticket && !bb_cd.want && !strcmp(said, "CUE CANCELLED"));
    /* still refused at the bar: the cue plays and the screen says so */
    world(); choose(0, 7); play(); run_to(3);
    key(0, 3); t = bb_cd.ticket; CK(t);
    for (unsigned i = 0; i < 2; i++) drain();
    CK(audio.section.phase == BB_SECTION_PENDING);
    fill_queue(); key(0, 3);
    unsigned n = 0;
    while (audio.core.ticks < 20) { bb_cd_poll(); CK(render()); players_sync(); fill_queue(); bb_cd_poll(); CK(++n < 1000); }
    for (unsigned i = 0; i < 8; i++) scan();
    for (unsigned q = 0; q < 3; q++) CK(applied(q) == t && playing_part(q) == 3 && !bb_player_pending(bb_cd_player(q)));
    CK(!bb_cd.ticket && !strcmp(said, "TOO LATE TO CANCEL"));
    key(0, 5); CK(bb_cd.ticket); run_to(40);                /* the next press is a normal cue */
    for (unsigned q = 0; q < 3; q++) CK(playing_part(q) == 5);
    fprintf(stderr, "c11 cancel, full queue: retried and cancelled before the bar; past the bar TOO LATE TO CANCEL; next press normal\n");
}
/* ---------------------------------------------------------------- c12 CHOOSE all or nothing (Duke 6592823816 item 2) */
static void c12(void)
{
    unsigned k = 0, bpm = 0, sw = 0;
    world();                                                /* a song with a sheet tempo: 118 as Duke's, else any but 57 */
    for (unsigned pass = 0; pass < 2 && k == 0; pass++)
        for (unsigned j = 1; j < nsongs && k == 0; j++) {
            bb_browser b = bb_cd.list; bb_factory_choice ch;
            if (bb_cd_at(&b, BB_BROWSER_SONGS, songs[j], -1, &ch) && bb_factory_read_song_tempo(&bb_factory_catalogue, &ch, &bpm, &sw) &&
                (pass ? bpm != 57 : bpm == 118)) k = j;
        }
    CK(k);
    for (unsigned queued = 13; queued <= 15; queued += 2) {
        world(); choose(0, 7); play(); run_to(5);
        push((bb_command){.type = BB_CMD_TEMPO, .value = 57}); drain(); CK(audio.core.bpm == 57);
        for (unsigned i = 0; i < queued; i++) push((bb_command){.type = BB_CMD_TEMPO, .value = 57});
        CK(audio.commands.count == queued);
        bb_nav.page = BB_NAV_STUDIO; bb_nav.bank = BB_NAV_HOME_SONGS; bb_cd.take = 7;
        CK(bb_browser_move_bank(&bb_cd.list, (int)songs[k] - (int)bb_cd.list.bank_index));
        uint32_t nt = audio.patterns.next_ticket; bb_cd_choose();
        if (queued == 15) {                                 /* one free slot: nothing is sent, nothing moves */
            CK(!bb_cd.ticket && audio.commands.count == 15 && audio.patterns.next_ticket == nt && bb_nav.bank == BB_NAV_HOME_SONGS);
            CK(!strcmp(said, "TX_BUSY_RETRY"));
            for (unsigned q = 0; q < 3; q++) CK(!bb_player_pending(bb_cd_player(q)) && playing_bank(q) == songs[0]);
            run_to(40); CK(audio.core.bpm == 57);
            bb_nav.bank = BB_NAV_HOME_SONGS; bb_cd_choose();   /* the queue has drained: CHOOSE again works */
        }
        CK(bb_cd.ticket && bb_nav.bank == BB_NAV_HOME_CONDUCT);
        unsigned n = 0; while (bb_cd.ticket) { scan(); CK(++n < 4000); }
        bb_core want = audio.core; CK(bb_set_swing_x2(&want, sw));
        for (unsigned q = 0; q < 3; q++) CK(playing_bank(q) == songs[k]);
        CK(audio.core.bpm == bpm && audio.core.swing == want.swing);
    }
    fprintf(stderr, "c12 CHOOSE: 15 queued refuses with nothing sent; 13 queued and the retry load the song at its sheet tempo %u and swing\n", bpm);
}
/* ---------------------------------------------------------------- c13 PLAY on the song list (RC2, Joe 10:26) */
static void c13(void)
{
    /* stopped, HOME/SONGS, the cursor on a song: PLAY = one section commit + its tempo and swing + start, then PERFORM */
    unsigned k = 2, bpm = 0, sw = 0;
    {   /* the PLAY button through bb_nav: the song list asks for the song (2), PATTERN for its pattern (1), PERFORM plain */
        static const struct { uint8_t page, bank, want; } at[3] = {{BB_NAV_STUDIO, BB_NAV_HOME_SONGS, 2}, {BB_NAV_STUDIO, BB_NAV_HOME_CONDUCT, 0},
                                                                    {BB_NAV_PATTERN, 0, 1}};
        for (unsigned i = 0; i < 3; i++) {
            bb_nav_state s; bb_nav_in in; bb_nav_out o; bb_nav_init(&s, BB_NAV_STUDIO, 600);
            if (at[i].page == BB_NAV_PATTERN) { memset(&o, 0, sizeof o); bb_nav_goto(&s, BB_NAV_PATTERN, &o); } else s.bank = at[i].bank;
            memset(&in, 0, sizeof in); in.now_ms = 3000; in.pressed = in.held = 1u << BB_NAV_B_PLAY; bb_nav_input(&s, &in, &o);
            CK((o.actions & BB_NAV_A_TRANSPORT) && o.pattern_play == at[i].want);
        }
    }
    for (unsigned queued = 0; queued <= 13; queued += 13) {
        world(); bb_nav.page = BB_NAV_STUDIO; bb_nav.bank = BB_NAV_HOME_SONGS; bb_cd.take = 7;
        CK(bb_browser_move_bank(&bb_cd.list, (int)songs[k] - (int)bb_cd.list.bank_index));
        bb_browser b = bb_cd.list; bb_factory_choice ch;
        int tempo = bb_cd_at(&b, BB_BROWSER_SONGS, songs[k], -1, &ch) && bb_factory_read_song_tempo(&bb_factory_catalogue, &ch, &bpm, &sw);
        for (unsigned i = 0; i < queued; i++) push((bb_command){.type = BB_CMD_TEMPO, .value = audio.core.bpm});
        unsigned before = audio.commands.count; uint32_t nt = audio.patterns.next_ticket;
        bb_cd.welcome=1;
        CK(!audio.core.running && bb_cd_play() == 1);
        CK(bb_cd.welcome == !!queued);
        if (queued) {                                           /* 3 free, 4 needed (with the sheet tempo): nothing */
            CK(tempo && !bb_cd.ticket && audio.commands.count == before && audio.patterns.next_ticket == nt);
            CK(!strcmp(said, "TX_BUSY_RETRY") && bb_nav.bank == BB_NAV_HOME_SONGS);
            for (unsigned i = 0; i < 40; i++) scan();
            CK(!audio.core.running);
            for (unsigned q = 0; q < 3; q++) CK(!bb_player_pending(bb_cd_player(q)) && !bb_cd_player(q)->playing.ticket);
            continue;
        }
        CK(bb_cd.ticket && audio.patterns.next_ticket == nt + 1u && bb_nav.bank == BB_NAV_HOME_CONDUCT);
        unsigned sections = 0;
        for (unsigned i = 0; i < audio.commands.count; i++) {
            const bb_command *c = &audio.commands.items[(audio.commands.read + i) % BB_COMMAND_CAPACITY];
            sections += c->type == BB_CMD_SECTION_PATTERN;
        }
        CK(sections == 1 && audio.commands.count == before + (tempo ? 4u : 2u));
        uint32_t t = bb_cd.ticket; unsigned n = 0;
        while (bb_cd.ticket) { scan(); CK(++n < 4000); }
        CK(audio.core.running);
        for (unsigned q = 0; q < 3; q++) CK(bb_cd_player(q)->playing.ticket == t && playing_bank(q) == songs[k]);
        if (tempo) CK(audio.core.bpm == bpm);
        /* playing: PLAY is start/stop again (the list does not re-choose) */
        bb_nav.bank = BB_NAV_HOME_SONGS; CK(bb_cd_play() == 0);
    }
    fprintf(stderr, "c13 PLAY on the list: one commit + tempo/swing + start, then PERFORM; 3 slots free: BUSY - RETRY, no start, nothing moved\n");
}
/* ---------------------------------------------------------------- c14 HOME collections as views (RC2, Joe 10:28) */
static void c14(void)
{
    static const char *const shelf[4] = {"Roland CR-8000", "Roland CR-78", "Roland TR-66", "Roland TR-77"};
    world();
    unsigned all = (unsigned)bb_browser_bank_count(&bb_cd.list);
    CK(bb_cd.shelf_ready && bb_cd.nshelf == 4 && all == 71);
    bb_cd_collection(0); CK(bb_cd_count() == 67 && bb_cd.list.group == BB_BROWSER_SONGS);
    for (unsigned v = 0; v < 67; v++) { unsigned b = bb_cd_bank(v); CK(!bb_cd_is_shelf(b) && bb_cd_view(b) == v); }
    bb_cd_collection(2); CK(bb_cd_count() == 4);
    for (unsigned v = 0; v < 4; v++) { CK(bb_cd_bank(v) == bank_named(shelf[v]) && bb_cd_view(bb_cd_bank(v)) == v); }
    bb_cd_collection(1); CK(bb_cd_count() == 21 && bb_cd.list.group == BB_BROWSER_ERAS);
    /* keys, ALGO and OCT walk the view: SONGS 17 is on the second 16-key page, white 1 */
    bb_cd_collection(0); bb_cd_page(1); CK(bb_cd.vpage == 1 && bb_cd_view(bb_cd.list.bank_index) == 16);
    bb_cd_pick(5); CK(bb_cd_view(bb_cd.list.bank_index) == 21);
    bb_cd_move(-100); CK(bb_cd_view(bb_cd.list.bank_index) == 0 && bb_cd.vpage == 0);
    bb_cd_move(200); CK(bb_cd_view(bb_cd.list.bank_index) == 66 && bb_cd.vpage == 4);
    bb_nav_facts f; memset(&f, 0, sizeof f); bb_nav.page = BB_NAV_STUDIO; bb_nav.bank = BB_NAV_HOME_SONGS; bb_cd_facts(&f);
    CK(f.list_count == 3 && f.list_cursor == 2);            /* 67 = 4 x 16 + 3 */
    /* PRESETS: TR-66 chosen and played on all three; its 54 rhythms are the sections */
    bb_cd_collection(2); bb_cd_pick(2); bb_cd.take = 7; bb_cd_choose();
    CK(bb_cd.ticket && bb_nav.bank == BB_NAV_HOME_CONDUCT);
    for (unsigned i = 0; i < 8 && bb_cd.ticket; i++) scan();
    for (unsigned q = 0; q < 2; q++) CK(playing_bank(q) == bank_named("Roland TR-66"));
    fprintf(stderr, "c14 views: SONGS 67 (renumbered), PRESETS 4 (CR-8000, CR-78, TR-66, TR-77), ERAS 21; keys / ALGO / OCT in view; a machine plays\n");
}
/* ---------------------------------------------------------------- c15 preset combining (RC3, Joe 11:32) */
static int has_ev(const bb_factory_drum_data *d, unsigned voice, unsigned step)
{
    for (unsigned k = 0; k < d->count; k++) if (d->events[k].voice == voice && d->events[k].step == step) return 1;
    return 0;
}
static void c15(void)
{
    static const char *const shelf[4] = {"Roland CR-8000", "Roland CR-78", "Roland TR-66", "Roland TR-77"};
    const char *path = getenv("CONDUCTOR_COMBO_DUMP");
    FILE *dump = path ? fopen(path, "w") : 0;
    unsigned pairs = 0, ok = 0, refused = 0;
    world();
    /* (a) the rule on every pair of every machine: Studio's union (one event per voice and step, the later one's fields),
     * refused unless length / lattice / bars agree; at most 4 */
    for (unsigned m = 0; m < 4; m++) {
        unsigned bank = bank_named(shelf[m]), n = sections_of(bank);
        static bb_factory_choice ch[64]; static char name[64][48];
        CK(bank != 9999 && n > 4 && n <= 64);
        for (unsigned i = 0; i < n; i++) {
            bb_browser b; CK(bb_browser_init(&b, &bb_factory_catalogue)); CK(bb_cd_at(&b, BB_BROWSER_SONGS, bank, (int)i, &ch[i]));
            b.part_page = (uint16_t)(i / BB_BROWSER_PART_KEYS); CK(bb_browser_part_title(&b, i % BB_BROWSER_PART_KEYS, name[i], sizeof name[i]));
        }
        for (unsigned i = 0; i < n; i++)
            for (unsigned j = 0; j < n; j++) {
                if (i == j) continue;
                bb_factory_choice two[2] = {ch[i], ch[j]};
                static bb_factory_drum_data a, b, d;
                CK(bb_factory_read_drum_scratch(&bb_factory_catalogue, &ch[i], &a) == BB_FDRUM_OK);
                CK(bb_factory_read_drum_scratch(&bb_factory_catalogue, &ch[j], &b) == BB_FDRUM_OK);
                int rc = bb_factory_combine_drum(&bb_factory_catalogue, two, 2, &d);
                int same = a.length == b.length && a.lattice == b.lattice && a.bars == b.bars;
                pairs++;
                if (!same) { CK(rc == BB_FDRUM_LENGTH); refused++; }
                else {
                    CK(rc == BB_FDRUM_OK || (rc == BB_FDRUM_LENGTH && a.count + b.count > BB_FACTORY_DRUM_EVENTS));
                    if (rc == BB_FDRUM_OK) {
                        ok++;
                        for (unsigned k = 0; k < a.count; k++) CK(has_ev(&d, a.events[k].voice, a.events[k].step));
                        for (unsigned k = 0; k < b.count; k++) CK(has_ev(&d, b.events[k].voice, b.events[k].step));
                        for (unsigned k = 0; k < d.count; k++) {   /* one per voice and step; the later rhythm's fields */
                            CK(has_ev(&a, d.events[k].voice, d.events[k].step) || has_ev(&b, d.events[k].voice, d.events[k].step));
                            for (unsigned l = k + 1; l < d.count; l++) CK(d.events[k].voice != d.events[l].voice || d.events[k].step != d.events[l].step);
                            for (unsigned l = 0; l < b.count; l++)
                                if (b.events[l].voice == d.events[k].voice && b.events[l].step == d.events[k].step)
                                    CK(d.events[k].intensity == b.events[l].intensity && d.events[k].modifiers == b.events[l].modifiers);
                        }
                        bb_phrase_index ph; int pr = bb_factory_prepare_rich_combo(&bb_factory_catalogue, two, 2, &ph);
                        if (pr != BB_FDRUM_OK && getenv("C15_DEBUG")) fprintf(stderr, "prep %d %s + %s count %u\n", pr, name[i], name[j], d.count);
                        CK(pr == BB_FDRUM_OK);
                        if (pr == BB_FDRUM_OK) {
                            CK(ph.phrase.count == d.count);
                            for (unsigned k = 0; k < d.count; k++) {
                                const bb_factory_drum_event *src = &d.events[k];
                                unsigned voice = src->voice;
                                bb_catalogue_id field = bb_catalogue_find(&bb_factory_catalogue, src->source, "source_voice");
                                if (voice == BB_FDV_HH && field != BB_CATALOGUE_NONE) {
                                    const uint8_t *text; size_t len;
                                    CK(bb_catalogue_text(&bb_factory_catalogue, field, &text, &len));
                                    if (len == 2 && text[0] == 'S' && text[1] == 'H') voice = BB_PHRASE_SHAKER;
                                }
                                bb_phrase_event packed;
                                CK(bb_phrase_read(&ph.phrase, k, &packed));
                                CK(packed.voice == voice && packed.step == src->step &&
                                   packed.intensity == src->intensity && packed.modifiers == src->modifiers);
                            }
                        }
                    }
                }
                if (dump && i < j) {
                    fprintf(dump, "%s|%s|%s|%d|", shelf[m], name[i], name[j], rc);
                    for (unsigned k = 0; rc == BB_FDRUM_OK && k < d.count; k++)
                        fprintf(dump, "%s%s:%u", k ? " " : "", bb_factory_drum_voice_name(d.events[k].voice), d.events[k].step);
                    fprintf(dump, "\n");
                }
            }
        /* 4 of one length combine; 5 never */
        bb_factory_choice four[5]; unsigned nf = 0; static bb_factory_drum_data x, y;
        CK(bb_factory_read_drum_scratch(&bb_factory_catalogue, &ch[0], &x) == BB_FDRUM_OK);
        for (unsigned i = 0; i < n && nf < 5; i++) {
            CK(bb_factory_read_drum_scratch(&bb_factory_catalogue, &ch[i], &y) == BB_FDRUM_OK);
            if (y.length == x.length && y.lattice == x.lattice && y.bars == x.bars) four[nf++] = ch[i];
        }
        if (nf == 5) { CK(bb_factory_combine_drum(&bb_factory_catalogue, four, 5, &y) == BB_FDRUM_INVALID);
                       int r4 = bb_factory_combine_drum(&bb_factory_catalogue, four, 4, &y); CK(r4 == BB_FDRUM_OK || r4 == BB_FDRUM_LENGTH); }
    }
    if (dump) fclose(dump);
    /* (b) on PERFORM: the combination through the existing queue (TR-66, all three parts) */
    bb_cd_collection(2); bb_cd_pick(2); bb_cd.take = 7; bb_cd_choose();
    for (unsigned i = 0; i < 8 && bb_cd.ticket; i++) scan();
    unsigned tr = bank_named("Roland TR-66"); CK(playing_bank(0) == tr);
    play(); bb_nav.page = BB_NAV_STUDIO; bb_nav.bank = BB_NAV_HOME_CONDUCT; bb_nav_row = 0;
    uint64_t set = (1u << 0) | (1u << 2);                   /* rhythms 1 and 3 of one length */
    { static bb_factory_drum_data u, v; bb_factory_choice c0, c2; bb_browser b0, b2; bb_browser_init(&b0, &bb_factory_catalogue); b2 = b0;
      CK(bb_cd_at(&b0, BB_BROWSER_SONGS, tr, 0, &c0) && bb_cd_at(&b2, BB_BROWSER_SONGS, tr, 2, &c2));
      bb_factory_read_drum_scratch(&bb_factory_catalogue, &c0, &u); bb_factory_read_drum_scratch(&bb_factory_catalogue, &c2, &v);
      if (u.length != v.length || u.lattice != v.lattice) set = (1u << 0) | (1u << 1); }
    unsigned before = audio.commands.count;
    bb_cd_set(set); CK(bb_cd.ticket && bb_cd.qset == set && audio.commands.count == before + 1);   /* one cue */
    bb_nav_facts f; memset(&f, 0, sizeof f); bb_cd_facts(&f); CK(f.sec_queued == set && (f.queued & BB_NAV_Q_SECTION));
    uint32_t t = bb_cd.ticket; unsigned n = 0;
    while (bb_cd.ticket) { scan(); CK(++n < 4000); }
    for (unsigned q = 0; q < 2; q++) CK(bb_cd_player(q)->playing.ticket == t && audio.rich.status[q].applied == t);
    memset(&f, 0, sizeof f); bb_cd_facts(&f); CK(f.sec_lit == set && bb_cd_cur() == set);
    { bb_nav_state s; bb_nav_init(&s, BB_NAV_STUDIO, 600); s.bank = BB_NAV_HOME_CONDUCT; f.section_count = 54; f.section = 0;
      f.section_queued = f.kit_slot = f.kit_slot_queued = f.list_cursor = f.list_playing = f.list_queued = BB_NAV_NONE_IDX;
      bb_nav_led_out l; bb_nav_leds(&s, &f, 0, &l);
      for (unsigned k = 0; k < 16; k++) CK((l.key[k] == BB_NAV_LED_LIT) == (((set >> k) & 1u) != 0)); }   /* every key lit */
    bb_cd_set(set); CK(!bb_cd.ticket);                       /* already playing: nothing */
    /* the same combination again while it waits = cancel; another = replace (the newer one plays) */
    uint64_t other = (1u << 0) | (1u << 3) | (1u << 4);
    bb_cd_set(other); CK(bb_cd.ticket && bb_cd.qset == other); bb_cd_set(other);
    n = 0; while (bb_cd.ticket || bb_cd.want) { scan(); CK(++n < 4000); }
    CK(bb_cd_cur() == set && !strcmp(said, "CUE CANCELLED"));
    bb_cd_set(other); bb_cd_key(5);                           /* replaced by one section */
    n = 0; while (bb_cd.ticket || bb_cd.want) { scan(); CK(++n < 4000); }
    CK(playing_part(0) == 5 && bb_cd_cur() == (1u << 5) && !bb_cd.pset);
    fprintf(stderr, "c15 combining: %u ordered pairs on CR-8000 / CR-78 / TR-66 / TR-77 (%u merged, %u refused: length), "
                    "Studio's union per voice and step, at most 4; one cue, every key lit, same set cancels, another replaces\n", pairs, ok, refused);
}
/* ---------------------------------------------------------------- c16 the chord (RC3, Joe 11:34) */
static uint64_t chord_run(const unsigned *down_ms, const unsigned *up_ms, unsigned n, uint64_t cur, unsigned *commits)
{
    bb_chord c; memset(&c, 0, sizeof c); uint64_t got = 0; *commits = 0;
    for (unsigned t = 0; t < 2000; t++) {
        uint32_t held = 0;
        for (unsigned i = 0; i < n; i++) {
            if (t == down_ms[i]) bb_chord_press(&c, i, i * 3u + 1u, 1000u + t);
            if (t >= down_ms[i] && t < up_ms[i]) held |= 1u << i;
        }
        uint64_t m = bb_chord_poll(&c, held, 1000u + t, got ? got : cur);
        if (m) { got = m; ++*commits; }
    }
    return got;
}
static void c16(void)
{
    unsigned k;
    { unsigned d[3] = {0, 20, 55}, u[3] = {300, 320, 280};   /* ragged three-key press: one 3-way combination */
      CK(chord_run(d, u, 3, 0, &k) == ((1u << 1) | (1u << 4) | (1u << 7)) && k == 1); }
    { unsigned d[3] = {0, 20, 55}, u[3] = {30, 40, 70};      /* the same, released fast */
      CK(chord_run(d, u, 3, 0, &k) == ((1u << 1) | (1u << 4) | (1u << 7)) && k == 1); }
    { unsigned d[1] = {0}, u[1] = {40};                      /* a tap: that section alone */
      CK(chord_run(d, u, 1, 0, &k) == (1u << 1) && k == 1); }
    { unsigned d[2] = {0, 300}, u[2] = {40, 340}; uint64_t got;   /* two taps outside the window: two singles */
      bb_chord c; memset(&c, 0, sizeof c); unsigned commits = 0; uint64_t last = 0;
      for (unsigned t = 0; t < 2000; t++) { uint32_t held = 0;
          for (unsigned i = 0; i < 2; i++) { if (t == d[i]) bb_chord_press(&c, i, i * 3u + 1u, t); if (t >= d[i] && t < u[i]) held |= 1u << i; }
          if ((got = bb_chord_poll(&c, held, t, last))) { CK(got == (commits ? (1u << 4) : (1u << 1))); last = got; commits++; } }
      CK(commits == 2); }
    { unsigned d[2] = {0, 200}, u[2] = {600, 260};          /* hold past the window, then tap: toggles in */
      CK(chord_run(d, u, 2, 0, &k) == ((1u << 1) | (1u << 4)) && k == 2); }
    { unsigned d[2] = {0, 200}, u[2] = {600, 260};          /* holding a member of {1,4,7}, tapping 4 removes it */
      bb_chord c; memset(&c, 0, sizeof c); uint64_t cur = (1u << 1) | (1u << 4) | (1u << 7), m, last = 0; unsigned commits = 0;
      for (unsigned t = 0; t < 2000; t++) { uint32_t held = 0;
          if (t == d[0]) bb_chord_press(&c, 0, 1, t); if (t == d[1]) bb_chord_press(&c, 1, 4, t);
          for (unsigned i = 0; i < 2; i++) if (t >= d[i] && t < u[i]) held |= 1u << i;
          if ((m = bb_chord_poll(&c, held, t, commits ? cur : cur))) { last = m; commits++; if (commits == 1) cur = m == (1u << 1) ? cur : m; } }
      CK(commits == 2 && last == ((1u << 1) | (1u << 7))); }
    { unsigned d[2] = {0, BB_CHORD_MS - 1u}, u[2] = {30, BB_CHORD_MS + 30u};   /* the window's edge: in */
      CK(chord_run(d, u, 2, 0, &k) == ((1u << 1) | (1u << 4)) && k == 1); }
    { unsigned d[2] = {0, BB_CHORD_MS + 1u}, u[2] = {30, BB_CHORD_MS + 40u};   /* just past it, first key up: two singles */
      CK(chord_run(d, u, 2, 0, &k) == (1u << 4) && k == 2); }
    /* a chord while a cue waits replaces it under the existing rules (c15: cancel, then the newer cue) */
    fprintf(stderr, "c16 chord: 0/20/55 ms = one 3-way commit; a tap = one; two taps 300 ms apart = two singles; hold > %u ms + tap toggles in / out; window edge %u/%u ms\n",
            BB_CHORD_MS, BB_CHORD_MS - 1u, BB_CHORD_MS + 1u);
}
static void c17(void)
{
    world(); bb_nav.page=BB_NAV_STUDIO; bb_nav.bank=BB_NAV_HOME_SONGS; bb_cd.welcome=1;
    unsigned bank=bb_cd.list.bank_index, commands=audio.commands.count;
    bb_cd_choose(); CK(!bb_cd.welcome && bb_nav.bank==BB_NAV_HOME_SONGS);
    CK(bb_cd.list.bank_index==bank && audio.commands.count==commands && !bb_cd.ticket);
    bb_cd.welcome=1; bb_cd_pick(3);
    CK(!bb_cd.welcome && bb_cd.list.bank_index==bank && audio.commands.count==commands);
    bb_cd.welcome=1; bb_cd_move(1);
    CK(bb_cd.welcome && bb_cd.list.bank_index!=bank && audio.commands.count==commands);
    bb_cd_collection(2); CK(!bb_cd.welcome && bb_cd.coll==2);
    bb_cd.welcome=1; bb_nav.page=BB_NAV_PLAY; bb_cd_poll(); CK(!bb_cd.welcome);
    fprintf(stderr,"c17 welcome: EDIT/white open list without loading, ALGO browses, collections and leaving dismiss\n");
}
static void c18(void)
{
    world(); choose(0,7); play(); run_to(15);
    unsigned says_before=says;
    bb_cd_test_refuse_once=1;key(0,3);
    CK(!bb_cd_test_refuse_once&&bb_cd.ticket&&bb_cd.moved==7&&says==says_before);
    uint32_t t=bb_cd.ticket;run_to(33);
    CK(!bb_cd.ticket&&audio.section.status.applied_tick==32);
    for(unsigned q=0;q<3;q++)CK(playing_part(q)==3&&bb_cd_player(q)->playing.ticket==t&&!bb_player_pending(bb_cd_player(q)));
    fprintf(stderr,"c18 boundary race: first publication refused, refreshed cue accepted and applied without a second press\n");
}
int main(int argc, char **argv)
{
    out = fopen("/dev/null", "wb"); CK(out);
    collect();
    static void (*const proof[18])(void) = {c1, c2, c3, c4, c5, c6, c7, c8, c9, c10, c11, c12, c13, c14, c15, c16, c17, c18};
    const char *only = argc > 1 ? argv[1] : "";
    for (unsigned i = 0; i < 18; i++) { char n[5]; snprintf(n, sizeof n, "c%u", i + 1); if (!*only || !strcmp(only, n)) proof[i](); }
    printf("PASS %u checks: P3b conductor %s on bb_conductor_firmware.inc and the real section owner\n", checks, *only ? only : "c1-c18");
    return 0;
}
