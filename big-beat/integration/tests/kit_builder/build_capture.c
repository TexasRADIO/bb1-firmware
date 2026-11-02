/* SPDX-License-Identifier: GPL-3.0-only
 * 2.0 kit builder screens on the candidate's own capture harness (capture.c: real gfx.c, fonts, art, bb_ui_v3 and
 * bb_nav.c, with its layout lint), compiled with the 2.0 recipe's -DBB_WITH_USER_KITS=1. Navigation state comes from
 * bb_nav_input (black 1, EDIT ...), so page names, K1-K4 labels and the key LEDs are the module's own; the screen
 * content is filled as bb_ui_v3_firmware.inc fills it (bb_uk_v3_kit: BUILD and SAVE AS in the KIT list layout),
 * with example kits. Example data, not device screenshots.
 * Build (from a source tree): clang -I firmware/src -I build/gen -I big-beat/integration/tests -Dsection(x)=used
 *   -DBB_WITH_USER_KITS=1 build_capture.c firmware/src/bb_nav.c -> ./a.out frames.raw (then tests/png.py) */
#define main capture_main
#include "capture.c"
#undef main
#if !BB_WITH_USER_KITS
#error "build with the 2.0 recipe's -DBB_WITH_USER_KITS=1"
#endif
static bb_nav_state ns;
static char pname[24];
static void nav_scan(uint32_t pressed, uint32_t held, uint32_t keys)
{
    static uint32_t t = 1000; bb_nav_in i; bb_nav_out o;
    memset(&i, 0, sizeof i); t += 20; i.now_ms = t; i.pressed = pressed; i.held = held; i.keys = keys; i.keys_held = keys;
    i.facts.kit_slot = i.facts.kit_slot_queued = i.facts.section_queued = BB_NAV_NONE_IDX;
    i.facts.list_cursor = i.facts.list_playing = i.facts.list_queued = BB_NAV_NONE_IDX;
    bb_nav_input(&ns, &i, &o);
    if (keys) { i.keys = i.keys_held = 0; i.pressed = 0; bb_nav_input(&ns, &i, &o); }
}
static void at(unsigned bank, unsigned focus)
{
    bb_nav_out o;
    bb_nav_init(&ns, BB_NAV_STUDIO, 600);
    bb_nav_set_instrument(&ns, BB_NAV_INSTR_DRUM, 2); bb_nav_set_focus(&ns, (uint8_t)focus);
    memset(&o, 0, sizeof o); bb_nav_goto(&ns, BB_NAV_KIT, &o);
    if (bank) nav_scan(0, 0, 1u << BB_NAV_BLACK(1));
    if (bank == 2) { nav_scan(BB_NAV_BIT(BB_NAV_B_EDIT), BB_NAV_BIT(BB_NAV_B_EDIT), 0); nav_scan(0, 0, 0); }
}
/* the frame every page shares, as bb_v3_common: page name ("KIT  BUILD"), knob labels, key LEDs */
static void frame(uint8_t page, unsigned cursor, unsigned count, const char *const v[4], const uint8_t bar[4])
{
    bb_nav_facts f; bb_nav_led_out l; const char *b = bb_nav_bank_name(&ns);
    base(&ns, (uint8_t)ns.focus, page);
    snprintf(pname, sizeof pname, "%s%s%s", bb_nav_page_name(&ns), b && b[0] ? "  " : "", b ? b : "");
    in.page_name = pname; in.running = 1;
    memset(&f, 0, sizeof f); f.list_cursor = (uint8_t)cursor; f.list_count = (uint8_t)count; f.running = 1;
    f.list_playing = f.list_queued = f.kit_slot = f.kit_slot_queued = f.section_queued = BB_NAV_NONE_IDX;
    bb_nav_leds(&ns, &f, 0, &l);
    for (int k = 0; k < 27; k++) in.key_led[k] = l.key[k];
    for (int k = 0; k < 4; k++) if (v && v[k]) { in.knob[k].text = v[k]; in.knob[k].bar = bar[k]; }
}
static void list(const char *title, const char *sub, const char *const *items, unsigned n, unsigned cursor, unsigned playing)
{
    in.title = title; in.subtitle = sub; in.items = (uint8_t)n;
    for (unsigned i = 0; i < n; i++) in.item[i] = items[i];
    in.cursor = (uint8_t)cursor; in.playing_item = (uint8_t)playing; in.queued_item = BB_UI_NO;
}
int main(int argc, char **argv)
{
    /* K1-K4 as bb_nav_knob_text shows them: level, tune (64 = as recorded, quarter semitones), decay (127 = whole) */
    static const char *const L[4] = {"127", "64", "127", "BD"}, *const S[4] = {"110", "72", "91", "SD"};
    static const uint8_t LB[4] = {254, 128, 254, 0}, SB[4] = {220, 144, 182, 7};
    /* five of the 17 lanes around the focus, as bb_uk_v3_lane writes them */
    static const char *const A[5] = {"BD  LinnDrum", "SD  DMX", "LT  DrumTraks HT SUB", "MT  808", "HT  808"};
    out = fopen(argc > 1 ? argv[1] : "build.raw", "wb");
    bb_ui_init(&u, 0, 1);
    /* 01: the KIT list on drum A, USER 1-4 after the machines (the picker's own rows) */
    at(0, 0); frame(BB_UI_KIT, 2, 8, 0, 0);
    { static const char *const it[5] = {"808", "LinnDrum", "DrumTraks", "DMX", "USER 1  BOOMBAP"};
      list("DrumTraks", "LOADED 808", it, 5, 2, 0); in.browser_state = BB_UI_BROWSER_BROWSE;
      in.notice = bb_nav_message_text(BB_NAV_MSG_KIT_BUILD); shot("01-kit-list-entry"); in.notice = 0; }
    /* 02: BUILD (black 1): the kit's name, a status line, the lanes */
    at(1, 0); frame(BB_UI_KIT, 0, 17, L, LB); list("USER 1 BOOMBAP", "EDITED \xB7 EDIT SAVE AS", A, 5, 0, BB_UI_NO);
    shot("02-build-lanes");
    /* 03: PRESETS turned on SD: the lane's kit changes at once (heard when stopped) */
    frame(BB_UI_KIT, 1, 17, L, LB); list("USER 1 BOOMBAP", "EDITED \xB7 EDIT SAVE AS", A, 5, 1, BB_UI_NO); in.running = 0;
    shot("03-build-kit-turned");
    /* 04: K4 on LT: a voice DrumTraks lacks, its usual substitute plays (~) */
    { static const char *const V[4] = {"127", "64", "127", "HT ~"}; static const uint8_t VB[4] = {254, 128, 254, 28};
      frame(BB_UI_KIT, 2, 17, V, VB); list("USER 1 BOOMBAP", "EDITED \xB7 EDIT SAVE AS", A, 5, 2, BB_UI_NO);
      shot("04-build-voice"); }
    /* 05: K1-K3 shape the lane */
    frame(BB_UI_KIT, 1, 17, S, SB); list("USER 1 BOOMBAP", "EDITED \xB7 EDIT SAVE AS", A, 5, 1, BB_UI_NO);
    shot("05-build-shape");
    /* 06: a kit naming a machine this library lacks: the status line says so, MISSING lanes, the 808 stands in */
    { static const char *const M[5] = {"BD  MISSING", "SD  MISSING", "LT  808", "MT  808", "HT  MISSING"};
      static const char *const V[4] = {"64", "64", "108", "SD ?"}; static const uint8_t VB[4] = {128, 128, 216, 7};
      at(1, 0); frame(BB_UI_KIT, 1, 17, V, VB);
      list("USER 3 SIMMONSY", "KIT NOT INSTALLED \xB7 808 STANDS IN", M, 5, 1, BB_UI_NO); shot("06-build-missing-kit"); }
    /* 07: SAVE AS (EDIT): white 1-4 choose the slot, EDIT stores (stopped); the kit keeps its source's name */
    { static const char *const it[4] = {"USER 1  BOOMBAP", "USER 2  (EMPTY)", "USER 3  SIMMONSY", "USER 4  (EMPTY)"};
      at(2, 0); frame(BB_UI_KIT, 1, 4, 0, 0); in.running = 0;
      list("SAVE AS", "KEYS 1-4 SLOT \xB7 EDIT STORE (STOPPED)", it, 4, 1, 0); shot("07-save-as");
      in.notice = "USER KIT STORED"; shot("07b-stored"); in.notice = 0; }
    /* 08: recall: the KIT list's USER rows, then PATTERN carries it (kit+phrase): QUEUED, then APPLIED */
    { static const char *const it[3] = {"USER 2  MYKIT", "USER 3  SIMMONSY", "USER 4  (EMPTY)"};
      at(0, 0); frame(BB_UI_KIT, 5, 8, 0, 0);
      list("USER 2  MYKIT", "LOADED USER 1 BOOMBAP", it, 3, 0, BB_UI_NO); in.browser_state = BB_UI_BROWSER_BROWSE; shot("08-kit-list-user");
      bb_nav_out o; memset(&o, 0, sizeof o); bb_nav_goto(&ns, BB_NAV_PATTERN, &o);
      base(&ns, 0, BB_UI_PATTERN); browse(BB_UI_PATTERN); in.subtitle = "NEXT KIT: USER 2"; in.queued_item = 1;
      in.queued = BB_UI_Q_PATTERN; in.browser_state = BB_UI_BROWSER_QUEUED; in.progress = 140;
      in.notice = "USER KIT QUEUED / NEXT BAR"; shot("09-pattern-user-queued");
      base(&ns, 0, BB_UI_PATTERN); browse(BB_UI_PATTERN); in.subtitle = "ERA \xB7 Part 1"; in.notice = "USER KIT APPLIED";
      shot("10-pattern-user-applied"); }
    /* help: BUILD, SAVE AS (both HELP_BUILD), the KIT list on drum A */
    at(1, 0); frame(BB_UI_KIT, 0, 17, L, LB); in.help = 1; shot("11-help-build");
    at(2, 0); frame(BB_UI_KIT, 0, 4, 0, 0); in.help = 1; shot("12-help-save-as");
    at(0, 0); frame(BB_UI_KIT, 0, 8, 0, 0); in.help = 1; shot("13-help-kit");
    /* drum B: no BUILD (black 1 dark), the list as before */
    at(0, 1); frame(BB_UI_KIT, 1, 4, 0, 0); browse(BB_UI_KIT); shot("14-kit-b-no-build");
    fclose(out);
    printf("kit builder captures: %u frames, %u lint failures\n", shots, failures);
    return failures != 0;
}
