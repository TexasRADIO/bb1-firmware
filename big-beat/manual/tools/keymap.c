/* Lane LP-P4 (BB1 2.1 manual): the key map of every page from the tree's own one table (bb_nav.c 2.1 P1):
 * bb_nav_key_role / bb_nav_key_word / bb_nav_key_led / bb_nav_key_help for the keys, bb_nav_button_role and
 * bb_nav_leds for the buttons, bb_nav_knobs, bb_nav_edit_label, bb_nav_legend and bb_nav_help (the generated HELP
 * rows). Firmware feature mask (bb_nav_firmware.inc BB_NAV_FEATURES) and example facts (the capture gate's).
 * LED letters: . off, h half-lit (DIM), B bright (LIT), p pulse (BREATH). JSON out. Host only. */
#include <stdio.h>
#include <string.h>
#include "bb_nav.h"
#include "bb/bb_text_block.h"

static const char L[4] = {'.', 'h', 'B', 'p'};
static bb_nav_facts F;
static void facts(uint8_t gen_mode)
{
    memset(&F, 0, sizeof F);
    F.running = 1; F.section = 1; F.section_count = 6; F.list_count = 16; F.list_playing = 1; F.list_cursor = 3;
    F.kit_slot = F.kit_slot_queued = F.section_queued = F.list_queued = BB_NAV_NONE_IDX;
    F.step_banks = 2; F.steps = 0x1111; F.evo_parts = 1; F.evo_moves = 3; F.gen_mode = gen_mode;
    F.song = BB_NAV_SONG_F_ROWS | BB_NAV_SONG_F_UNDO; F.home_take = 7; F.home_coll = 0;
}
static void js(const char *s)
{
    putchar('"');
    for (; *s; s++) { if (*s == '"' || *s == '\\') putchar('\\'); if ((unsigned char)*s == 0xB7) fputs("\\u00b7", stdout); else putchar(*s); }
    putchar('"');
}
static const char *const BN[BB_NAV_BUTTONS] = {"FX", "SCL", "ENV", "LFO", "EDIT", "GLO", "HOME", "SAVE", "ARP", "SEQ", "PLAY", "REC", "OCT-", "OCT+"};
static int first = 1;
static void dump(const char *name, int row, int page, int keys, int bank, uint8_t gen_mode, int sec_page, int sec_count)
{
    bb_nav_state s; bb_nav_out o; char leg[96], legb[96]; uint8_t r[4]; const char *lab[4]; bb_nav_led_out l;
    bb_nav_init(&s, BB_NAV_STUDIO, 400);
    bb_nav_set_features(&s, (BB_NAV_F_TAP | BB_NAV_F_SCALE | BB_NAV_F_THROW | BB_NAV_F_STEP_PITCH));   /* bb_nav_firmware.inc BB_NAV_FEATURES */
    bb_nav_set_instrument(&s, row < 2 ? BB_NAV_INSTR_DRUM : BB_NAV_INSTR_DILLO, row < 2 ? 2 : 1); bb_nav_set_focus(&s, (uint8_t)row);
    memset(&o, 0, sizeof o); bb_nav_goto(&s, (uint8_t)page, &o);
    s.play_keys = (uint8_t)keys; s.bank = (uint8_t)bank; s.gen_mode = gen_mode;
    facts(gen_mode);
    if (sec_count) { F.section_count = (uint8_t)sec_count; s.sec_count = (uint8_t)sec_count; s.sec_page = (uint8_t)sec_page; F.section = (uint8_t)(sec_count - 5); }
    printf("%s", first ? "" : ",\n"); first = 0;
    js(name); printf(":{\"label\":");
    {   const char *b = bb_nav_bank_name(&s); char pn[48];
        if (b && b[0]) snprintf(pn, sizeof pn, "%s  %s", bb_nav_page_name(&s), b); else snprintf(pn, sizeof pn, "%s", bb_nav_page_label(&s));
        js(pn); }
    bb_nav_legend(&s, leg, sizeof leg, 1); bb_nav_legend(&s, legb, sizeof legb, 0);
    printf(",\"legend\":"); js(leg); printf(",\"legend_black\":"); js(legb);
    printf(",\"edit\":"); js(bb_nav_edit_label(&s, &F));
    bb_nav_knobs(&s, r, lab);
    printf(",\"knobs\":["); for (int k = 0; k < 4; k++) { if (k) putchar(','); js(lab[k]); } printf("]");
    bb_nav_leds(&s, &F, 0, &l);
    printf(",\"keys\":[");
    for (unsigned k = 0; k < BB_NAV_KEYS; k++) {
        uint8_t a, b, x = bb_nav_key_role(&s, (uint8_t)k, 0, &a), y = bb_nav_key_role(&s, (uint8_t)k, 1, &b);
        printf("%s[", k ? "," : ""); js(bb_nav_key_word(&s, (uint8_t)k, 0)); printf(",%u,%u,\"%c\",", x, a, L[l.key[k] & 3]);
        js(x ? bb_nav_help_text(bb_nav_key_help(x, a)) : ""); printf(","); js(bb_nav_key_word(&s, (uint8_t)k, 1)); printf(",%u,%u]", y, b);
    }
    printf("],\"buttons\":{");
    for (unsigned b = 0; b < BB_NAV_BUTTONS; b++) {
        uint8_t br = bb_nav_button_role(&s, &F, (uint8_t)b, 0), brr = bb_nav_button_role(&s, &F, (uint8_t)b, 1);
        printf("%s", b ? "," : ""); js(BN[b]); printf(":[\"%c\",", L[l.button[b] & 3]); js(bb_nav_help_text(bb_nav_button_help(br)));
        printf(","); js(bb_nav_help_text(bb_nav_button_help(brr))); printf("]");
    }
    printf("},\"help\":[");
    {   bb_nav_help_row hr[BB_NAV_HELP_ROWS]; int found = -1;
        unsigned n = bb_nav_help(&s, &F, 0, hr, 0, BB_NAV_HELP_ROWS, 0, &found);
        for (unsigned i = 0; i < n && i < BB_NAV_HELP_ROWS; i++) {
            printf("%s[%u,%u,%u,", i ? "," : "", hr[i].kind, hr[i].a, hr[i].b); js(bb_nav_help_text(hr[i].text)); printf("]");
        }
    }
    printf("]}");
}
int main(void)
{
    printf("{");
    dump("studio-songs", 0, BB_NAV_STUDIO, 0, BB_NAV_HOME_SONGS, 0, 0, 0);
    dump("studio", 0, BB_NAV_STUDIO, 0, BB_NAV_HOME_CONDUCT, 0, 0, 0);
    dump("studio-p3", 0, BB_NAV_STUDIO, 0, BB_NAV_HOME_CONDUCT, 0, 2, 43);   /* RC2 PERFORM pages of 16: CR-8000 page 3 = 33-43 */
    dump("studio-p4", 0, BB_NAV_STUDIO, 0, BB_NAV_HOME_CONDUCT, 0, 3, 54);   /* TR-66 page 4 = 49-54 */
    dump("play-drums", 0, BB_NAV_PLAY, 0, 0, 0, 0, 0); dump("steps-drums", 0, BB_NAV_PLAY, 1, 0, 0, 0, 0);
    dump("play-dillo", 2, BB_NAV_PLAY, 0, 0, 0, 0, 0); dump("steps-dillo", 2, BB_NAV_PLAY, 1, 0, 0, 0, 0);
    dump("kit-a", 0, BB_NAV_KIT, 0, 0, 0, 0, 0); dump("kit-b", 1, BB_NAV_KIT, 0, 0, 0, 0, 0); dump("kit-dillo", 2, BB_NAV_KIT, 0, 0, 0, 0, 0);
#if BB_WITH_USER_KITS
    dump("kit-build", 0, BB_NAV_KIT, 0, BB_NAV_KIT_BUILD, 0, 0, 0); dump("kit-save-as", 0, BB_NAV_KIT, 0, BB_NAV_KIT_SAVE_AS, 0, 0, 0);
#endif
    dump("pattern", 0, BB_NAV_PATTERN, 0, 0, 0, 0, 0); dump("pattern-dillo", 2, BB_NAV_PATTERN, 0, 0, 0, 0, 0);
    dump("sound-drums", 0, BB_NAV_SOUND, 0, 0, 0, 0, 0); dump("sound-drums-colour", 0, BB_NAV_SOUND, 0, 1, 0, 0, 0);
    dump("sound-dillo", 2, BB_NAV_SOUND, 0, 0, 0, 0, 0);
    for (int b = 0; b < 5; b++) { char n[16]; snprintf(n, sizeof n, "fx-%d", b); dump(n, 0, BB_NAV_FX, 0, b, 0, 0, 0); }
    dump("save", 0, BB_NAV_SAVE, 0, 0, 0, 0, 0);
    dump("evolve", 0, BB_NAV_EVOLVE, 0, 0, BB_NAV_MODE_EVOLVE, 0, 0); dump("euclid", 0, BB_NAV_EVOLVE, 0, 0, BB_NAV_MODE_EUCLID, 0, 0);
    dump("random", 0, BB_NAV_EVOLVE, 0, 0, BB_NAV_MODE_RANDOM, 0, 0); dump("evolve-dillo", 2, BB_NAV_EVOLVE, 0, 0, BB_NAV_MODE_EVOLVE, 0, 0);
    dump("global", 0, BB_NAV_GLOBAL, 0, 0, 0, 0, 0); dump("global-dillo", 2, BB_NAV_GLOBAL, 0, 0, 0, 0, 0);
    dump("midi", 0, BB_NAV_GLOBAL, 0, BB_NAV_GLOBAL_MIDI, 0, 0, 0); dump("clock", 0, BB_NAV_GLOBAL, 0, BB_NAV_GLOBAL_CLOCK, 0, 0, 0);
    dump("song", 0, BB_NAV_SONG, 0, BB_NAV_SONG_ROWS, 0, 0, 0); dump("song-add", 0, BB_NAV_SONG, 0, BB_NAV_SONG_ADD, 0, 0, 0);
    printf("}\n");
    return 0;
}
