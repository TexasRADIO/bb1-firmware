/* SPDX-License-Identifier: GPL-3.0-only
 * Big Beat navigation core v2. See bb_nav.h. Pure decisions, no side effects
 * outside the caller's bb_nav_state / bb_nav_out / bb_nav_led_out. */
#include "bb_nav.h"
#include "bb/bb_text_block.h"
#ifndef BB_NAV_FIRMWARE_INCLUDE
#include <string.h>
#endif

/* Internal names are nav_-prefixed (v2.1) so the file can be #included in
 * the firmware's single translation unit without clashing (go_home ...). */
#define B(x) BB_NAV_BIT(BB_NAV_B_##x)
#define BOTH_OCT (B(OCTDN) | B(OCTUP))
#define NONE_IDX BB_NAV_NONE_IDX

/* ---------------------------------------------------------------- tables */

static const char *const nav_page_names[BB_NAV_PAGES] = {
    "STUDIO", "PLAY", "KIT", "PATTERN", "SOUND", "FX", "SAVE", "EVOLVE", "GLOBAL", "SONG"
};

static const char *const nav_role_labels[BB_NAV_ROLES] = {
    "",
    "KICK", "SN/CL", "HATS", "PERC",
    "LEVEL", "TUNE", "DECAY", "SWING",
    "LEVEL", "DRIVE", "TONE", "ATTACK", "DIST", "SNAPPY", "MODE", "CONTROL",
    "LEVEL", "CUTOFF", "RESO", "ENV MOD", "DECAY", "ACCENT", "DRIVE", "SLIDE", "WAVE",
    "SQUARE", "SAW", "SUB", "MOD", "LFO", "GLIDE",
    "DRUM A", "DRUM B", "DILLO", "FX RET", "BPM", "A/B BAL", "TRANSPOS",
    "TIME MS", "FDBK %", "HEADS", "ECHO WET", "TONE %", "DRIVE %", "WEAR %", "SIZE %",
    "DAMP %", "SPR WET", "MOTION %", "ECHO", "SPRING",       /* the two wets named apart (SPRING bank shows both) */
    "SLOT", "AMOUNT", "EVERY", "ROOT", "SCALE",
    "ROW", "REPEATS", "BARS", "LOOP", "TARGET", "GROUP", "PART", "BARS",
    "KIT A CH", "KIT B CH", "BASS CH", "LEAD CH", "CLOCK IN", "FOLLOW", "TRANSPORT", "CLOCK OUT"
#if BB_WITH_USER_KITS
    , "LEVEL", "TUNE", "DECAY", "VOICE"
#endif
};

/* 2.0 help flash: message IDs; the texts live in the BBTX flash block (bb/bb_text_block.h). TX_NONE = no text. */
static const uint16_t nav_messages[BB_NAV_MESSAGES] = {
    TX_NONE,
    TX_WAIT,
    TX_STUDIO_EDIT_SONG,
    TX_PADS_SEQ_STEPS,
    TX_STEPS_ARP_LIVE,
    TX_KEYS_SEQ_STEPS,
    TX_KIT_EDIT_CHOOSE,
    TX_PATCH_EDIT_LOAD,
    TX_PATTERN_EDIT_CHOOSE,
    TX_SOUND_OCT_MORE,
    TX_FX_WHITE_1_5_TABS,
    TX_SAVE_KEYS_1_4_EDIT_STORE,
    TX_EVOLVE_EDIT_CAPTURE,
    TX_GLOBAL_K1_K4,
    TX_CHOOSE_PATTERN_PLAY_START,
    TX_ALL_NOTES_OFF,
    TX_RESTART_FROM_BAR_1,
    TX_SWING_ON,
    TX_SWING_OFF,
    TX_SCALE_OFF,
    TX_SCALE_MINOR,
    TX_SCALE_MAJOR,
    TX_SCALE_DORIAN,
    TX_SCALE_PENTA,
    TX_ROOT_PRESS_A_KEY,
    TX_ROOT_SET,
    TX_TAP_TEMPO,
    TX_DUB_THROW,
    TX_STOP_FIRST,
    TX_OCTAVE_LIMIT,
    TX_KIT_WAITING_EDIT_RETRY,
    TX_PATTERN_WAITING_EDIT_RETRY,
    TX_NOT_IN_THIS_BUILD,
    TX_NONE,                 /* AUDIO FAULT / LOAD BLOCKED: a safety text, kept in the app (bb_nav_message_text) */
    TX_KIT_NOT_READY,
    TX_PATTERNS_NOT_READY,
    TX_SOUNDS_NOT_READY,
    TX_NO_PATTERNS_FOR_THIS_KIT,
    TX_KIT_PATTERN_MISMATCH,
    TX_TIMING_NOT_SUPPORTED,
    TX_VOICE_NOT_SUPPORTED,
    TX_EXPRESSION_NOT_SUPPORTED,
    TX_PATTERN_UNAVAILABLE,
    TX_SOUND_REQUEST_REFUSED,
    TX_SOUND_ALREADY_LOADED,
    TX_NO_ALTERNATE_VOICE,
    TX_LAYER_OFF,
    TX_SOURCE_LENGTH_READ_ONLY,
    TX_SOURCE_PATTERN_EVOLVE_OFF,
    TX_SECTIONS_1_11,
    TX_SECTIONS_12_22,
    TX_SECTIONS_23_33,
    TX_SOUND_K1_K4,
    TX_ARRANGE_RAM_PROJECT, TX_GLOBAL_MIDI, TX_GLOBAL_MIDI, TX_SELECT_A_B_DILLO_EDIT_ADDS,
    TX_EUCLID_EDIT_APPLY, TX_RANDOM_EDIT_APPLY
#if BB_WITH_USER_KITS
    , TX_BUILD_KEYS_LANE_PRESETS_KIT, TX_SAVE_AS_KEYS_1_4_EDIT_STORE, TX_KIT_BLACK_1_BUILD
#endif
};

static const char *const nav_scale_names[BB_NAV_SCALES] = { "OFF", "MINOR", "MAJOR", "DORIAN", "PENTA" };
static const uint8_t nav_scale_len[BB_NAV_SCALES] = { 12, 7, 7, 7, 5 };
static const uint8_t nav_scale_iv[BB_NAV_SCALES][7] = {
    {0, 0, 0, 0, 0, 0, 0},
    {0, 2, 3, 5, 7, 8, 10},
    {0, 2, 4, 5, 7, 9, 11},
    {0, 2, 3, 5, 7, 9, 10},
    {0, 3, 5, 7, 10, 0, 0}
};
/* FM-1 keyboard F..G (v2.1): semitone of each logical key above the bottom
 * F, i.e. its physical note-bit index (bb_panel.c key_index order). */
static const uint8_t nav_white_semi[7] = {0, 2, 4, 6, 7, 9, 11};
static const uint8_t nav_black_semi[11] = {1, 3, 5, 8, 10, 13, 15, 17, 20, 22, 25};
#define NOTE_BASE 41   /* bottom F at key octave 0 = MIDI 41 (installed Dillo key base) */

static const uint8_t nav_fx_roles[4][4] = {
    {BB_NAV_R_ECHO_TIME, BB_NAV_R_ECHO_FDBK, BB_NAV_R_ECHO_HEADS, BB_NAV_R_ECHO_WET},
    {BB_NAV_R_ECHO_TONE, BB_NAV_R_ECHO_DRIVE, BB_NAV_R_ECHO_WEAR, BB_NAV_R_ECHO_WET},
    {BB_NAV_R_SPRING_SIZE, BB_NAV_R_SPRING_DAMP, BB_NAV_R_SPRING_WET, BB_NAV_R_ECHO_WET},
    {BB_NAV_R_ECHO_MOTION, BB_NAV_R_ECHO_WEAR, BB_NAV_R_ECHO_FDBK, BB_NAV_R_ECHO_WET}
};
static const char *const nav_fx_bank_names[5] = {"ECHO", "TAPE DRIVE", "SPRING", "MOTION", "SENDS"};   /* 2.1 P1: the tabs */
static const char *const nav_mode_names[BB_NAV_MODES] = {"EVOLVE", "EUCLID", "RANDOM"};

/* bb_drum_knob_parameter() colour tables, by voice family (unchanged). */
static const uint8_t nav_colour_bass[4]  = {BB_NAV_R_TONE, BB_NAV_R_ATTACK, BB_NAV_R_DIST, BB_NAV_R_VOICE_LEVEL};
static const uint8_t nav_colour_snare[4] = {BB_NAV_R_TONE, BB_NAV_R_SNAPPY, BB_NAV_R_DIST, BB_NAV_R_VOICE_LEVEL};
static const uint8_t nav_colour_tom[4]   = {BB_NAV_R_MODE, BB_NAV_R_DRIVE, BB_NAV_R_DIST, BB_NAV_R_VOICE_LEVEL};
static const uint8_t nav_colour_clap[4]  = {BB_NAV_R_MODE, BB_NAV_R_ATTACK, BB_NAV_R_DIST, BB_NAV_R_DRIVE};
static const uint8_t nav_colour_metal[4] = {BB_NAV_R_DIST, BB_NAV_R_TUNE, BB_NAV_R_DECAY, BB_NAV_R_DRIVE};

/* --------------------------------------------------------------- helpers */

static int nav_is_drum(const bb_nav_state *s)
{
    return s->instrument == BB_NAV_INSTR_DRUM || s->instrument == BB_NAV_INSTR_DRUM_909;
}
/* BB_NAV_FEATURES_BUILD (compile time) bounds the runtime mask: a bit that
 * is 0 here lets the compiler drop that function's code entirely. */
#ifndef BB_NAV_FEATURES_BUILD
#define BB_NAV_FEATURES_BUILD BB_NAV_F_ALL
#endif
static int nav_has(const bb_nav_state *s, unsigned f) { return ((BB_NAV_FEATURES_BUILD & f) != 0) && (s->features & f) != 0; }
static int nav_steps(const bb_nav_state *s) { return s->play_keys == BB_NAV_PLAY_STEPS; }
/* 2.1 P1: GLOBAL white 5 (the Dillo kit view) exists only when the firmware builds that view */
#ifndef BB_NAV_HAS_DILLO_VIEW
#define BB_NAV_HAS_DILLO_VIEW 1
#endif
/* 2.1 P1: a REC tap on SONG records a take only when the build has live capture */
#ifndef BB_NAV_HAS_SONG_RECORD
#define BB_NAV_HAS_SONG_RECORD 1
#endif
static int nav_midi(const bb_nav_state *s) { return s->page == BB_NAV_GLOBAL && s->bank != BB_NAV_GLOBAL_MAIN; }
#if BB_WITH_USER_KITS
/* 2.0 kit builder: KIT on drum A (an 808-family layer, focus row 0) has BUILD; drum B and Dillo do not. */
static int nav_build_ok(const bb_nav_state *s)
{
    return s->page == BB_NAV_KIT && s->instrument == BB_NAV_INSTR_DRUM && s->focus == 0;
}
#endif
static void nav_knob_roles(const bb_nav_state *s, uint8_t r[4], uint8_t idx[4]);
static uint8_t nav_key_led(const bb_nav_state *s, const bb_nav_facts *f, uint8_t k, uint32_t keys_held, int rec);

/* v2.1 section pages: how many pages the current pattern needs (>= 1). */
static int nav_perform(const bb_nav_state *s) { return s->page == BB_NAV_STUDIO && s->bank == BB_NAV_HOME_CONDUCT; }
static unsigned nav_sec_keys(const bb_nav_state *s) { return nav_perform(s) ? BB_NAV_CONDUCT_KEYS : BB_NAV_SECTION_KEYS; }
static uint8_t nav_section_pages(const bb_nav_state *s)
{
    unsigned k = nav_sec_keys(s), m = nav_perform(s) ? BB_NAV_CONDUCT_PAGES : BB_NAV_SECTION_PAGES;
    unsigned n = s->sec_count ? (s->sec_count + k - 1u) / k : 1u;
    return (uint8_t)(n > m ? m : n);
}
uint8_t bb_nav_section_page(const bb_nav_state *s) { return s->sec_page; }

uint8_t bb_nav_bank_count(const bb_nav_state *s, uint8_t page)
{
    if (page == BB_NAV_FX) return 5;
    if (page != BB_NAV_SOUND) return 1;
    switch (s->instrument) {
    case BB_NAV_INSTR_DRUM: return 2;
    case BB_NAV_INSTR_DRUM_909: return s->sound_banks < 1 ? 1 : s->sound_banks > 8 ? 8 : s->sound_banks;
    case BB_NAV_INSTR_TELEGRAM: return 2;
    default: return 1;
    }
}

static uint8_t nav_entry_message(const bb_nav_state *s)
{
    switch (s->page) {
    case BB_NAV_STUDIO: return BB_NAV_MSG_STUDIO;
    case BB_NAV_PLAY:
        if (nav_steps(s)) return BB_NAV_MSG_PLAY_STEPS;
        return nav_is_drum(s) ? BB_NAV_MSG_PLAY_PADS : BB_NAV_MSG_PLAY_KEYS;
#if BB_WITH_USER_KITS
    case BB_NAV_KIT: return nav_build_ok(s) ? BB_NAV_MSG_KIT_BUILD : nav_is_drum(s) ? BB_NAV_MSG_KIT : BB_NAV_MSG_PRESET;
#else
    case BB_NAV_KIT: return nav_is_drum(s) ? BB_NAV_MSG_KIT : BB_NAV_MSG_PRESET;
#endif
    case BB_NAV_PATTERN: return BB_NAV_MSG_PATTERN;
    case BB_NAV_SOUND: return bb_nav_bank_count(s, BB_NAV_SOUND) > 1 ? BB_NAV_MSG_SOUND : BB_NAV_MSG_SOUND_KNOBS;
    case BB_NAV_FX: return BB_NAV_MSG_FX;
    case BB_NAV_SAVE: return BB_NAV_MSG_SAVE;
    case BB_NAV_EVOLVE: return s->gen_mode == BB_NAV_MODE_EUCLID ? BB_NAV_MSG_EUCLID : s->gen_mode == BB_NAV_MODE_RANDOM ?
                               BB_NAV_MSG_RANDOM : BB_NAV_MSG_EVOLVE;
    case BB_NAV_SONG: return s->bank == BB_NAV_SONG_ADD ? BB_NAV_MSG_NONE : BB_NAV_MSG_SONG;
    case BB_NAV_GLOBAL: return s->bank == BB_NAV_GLOBAL_MIDI ? BB_NAV_MSG_MIDI : s->bank ? BB_NAV_MSG_CLOCK : BB_NAV_MSG_GLOBAL;
    default: return BB_NAV_MSG_GLOBAL;
    }
}

static void nav_emit(bb_nav_out *o, uint8_t key, uint8_t role, uint8_t arg, uint8_t flags)
{
    bb_nav_key_ev *e;
    if (o->kev_count >= BB_NAV_KEV_MAX) return;      /* cannot happen: 27 + 27 */
    e = &o->kev[o->kev_count++];
    e->key = key; e->role = role; e->arg = arg; e->flags = flags;
}

/* Emit the owed RELEASE for every active (NOTE/FILL) key in mask. */
static void nav_release_keys(bb_nav_state *s, bb_nav_out *o, uint32_t mask)
{
    uint8_t k;
    mask &= s->keys_active;
    for (k = 0; k < BB_NAV_KEYS; k++)
        if (mask & (1u << k)) nav_emit(o, k, s->act_role[k], s->act_arg[k], BB_NAV_KF_RELEASE);
    s->keys_active &= ~mask;
}

static void nav_drop_routes(bb_nav_out *o)
{
    unsigned i;
    for (i = 0; i < BB_NAV_ENCODERS; i++) {
        o->enc[i].target = BB_NAV_TO_NONE; o->enc[i].role = BB_NAV_R_NONE;
        o->enc[i].index = 0; o->enc[i].flags = 0; o->enc[i].delta = 0;
    }
}

/* Page/focus change: drop this scan's detents and presses, release every
 * owed key, and ignore keys still held until they are lifted. */
static void nav_drain(bb_nav_state *s, bb_nav_out *o, uint32_t hold)
{
    /* 2.1 P2: release only the keys whose role changes (spec 3.2.2). A Dillo note held across PLAY, PATCH and SOUND
     * keeps the same NOTE role and pitch, so it sounds on; its release comes when the key is lifted. */
    uint32_t change = 0;
    uint8_t k, arg;
    for (k = 0; k < BB_NAV_KEYS; k++)
        if ((s->keys_active >> k) & 1u)
            if (bb_nav_key_role(s, k, s->rec_down, &arg) != s->act_role[k] || arg != s->act_arg[k]) change |= 1u << k;
    nav_drop_routes(o);
    nav_release_keys(s, o, change);
    s->step_hold = 0; s->step_hold_used = 0;   /* a held step is dropped with the page, never toggled late */
    s->keys_blocked |= hold;
    o->block_held_keys = 1;
}

/* Enter a page (idempotent). Returns 1 when the visible page changed. */
static int nav_enter(bb_nav_state *s, bb_nav_out *o, uint8_t page)
{
    int changed = s->page != page;
    s->landing = 0;
    if (changed) {
        s->page = page; s->bank = 0; s->root_wait = 0;
        if (page == BB_NAV_FX && s->fx_tab < 5u) s->bank = s->fx_tab;   /* 2.1 P1: FX opens on the tab last used */
        o->message = nav_entry_message(s);
        /* v2.1: section keys open on the page of the playing section */
        if (page == BB_NAV_STUDIO || page == BB_NAV_PATTERN) {
            uint8_t p = s->sec_seen == NONE_IDX ? 0 : (uint8_t)(s->sec_seen / nav_sec_keys(s));
            s->sec_page = p < nav_section_pages(s) ? p : 0;
        }
    }
    o->page_changed |= (uint8_t)changed;
    return changed;
}

static int nav_enter_play(bb_nav_state *s, bb_nav_out *o, uint8_t mode)
{
    int changed = s->play_keys != mode;
    s->play_keys = mode;
    changed |= nav_enter(s, o, BB_NAV_PLAY);
    if (changed) { o->message = nav_entry_message(s); s->root_wait = 0; }
    return changed;
}

static int nav_go_home(bb_nav_state *s, bb_nav_out *o)
{
    int changed;
    if (s->home_page == BB_NAV_PLAY) changed = nav_enter_play(s, o, BB_NAV_PLAY_LIVE);
    else changed = nav_enter(s, o, s->home_page);
    changed |= s->bank != 0;
    s->bank = 0;
    s->landing = 1;
    return changed;
}

static void nav_reset_home(bb_nav_state *s)
{
    s->home_down = s->home_used = s->help = 0;
    s->home_since = 0;
    s->explain = 0; s->help_page = 0;
}

static void nav_finish(const bb_nav_state *s, bb_nav_out *o)
{
    o->page = s->page;
    o->bank = s->bank;
    o->help = s->help || s->rec_help;   /* REC's overlay never blocks keys or chords (key_mode below) */
    o->key_octave = s->key_octave;
    o->scale = s->scale;
    o->root = s->root;
    o->key_mode = (s->help || o->uboot) ? BB_NAV_KEYS_NONE : bb_nav_keys_for(s);
}

static int nav_audible(uint8_t muted, uint8_t solo, unsigned i)
{
    return !((muted >> i) & 1u) && (!solo || ((solo >> i) & 1u));
}

/* ------------------------------------------------------------ public API */

void bb_nav_init(bb_nav_state *s, uint8_t home_page, uint16_t hold_ms)
{
    memset(s, 0, sizeof *s);
    s->home_page = home_page == BB_NAV_PLAY ? BB_NAV_PLAY : BB_NAV_STUDIO;
    s->hold_ms = hold_ms ? hold_ms : 500;
    s->queue_timeout_ms = 8000;
    s->features = BB_NAV_F_ALL;
    s->page = s->home_page;
    s->play_keys = BB_NAV_PLAY_LIVE;
    s->landing = 1;
    s->instrument = BB_NAV_INSTR_DRUM;
    s->sound_banks = 1;
    s->sec_seen = NONE_IDX;
    s->enc_guard_ms = BB_NAV_ENC_GUARD_MS;
}

void bb_nav_set_focus(bb_nav_state *s, uint8_t focus) { s->focus = (uint8_t)(focus & 3u); }

void bb_nav_set_features(bb_nav_state *s, uint8_t features)
{
    s->features = (uint8_t)(features & BB_NAV_F_ALL);
    if (!nav_has(s, BB_NAV_F_SCALE)) { s->scale = BB_NAV_SCALE_OFF; s->root = 0; s->root_wait = 0; }
    if (!nav_has(s, BB_NAV_F_TAP)) { s->tap_run = 0; s->tap_n = 0; }
    /* an active throw keeps its owed THROW_OFF */
}

void bb_nav_set_instrument(bb_nav_state *s, uint8_t instrument, uint8_t sound_banks)
{
    if (instrument >= BB_NAV_INSTRS) instrument = BB_NAV_INSTR_DRUM;
    if (s->instrument != instrument || s->sound_banks != sound_banks) { s->bank = 0; s->root_wait = 0; }
    s->instrument = instrument;
    s->sound_banks = sound_banks;
}

void bb_nav_set_voice(bb_nav_state *s, uint8_t voice)
{
    s->voice = voice > 11 ? 11 : voice;
}

void bb_nav_cancel(bb_nav_state *s) { nav_reset_home(s); }

void bb_nav_goto(bb_nav_state *s, uint8_t page, bb_nav_out *o)
{
    memset(o, 0, sizeof *o);
    if (page >= BB_NAV_PAGES) page = s->home_page;
    nav_reset_home(s);
    if (page == BB_NAV_PLAY) (void)nav_enter_play(s, o, s->play_keys);
    else (void)nav_enter(s, o, page);
    nav_drain(s, o, s->keys_active);
    nav_finish(s, o);
}

void bb_nav_kit_confirmed(bb_nav_state *s, bb_nav_out *o)
{
    memset(o, 0, sizeof *o);
    if (s->page == BB_NAV_KIT) {
        (void)nav_enter(s, o, BB_NAV_PATTERN);
        o->message = BB_NAV_MSG_KIT_CHOSEN;
        nav_drain(s, o, s->keys_active);
    }
    nav_finish(s, o);
}

uint8_t bb_nav_keys_for(const bb_nav_state *s)
{
    switch (s->page) {
    case BB_NAV_STUDIO: return BB_NAV_KEYS_STUDIO;
    case BB_NAV_PLAY:
        if (nav_steps(s)) return BB_NAV_KEYS_STEPS;
        return nav_is_drum(s) ? BB_NAV_KEYS_PADS : BB_NAV_KEYS_NOTES;
    case BB_NAV_KIT: return nav_is_drum(s) ? BB_NAV_KEYS_KIT : BB_NAV_KEYS_NOTES;   /* 2.1 P2: PATCH keys are notes */
    case BB_NAV_PATTERN: return BB_NAV_KEYS_PATTERN;
    case BB_NAV_SOUND: return nav_is_drum(s) ? BB_NAV_KEYS_VOICE : BB_NAV_KEYS_NOTES;
    case BB_NAV_SAVE: return BB_NAV_KEYS_SLOT;
    case BB_NAV_EVOLVE: return BB_NAV_KEYS_EVOLVE;
    case BB_NAV_GLOBAL: case BB_NAV_SONG: return BB_NAV_KEYS_GLOBAL;
    default: return BB_NAV_KEYS_NONE;
    }
}

/* ----------------------------------------------------------------- keys */

static uint8_t nav_semitone(uint8_t key)
{
    return key < 16 ? (uint8_t)(nav_white_semi[key % 7] + 12u * (key / 7u)) : nav_black_semi[key - 16];
}

static uint8_t nav_keyboard(const bb_nav_state *s, uint8_t key, uint8_t *arg)
{
    int note;
    /* v2.1: root is a pitch class (C = 0). White key 1 plays the lowest root
     * at or above the bottom key, so the keyboard keeps its range. */
    if (s->root_wait) { *arg = (uint8_t)((NOTE_BASE + nav_semitone(key)) % 12u); return BB_NAV_K_ROOT; }
    if (s->scale != BB_NAV_SCALE_OFF) {
        uint8_t n = nav_scale_len[s->scale];
        if (key >= 16) return BB_NAV_K_NONE;          /* only in-scale notes */
        note = (int)((s->root + 12u - NOTE_BASE % 12u) % 12u) + nav_scale_iv[s->scale][key % n] + 12 * (key / n);
    } else {
        note = nav_semitone(key);
    }
    *arg = (uint8_t)(NOTE_BASE + 12 * s->key_octave + note);
    return BB_NAV_K_NOTE;
}

int bb_nav_key_is_sound(uint8_t role)
{
#if BB_WITH_USER_KITS
    if (role == BB_NAV_K_LANE) return 1;      /* a BUILD lane plays like a pad: REC held = accent */
#endif
    return role == BB_NAV_K_PAD || role == BB_NAV_K_NOTE || role == BB_NAV_K_STEP;
}

uint8_t bb_nav_key_role(const bb_nav_state *s, uint8_t key, int rec, uint8_t *arg)
{
    const int white = key < 16;
    const uint8_t n = white ? key : (uint8_t)(key - 16);
    *arg = 0;
    if (key >= BB_NAV_KEYS) return BB_NAV_K_NONE;
    switch (s->page) {
    case BB_NAV_STUDIO:
        *arg = n;
        if (s->bank == BB_NAV_HOME_SONGS) {                 /* 2.1 P3b HOME/SONGS (home-keymap.png) */
            if (white) return BB_NAV_K_SONG_PICK;
            *arg = (uint8_t)(n - 4);
            return n >= 4 && n < 7 ? BB_NAV_K_COLLECTION : BB_NAV_K_NONE;
        }
        /* RC2 (Joe 10:32) PERFORM: white 1-16 = the sections (16 a page; REC+white = the focused part only), black B1-B3 =
         * the instruments A / B / DILLO (mute; REC = solo), as on the song list where B1-B3 are the same three */
        if (!white) return n < 3 ? (rec ? BB_NAV_K_INSTR_SOLO : BB_NAV_K_INSTR_MUTE) : BB_NAV_K_NONE;
        *arg = (uint8_t)(s->sec_page * BB_NAV_CONDUCT_KEYS + n);
        return rec ? BB_NAV_K_SECTION : BB_NAV_K_CONDUCT;
    case BB_NAV_PLAY:
        if (nav_steps(s)) {
            *arg = n;
            if (white) return BB_NAV_K_STEP;
            if (nav_is_drum(s)) {
                if (n < 8) return BB_NAV_K_VOICE;
                return n == 8 ? BB_NAV_K_ACCENT_HOLD : n == 9 ? BB_NAV_K_BANK_PREV : BB_NAV_K_BANK_NEXT;
            }
            if (!nav_has(s, BB_NAV_F_STEP_PITCH)) return BB_NAV_K_NONE;
            *arg = (uint8_t)(NOTE_BASE + nav_semitone(key));
            return BB_NAV_K_STEP_PITCH;
        }
        if (!nav_is_drum(s)) return nav_keyboard(s, key, arg);
        if (white) {
            if (n < 11) { *arg = n; return BB_NAV_K_PAD; }
            if (!nav_has(s, BB_NAV_F_FAV_KITS)) return BB_NAV_K_NONE;
            *arg = (uint8_t)(n - 11);
            return rec ? BB_NAV_K_KIT_SLOT_STORE : BB_NAV_K_KIT_SLOT;
        }
        if (n < 4) { *arg = n; return rec ? BB_NAV_K_GROUP_SOLO : BB_NAV_K_GROUP_MUTE; }
        if (n == 4) return nav_has(s, BB_NAV_F_FILL) ? BB_NAV_K_FILL : BB_NAV_K_NONE;
        if (n == 5) return BB_NAV_K_SECTION_PREV;
        if (n == 6) return BB_NAV_K_SECTION_NEXT;
        *arg = (uint8_t)(n - 7);
        return BB_NAV_K_KILL;
    case BB_NAV_SOUND:
        if (!nav_is_drum(s)) return nav_keyboard(s, key, arg);
        if (white && n < 11) { *arg = n; return BB_NAV_K_VOICE; }
        return BB_NAV_K_NONE;
    case BB_NAV_KIT:
        if (!nav_is_drum(s)) return nav_keyboard(s, key, arg);   /* 2.1 P2: Dillo PATCH: keys stay notes */
        *arg = n;
#if BB_WITH_USER_KITS
        if (nav_build_ok(s)) {
            if (s->bank == BB_NAV_KIT_SAVE_AS) return white && n < 4 ? BB_NAV_K_UK_SLOT : BB_NAV_K_NONE;
            if (!white && n == 0) return BB_NAV_K_BUILD;
            if (s->bank == BB_NAV_KIT_BUILD) {
                if (!white && n <= 6) *arg = (uint8_t)(n + 10);     /* black 2-7: LC MC HC CL MA RD */
                return (white && n < 11) || (!white && n <= 6) ? BB_NAV_K_LANE : BB_NAV_K_NONE;
            }
        }
#endif
        return white ? BB_NAV_K_PICK : BB_NAV_K_NONE;
    case BB_NAV_PATTERN:
        *arg = white ? n : (uint8_t)(s->sec_page * BB_NAV_SECTION_KEYS + n);
        return white ? BB_NAV_K_PICK : BB_NAV_K_SECTION;
    case BB_NAV_SAVE:
        *arg = n;
        if (white && n < 4) return rec ? BB_NAV_K_SLOT_LOAD : BB_NAV_K_SLOT;
        return BB_NAV_K_NONE;
    case BB_NAV_GLOBAL:
        /* The ANIMATION / SCOPE / METERS display toggles belonged to the legacy faces and stay gone.
         * 2.1 P1: white 1 and 2 open the MIDI CHANNELS and MIDI CLOCK banks (bb_nav), white 4 the SONG arranger
         * and white 5 the Dillo kit view (drum focus only: it is a drum-layer view), both opened by the firmware.
         * On MIDI and CLOCK, white 1 / 2 switch between them and black 1 goes back to GLOBAL. */
        *arg = n;
        if (s->bank != BB_NAV_GLOBAL_MAIN) {
            if (!white) return n == 0 ? BB_NAV_K_BACK : BB_NAV_K_NONE;
            return n < 2 ? BB_NAV_K_SUBPAGE : BB_NAV_K_NONE;
        }
        if (!white) return BB_NAV_K_NONE;
        if (n < 2 || n == 3) return BB_NAV_K_SUBPAGE;
        return n == 4 && nav_is_drum(s) && BB_NAV_HAS_DILLO_VIEW ? BB_NAV_K_SUBPAGE : BB_NAV_K_NONE;
    case BB_NAV_EVOLVE:
        /* 2.1 P1: only the keys that act are named. EVOLVE: white 1-3 the parts (A, B, DILLO), 7-10 what moves
         * (rhythm, pitch, accent, slide). EUCLID on drums: white 1-16 the lane it writes. RANDOM: none. REC: every
         * white key locks its step, in every mode (the generators keep locked steps too). */
        *arg = n;
        if (white) {
            if (rec) return BB_NAV_K_EVO_LOCK;
            if (s->gen_mode == BB_NAV_MODE_EUCLID) return nav_is_drum(s) ? BB_NAV_K_GEN_VOICE : BB_NAV_K_NONE;
            if (s->gen_mode != BB_NAV_MODE_EVOLVE) return BB_NAV_K_NONE;
            return n < 3 ? BB_NAV_K_EVO_KEY : (n >= 6 && n <= 9) ? BB_NAV_K_EVO_MOVE : BB_NAV_K_NONE;
        }
        if (n == 0) return BB_NAV_K_EVO_FREEZE;
        if (n == 1) return BB_NAV_K_EVO_HOME;
        if (n == 2) return BB_NAV_K_GEN_MODE;
        return BB_NAV_K_NONE;
    case BB_NAV_FX:
        /* 2.1 P1: the five tabs on white 1-5 */
        *arg = n;
        return white && n < 5 ? BB_NAV_K_FX_TAB : BB_NAV_K_NONE;
    case BB_NAV_SONG:
        *arg = n;
        if (s->bank == BB_NAV_SONG_ADD) {
            if (!white) return BB_NAV_K_NONE;
            return n == 0 ? BB_NAV_K_BACK : n == 2 ? BB_NAV_K_SONG_DILLO : BB_NAV_K_NONE;
        }
        if (white) return n < 2 ? BB_NAV_K_SONG_ADD : n == 2 ? BB_NAV_K_SONG_COPY : n == 3 ? BB_NAV_K_SONG_DELETE : BB_NAV_K_NONE;
        return n == 0 ? BB_NAV_K_SONG_UNDO : n == 1 ? BB_NAV_K_SONG_REDO : BB_NAV_K_NONE;
    default:
        return BB_NAV_K_NONE;
    }
}

/* ---------------------------------------------------------------- knobs */

static void nav_knob_roles(const bb_nav_state *s, uint8_t r[4], uint8_t idx[4])
{
    unsigned k;
    int drum = nav_is_drum(s);
    for (k = 0; k < 4; k++) { r[k] = BB_NAV_R_NONE; idx[k] = (uint8_t)k; }
    switch (s->page) {
    case BB_NAV_STUDIO:
        r[0] = BB_NAV_R_MIX_DRUM_A; r[1] = BB_NAV_R_MIX_DRUM_B;
        r[2] = BB_NAV_R_MIX_DILLO; r[3] = BB_NAV_R_FX_RETURN;
        break;
    case BB_NAV_PLAY:
    case BB_NAV_PATTERN:
        if (drum && s->page == BB_NAV_PLAY) {
            r[0] = BB_NAV_R_GROUP_KICK; r[1] = BB_NAV_R_GROUP_SNCL;
            r[2] = BB_NAV_R_GROUP_HATS; r[3] = BB_NAV_R_GROUP_PERC;
        } else if (drum) {
            r[0] = BB_NAV_R_LAYER_LEVEL; r[1] = BB_NAV_R_TUNE;
            r[2] = BB_NAV_R_DECAY; r[3] = BB_NAV_R_SWING;
        } else if (s->instrument == BB_NAV_INSTR_DILLO) {
            r[0] = BB_NAV_R_CUTOFF; r[1] = BB_NAV_R_RESO; r[2] = BB_NAV_R_ENV; r[3] = BB_NAV_R_SYNTH_DECAY;
        } else {
            r[0] = BB_NAV_R_SYNTH_LEVEL; r[1] = BB_NAV_R_CUTOFF; r[2] = BB_NAV_R_RESO; r[3] = BB_NAV_R_ENV;
        }
        break;
    case BB_NAV_KIT:
        r[0] = drum ? BB_NAV_R_LAYER_LEVEL : BB_NAV_R_SYNTH_LEVEL;
#if BB_WITH_USER_KITS
        if (s->bank == BB_NAV_KIT_BUILD) {
            r[0] = BB_NAV_R_UK_LEVEL; r[1] = BB_NAV_R_UK_TUNE; r[2] = BB_NAV_R_UK_DECAY; r[3] = BB_NAV_R_UK_VOICE;
        } else if (s->bank == BB_NAV_KIT_SAVE_AS) {
            r[0] = BB_NAV_R_NONE;               /* SAVE AS: white 1-4 pick the slot, EDIT stores */
        }
#endif
        break;
    case BB_NAV_SOUND:
        if (s->instrument == BB_NAV_INSTR_DRUM_909) {
            for (k = 0; k < 4; k++) { r[k] = BB_NAV_R_909_CONTROL; idx[k] = (uint8_t)(s->bank * 4u + k); }
        } else if (s->instrument == BB_NAV_INSTR_DRUM) {
            const uint8_t *t;
            if (s->bank == 0) {
                r[0] = BB_NAV_R_VOICE_LEVEL; r[1] = BB_NAV_R_TUNE; r[2] = BB_NAV_R_DECAY; r[3] = BB_NAV_R_TONE;
                break;
            }
            t = s->voice == 0 ? nav_colour_bass : s->voice == 1 ? nav_colour_snare : s->voice == 6 ? nav_colour_clap :
                s->voice <= 5 ? nav_colour_tom : nav_colour_metal;
            for (k = 0; k < 4; k++) r[k] = t[k];
        } else if (s->instrument == BB_NAV_INSTR_DILLO) {
            r[0] = BB_NAV_R_ACCENT; r[1] = BB_NAV_R_SYNTH_DRIVE; r[2] = BB_NAV_R_SLIDE; r[3] = BB_NAV_R_WAVE;
        } else if (s->bank == 0) {
            r[0] = BB_NAV_R_SQUARE; r[1] = BB_NAV_R_SAW; r[2] = BB_NAV_R_SUB; r[3] = BB_NAV_R_MOD;
        } else {
            r[0] = BB_NAV_R_SYNTH_LEVEL; r[1] = BB_NAV_R_LFO; r[2] = BB_NAV_R_GLIDE; r[3] = BB_NAV_R_MOD;
        }
        break;
    case BB_NAV_FX:
        if (s->bank < 4) { for (k = 0; k < 4; k++) r[k] = nav_fx_roles[s->bank][k]; }
        else {
            r[0] = BB_NAV_R_SEND_ECHO; r[1] = BB_NAV_R_SEND_SPRING;
            if (drum) { r[2] = BB_NAV_R_VOICE_LEVEL; r[3] = BB_NAV_R_DRIVE; }
        }
        break;
    case BB_NAV_SAVE:
        r[0] = BB_NAV_R_SLOT;
        break;
    case BB_NAV_EVOLVE:
        r[0] = BB_NAV_R_EVO_AMOUNT; r[1] = BB_NAV_R_EVO_EVERY; r[2] = BB_NAV_R_EVO_ROOT; r[3] = BB_NAV_R_EVO_SCALE;
        break;
    case BB_NAV_GLOBAL:
        if (s->bank == BB_NAV_GLOBAL_MAIN) {
            r[0] = BB_NAV_R_BPM; r[1] = BB_NAV_R_SWING; r[2] = BB_NAV_R_AB_BALANCE; r[3] = BB_NAV_R_TRANSPOSE;
        } else {
            for (k = 0; k < 4; k++)
                r[k] = (uint8_t)((s->bank == BB_NAV_GLOBAL_MIDI ? BB_NAV_R_MIDI_CH_A : BB_NAV_R_CLOCK_IN) + k);
        }
        break;
    case BB_NAV_SONG:
        for (k = 0; k < 4; k++)
            r[k] = (uint8_t)((s->bank == BB_NAV_SONG_ADD ? BB_NAV_R_SONG_TARGET : BB_NAV_R_SONG_ROW) + k);
        break;
    default:
        break;
    }
}

void bb_nav_knobs(const bb_nav_state *s, uint8_t roles[4], const char *labels[4])
{
    uint8_t idx[4];
    unsigned k;
    nav_knob_roles(s, roles, idx);
    for (k = 0; k < 4; k++) labels[k] = nav_role_labels[roles[k]];
    /* 2.0: while a drum step is held on STEPS, K4 sets that step's ratchet (bb_nav_input 10a); say so */
    if (s->step_hold && s->page == BB_NAV_PLAY && nav_steps(s) && nav_is_drum(s)) labels[3] = "RATCHET";
}

const char *bb_nav_role_label(uint8_t role) { return role < BB_NAV_ROLES ? nav_role_labels[role] : ""; }
const char *bb_nav_mode_name(uint8_t mode) { return mode < BB_NAV_MODES ? nav_mode_names[mode] : ""; }
const char *bb_nav_page_name(const bb_nav_state *s)
{
    if (s->page == BB_NAV_PLAY) {
        if (s->landing) return nav_is_drum(s) ? "BIG BEAT" : "DILLO";
        if (nav_is_drum(s)) return nav_steps(s) ? "DRUM STEPS" : "DRUM PADS";
        if (s->instrument == BB_NAV_INSTR_DILLO)
            return nav_steps(s) ? "DILLO STEPS" : "DILLO KEYS";
        return nav_steps(s) ? "STEPS" : "PLAY";
    }
    if (s->page == BB_NAV_KIT && !nav_is_drum(s)) return "PATCH";
    return s->page < BB_NAV_PAGES ? nav_page_names[s->page] : "";
}
/* 2.0 navigation (N1): the page with the control that opens it, where the panel's printed name differs.
 * The ALGO and PRESET encoders open PATTERN and KIT; ENV, LFO, GLO and HOME are printed differently from
 * the page they open. FX and SAVE match. */
const char *bb_nav_page_label(const bb_nav_state *s)
{
    switch (s->page) {
    case BB_NAV_STUDIO: return s->landing && s->bank == BB_NAV_HOME_CONDUCT ? "CONDUCTOR" :
                               s->bank == BB_NAV_HOME_CONDUCT ? "PERFORM" : BB_TX(TX_STUDIO_HOME);
    case BB_NAV_PLAY: return bb_nav_page_name(s);
    case BB_NAV_KIT: return nav_is_drum(s) ? BB_TX(TX_KIT_PRESET) : BB_TX(TX_PATCH_PRESET);
    case BB_NAV_PATTERN: return BB_TX(TX_PATTERN_ALGO);
    case BB_NAV_SOUND: return BB_TX(TX_SOUND_ENV);
    case BB_NAV_EVOLVE: return BB_TX(TX_EVOLVE_LFO);
    case BB_NAV_GLOBAL: return BB_TX(TX_GLOBAL_GLO);
    case BB_NAV_SONG: return BB_TX(TX_SONG_HOME);
    default: return bb_nav_page_name(s);
    }
}
const char *bb_nav_message_text(uint8_t msg)
{
    if (msg == BB_NAV_MSG_AUDIO_FAULT)
        return "AUDIO FAULT / LOAD BLOCKED";
    if (msg == BB_NAV_MSG_SECTIONS_4 || msg == BB_NAV_MSG_SECTIONS_5)   /* RC1: pages 4-5 (app texts, no BBTX change) */
        return msg == BB_NAV_MSG_SECTIONS_4 ? "SECTIONS 34-44" : "SECTIONS 45-55";
    if (msg >= BB_NAV_MSG_PERFORM_1 && msg <= BB_NAV_MSG_PERFORM_4) {   /* RC2: PERFORM's white-key pages */
        static const char *const t[4] = {"SECTIONS 1-16", "SECTIONS 17-32", "SECTIONS 33-48", "SECTIONS 49-64"};
        return t[msg - BB_NAV_MSG_PERFORM_1];
    }
    return msg < BB_NAV_MESSAGES && nav_messages[msg] ? bb_tx(nav_messages[msg]) : "";
}
const char *bb_nav_scale_name(uint8_t scale) { return scale < BB_NAV_SCALES ? nav_scale_names[scale] : ""; }

const char *bb_nav_help_word(const bb_nav_state *s)
{
    if (s->page == BB_NAV_PLAY) return bb_nav_page_name(s);
    if (s->page == BB_NAV_STUDIO) return s->bank == BB_NAV_HOME_SONGS ? "SONGS" : s->landing ? "CONDUCTOR" : "PERFORM";
    if (s->page != BB_NAV_KIT) return 0;
#if BB_WITH_USER_KITS
    if (s->bank) return bb_nav_bank_name(s);
#endif
    return nav_is_drum(s) ? 0 : "PATCH";
}
unsigned bb_nav_status_name(const bb_nav_state *s, char *out, unsigned n)
{
    /* With no text block a translated label would read HELP UNAVAILABLE, so
     * use the page's own name instead.  The three instrument landings are
     * app-resident literals, however: CONDUCTOR must not regress to STUDIO
     * merely because contextual help is unavailable. */
    const char *b = bb_nav_bank_name(s);
    int landing_name = s->landing && (s->page == BB_NAV_STUDIO || s->page == BB_NAV_PLAY);
    const char *p = b[0] || (!bb_tx_ready() && !landing_name) ? bb_nav_page_name(s) : bb_nav_page_label(s);
    unsigned k = 0;
    if (!n) return 0;
    while (*p && k + 1u < n) out[k++] = *p++;
    if (b[0] && k + 3u < n) {
        out[k++] = ' '; out[k++] = ' ';
        while (*b && k + 1u < n) out[k++] = *b++;
    }
    out[k] = 0;
    return k;
}
const char *bb_nav_bank_name(const bb_nav_state *s)
{
    if (s->page == BB_NAV_FX && s->bank < 5) return nav_fx_bank_names[s->bank];
    if (s->page == BB_NAV_STUDIO && s->bank) return "SONGS";   /* 2.1 P3b: HOME/SONGS (CONDUCT keeps "STUDIO · HOME") */
    if (s->page == BB_NAV_GLOBAL && s->bank) return s->bank == BB_NAV_GLOBAL_MIDI ? "MIDI" : "CLOCK";
    if (s->page == BB_NAV_SONG && s->bank) return "ADD";
#if BB_WITH_USER_KITS
    if (s->page == BB_NAV_KIT && s->bank) return s->bank == BB_NAV_KIT_BUILD ? "BUILD" : "SAVE AS";
#endif
    if (s->page != BB_NAV_SOUND) return "";
    switch (s->instrument) {
    case BB_NAV_INSTR_DRUM: return s->bank ? "COLOUR" : "BASIC";
    case BB_NAV_INSTR_DRUM_909: return "PAGE";
    case BB_NAV_INSTR_TELEGRAM: return s->bank ? "MOD" : "OSC";
    default: return "TONE";
    }
}

static int nav_save_blocked(const bb_nav_facts *f) { return f && (f->saving || f->running); }

/* What EDIT does on this page, shown on screen. "" = nothing (LED dark). 2.1 P1: the caption is the EDIT button's
 * role word from the one table (nav_btn_word), so the screen, the LED and HELP agree; FX has none (the 2.0 NEXT
 * alias is gone: white 1-5 choose the tab), EVOLVE reads CAPTURE and EUCLID / RANDOM read APPLY. */
static uint8_t nav_button_led_rule(const bb_nav_state *s, const bb_nav_facts *f, uint8_t b, int rec);
static const char *nav_btn_word(uint8_t role);
const char *bb_nav_edit_label(const bb_nav_state *s, const bb_nav_facts *f)
{
    uint8_t r = bb_nav_button_role(s, f, BB_NAV_B_EDIT, 0);
    if (r == BB_NAV_BR_SONG_EDIT && f && (f->song & BB_NAV_SONG_F_DELETE)) return "DELETE";
    if (r == BB_NAV_BR_SONG_EDIT && f && !f->running) return "LOAD";
    return nav_button_led_rule(s, f, BB_NAV_B_EDIT, 0) == BB_NAV_LED_DARK ? "" : nav_btn_word(r);
}

/* ------------------------------------------------------------- buttons */

static void nav_tap(bb_nav_state *s, bb_nav_out *o, uint32_t now)
{
    uint32_t iv = now - s->tap_last;
    unsigned i, sum = 0;
    if (s->tap_run && iv < 100) return;                 /* contact bounce */
    o->actions |= BB_NAV_A_TAP;
    o->message = BB_NAV_MSG_TAP;
    if (!s->tap_run || iv > 2000) {                     /* new sequence */
        s->tap_run = 1; s->tap_n = 0; s->tap_last = now;
        return;
    }
    s->tap_iv[2] = s->tap_iv[1]; s->tap_iv[1] = s->tap_iv[0]; s->tap_iv[0] = (uint16_t)iv;
    if (s->tap_n < 3) s->tap_n++;
    s->tap_last = now;
    for (i = 0; i < s->tap_n; i++) sum += s->tap_iv[i];
    {
        uint32_t t = (600000u * s->tap_n + sum / 2u) / sum;    /* BPM x 10 */
        o->tempo_x10 = (uint16_t)(t < 300u ? 300u : t > 3000u ? 3000u : t);
    }
    o->actions |= BB_NAV_A_TEMPO_SET;
}

static void nav_octave(bb_nav_state *s, bb_nav_out *o, int dir)
{
    int next = s->key_octave + dir;
    if (next < -2 || next > 2) { o->message = BB_NAV_MSG_OCTAVE_LIMIT; return; }
    s->key_octave = (int8_t)next;
    o->oct = BB_NAV_OCT_KEY_OCTAVE;
    o->oct_dir = (int8_t)dir;
    o->actions |= BB_NAV_A_KEY_OCTAVE;
}

/* v2.1: page the section keys (STUDIO OCT-/+, PATTERN REC+OCT-/+). A change
 * moves every black key's meaning, so the scan is drained (returns 1). */
static int nav_section_page(bb_nav_state *s, bb_nav_out *o, int dir)
{
    int next = (int)s->sec_page + dir;
    if (next < 0 || next >= (int)nav_section_pages(s)) return 0;
    s->sec_page = (uint8_t)next;
    o->message = (uint8_t)(nav_perform(s) ? BB_NAV_MSG_PERFORM_1 + s->sec_page :
                           s->sec_page < 3u ? BB_NAV_MSG_SECTIONS_1 + s->sec_page : BB_NAV_MSG_SECTIONS_4 + s->sec_page - 3u);
    return 1;
}

/* v2.1 held-layer encoder latch. The job an encoder has this scan; any part
 * that changes what one more detent would do is in it. */
static uint32_t nav_enc_job(const bb_nav_state *s, unsigned e, int rec)
{
    if (s->help) return 0xffffffffu;
    switch (e) {
    case BB_NAV_E_SELECT: case BB_NAV_E_ALGO: return rec ? 2u : 1u;
    case BB_NAV_E_PRESET:
        if (!rec) return 1u;
        if (nav_is_drum(s)) return 2u | (uint32_t)s->focus << 4;
        return (s->page == BB_NAV_PLAY && nav_steps(s)) ? 3u : 4u;
    default: {
        uint8_t r[4], idx[4];
        unsigned k = e - BB_NAV_E_K1;
        nav_knob_roles(s, r, idx);
        return 0x10000u | r[k] | (uint32_t)idx[k] << 8 | (uint32_t)s->focus << 14 |
               (s->page == BB_NAV_SOUND ? (uint32_t)s->voice << 17 : 0u) | (uint32_t)s->page << 22;
    }
    }
}

/* Returns the detents this scan may use. An encoder whose job changed while it
 * was turning (a detent within enc_guard_ms) keeps dropping detents until it
 * has been still for enc_guard_ms; a still encoder takes its new job at once. */
static void nav_enc_latch(bb_nav_state *s, const bb_nav_in *in, int rec, int16_t e[BB_NAV_ENCODERS])
{
    unsigned i;
    const uint32_t now = in->now_ms | 1u, g = s->enc_guard_ms;   /* stored times are ms|1: 0 = none */
    for (i = 0; i < BB_NAV_ENCODERS; i++) {
        const uint32_t job = nav_enc_job(s, i, rec);
        e[i] = in->enc[i];
        if (job != s->enc_sig[i]) {
            if (g && s->enc_moved[i] && (uint32_t)(now - s->enc_moved[i]) < g) s->enc_hold[i] = now;
            s->enc_sig[i] = job;
        }
        if (s->enc_hold[i] && (uint32_t)(now - s->enc_hold[i]) >= g) s->enc_hold[i] = 0;
        if (in->enc[i]) {
            s->enc_moved[i] = now;
            if (s->enc_hold[i]) { s->enc_hold[i] = now; s->enc_dropped++; e[i] = 0; }
        }
    }
}

/* Priority order when several buttons arrive in one scan: first one wins. */
static const uint8_t nav_button_order[] = {
    BB_NAV_B_FX, BB_NAV_B_GLO, BB_NAV_B_SAVE, BB_NAV_B_ENV, BB_NAV_B_SEQ, BB_NAV_B_ARP,
    BB_NAV_B_EDIT, BB_NAV_B_SCL, BB_NAV_B_LFO, BB_NAV_B_OCTDN, BB_NAV_B_OCTUP
};

/* 2.1 P1: the button resolver. What button b does here, decided once; nav_button performs it, the LEDs, the EDIT
 * caption and HELP read it. The old dry-run of the input path stays as a host cross-check (nav_p1 test). */
static int nav_page_buttons(const bb_nav_state *s) { return s->page != BB_NAV_SONG && !nav_midi(s); }
static int nav_oct_room(const bb_nav_state *s, int dir, int keys)
{
    if (keys) { int next = s->key_octave + dir; return next >= -2 && next <= 2; }
    return 1;
}
uint8_t bb_nav_button_role(const bb_nav_state *s, const bb_nav_facts *f, uint8_t b, int rec)
{
    const int drum = nav_is_drum(s);
    if (b == BB_NAV_B_HOME) return rec ? BB_NAV_BR_PANIC : BB_NAV_BR_HOME;
    if (b == BB_NAV_B_PLAY) return !rec ? BB_NAV_BR_TRANSPORT : nav_has(s, BB_NAV_F_RESTART) ? BB_NAV_BR_TRANSPORT : BB_NAV_BR_NONE;
    if (b == BB_NAV_B_REC) {
#if BB_NAV_HAS_SONG_RECORD
        if (s->page == BB_NAV_SONG && s->bank == BB_NAV_SONG_ROWS) return BB_NAV_BR_SONG_REC;   /* a tap records */
#endif
        return BB_NAV_BR_REC;
    }
    if (s->page == BB_NAV_SONG) {
        /* SONG keeps 2.0's controls: HOME and GLO leave, EDIT acts, PLAY plays the song; the rest does nothing */
        if (b == BB_NAV_B_GLO)                                /* REC+GLO taps tempo on every page */
            return !rec ? BB_NAV_BR_LEAVE : nav_has(s, BB_NAV_F_TAP) ? BB_NAV_BR_TAP : BB_NAV_BR_NONE;
        if (b != BB_NAV_B_EDIT) return BB_NAV_BR_NONE;
        if (rec) return BB_NAV_BR_MUTE;                       /* REC+EDIT: mute, as on every page but PLAY */
        return s->bank == BB_NAV_SONG_ADD ? BB_NAV_BR_SONG_ADD : BB_NAV_BR_SONG_EDIT;
    }
    if (nav_midi(s)) {
        if (b == BB_NAV_B_GLO)                                /* REC+GLO taps tempo on every page */
            return !rec ? BB_NAV_BR_LEAVE : nav_has(s, BB_NAV_F_TAP) ? BB_NAV_BR_TAP : BB_NAV_BR_NONE;
        return b == BB_NAV_B_EDIT || b == BB_NAV_B_SAVE ? BB_NAV_BR_GLOBALS_SAVE : BB_NAV_BR_NONE;
    }
    switch (b) {
    case BB_NAV_B_FX:
        if (!rec) return BB_NAV_BR_FX;
        return nav_has(s, BB_NAV_F_THROW) ? BB_NAV_BR_THROW : BB_NAV_BR_NONE;
    case BB_NAV_B_GLO:
        if (!rec) return BB_NAV_BR_GLOBAL;
        return nav_has(s, BB_NAV_F_TAP) ? BB_NAV_BR_TAP : BB_NAV_BR_NONE;
    case BB_NAV_B_SAVE: return rec ? BB_NAV_BR_QUICK_SAVE : BB_NAV_BR_SAVE_PAGE;
    case BB_NAV_B_ENV:
        if (!rec) return BB_NAV_BR_SOUND;
        return drum ? BB_NAV_BR_NONE : BB_NAV_BR_REVERT;   /* a drum voice has no revert yet: dark, not a promise */
    case BB_NAV_B_SEQ:
        if (rec && s->page == BB_NAV_PLAY && drum) return BB_NAV_BR_RECORD;
        return rec ? BB_NAV_BR_STEP_BANK : BB_NAV_BR_STEPS;
    case BB_NAV_B_ARP: return rec ? BB_NAV_BR_LAYER : BB_NAV_BR_LIVE;
    case BB_NAV_B_EDIT:
        if (rec) return s->page == BB_NAV_PLAY ? BB_NAV_BR_UNDO : BB_NAV_BR_MUTE;
        switch (s->page) {
        case BB_NAV_STUDIO: return s->bank == BB_NAV_HOME_SONGS ? BB_NAV_BR_CHOOSE : BB_NAV_BR_SONGS;   /* 2.1 P3b */
        case BB_NAV_KIT:
#if BB_WITH_USER_KITS
            if (s->bank == BB_NAV_KIT_BUILD) return BB_NAV_BR_SAVE_AS;
            if (s->bank == BB_NAV_KIT_SAVE_AS) return BB_NAV_BR_STORE;
#endif
            return drum ? BB_NAV_BR_CHOOSE : BB_NAV_BR_LOAD;
        case BB_NAV_PATTERN: return BB_NAV_BR_CHOOSE;
        case BB_NAV_SAVE: return BB_NAV_BR_STORE;
        case BB_NAV_EVOLVE: return f && f->gen_mode != BB_NAV_MODE_EVOLVE ? BB_NAV_BR_APPLY : BB_NAV_BR_CAPTURE;
        case BB_NAV_PLAY: return !drum && nav_steps(s) ? BB_NAV_BR_TYPE : BB_NAV_BR_NONE;
        default: return BB_NAV_BR_NONE;                     /* FX: white 1-5 choose the tab */
        }
    case BB_NAV_B_SCL:
        if (drum) return rec ? BB_NAV_BR_NONE : BB_NAV_BR_SWING;
        if (!nav_has(s, BB_NAV_F_SCALE)) return BB_NAV_BR_NONE;
        return rec ? BB_NAV_BR_ROOT : BB_NAV_BR_SCALE;
    case BB_NAV_B_LFO:
        if (rec) return BB_NAV_BR_EVO_TOGGLE;
        return s->page == BB_NAV_EVOLVE ? BB_NAV_BR_MODE : BB_NAV_BR_EVOLVE;
    case BB_NAV_B_OCTDN:
    case BB_NAV_B_OCTUP: {
        const int dir = b == BB_NAV_B_OCTUP ? 1 : -1;
        switch (s->page) {
        case BB_NAV_PLAY:
            if (nav_steps(s)) return BB_NAV_BR_OCT_STEPS;
            return !drum && nav_oct_room(s, dir, 1) ? BB_NAV_BR_OCT_KEYS : BB_NAV_BR_NONE;
        case BB_NAV_STUDIO: {
            int next = (int)s->sec_page + dir;
            if (s->bank == BB_NAV_HOME_SONGS) return BB_NAV_BR_OCT_LIST;   /* 2.1 P3b: the song list page */
            return next >= 0 && next < (int)nav_section_pages(s) ? BB_NAV_BR_OCT_SECTIONS : BB_NAV_BR_NONE;
        }
        case BB_NAV_PATTERN:
            if (rec) {
                int next = (int)s->sec_page + dir;
                return next >= 0 && next < (int)nav_section_pages(s) ? BB_NAV_BR_OCT_SECTIONS : BB_NAV_BR_NONE;
            }
            return BB_NAV_BR_OCT_LIST;
        case BB_NAV_KIT:
            if (!drum) return nav_oct_room(s, dir, 1) ? BB_NAV_BR_OCT_KEYS : BB_NAV_BR_NONE;   /* 2.1 P2: as PLAY */
#if BB_WITH_USER_KITS
            if (s->bank) return BB_NAV_BR_NONE;                 /* BUILD and SAVE AS are one page each */
#endif
            return BB_NAV_BR_OCT_LIST;
        case BB_NAV_SOUND: case BB_NAV_FX: {
            int next = (int)s->bank + dir, count = bb_nav_bank_count(s, s->page);
            if (count < 2) return s->page == BB_NAV_SOUND && !drum && nav_oct_room(s, dir, 1) ? BB_NAV_BR_OCT_KEYS : BB_NAV_BR_NONE;
            return next >= 0 && next < count ? BB_NAV_BR_OCT_BANK : BB_NAV_BR_NONE;
        }
        default: return BB_NAV_BR_NONE;
        }
    }
    default: return BB_NAV_BR_NONE;
    }
}

/* Its light. "Now" checks (SAVE while running, SONG with no rows, the arranger not ready) make it dark, and a
 * dark button never acts: nav_button only says why (STOP FIRST, ...). */
static uint8_t nav_button_led_rule(const bb_nav_state *s, const bb_nav_facts *f, uint8_t b, int rec)
{
    const uint8_t r = bb_nav_button_role(s, f, b, rec), song = f ? f->song : 0xffu;
    switch (r) {
    case BB_NAV_BR_NONE: return BB_NAV_LED_DARK;
    case BB_NAV_BR_FX: return s->page == BB_NAV_FX ? BB_NAV_LED_LIT : BB_NAV_LED_DIM;
    case BB_NAV_BR_GLOBAL: case BB_NAV_BR_LEAVE: return s->page == BB_NAV_GLOBAL ? BB_NAV_LED_LIT : BB_NAV_LED_DIM;
    case BB_NAV_BR_SAVE_PAGE: return s->page == BB_NAV_SAVE ? BB_NAV_LED_LIT : BB_NAV_LED_DIM;
    case BB_NAV_BR_SOUND: return s->page == BB_NAV_SOUND ? BB_NAV_LED_LIT : BB_NAV_LED_DIM;
    case BB_NAV_BR_EVOLVE: case BB_NAV_BR_EVO_TOGGLE: return f && f->evolve_on ? BB_NAV_LED_LIT : BB_NAV_LED_DIM;
    case BB_NAV_BR_MODE: return BB_NAV_LED_LIT;
    case BB_NAV_BR_LIVE: return s->page == BB_NAV_PLAY && !nav_steps(s) ? BB_NAV_LED_LIT : BB_NAV_LED_DIM;
    case BB_NAV_BR_STEPS: return s->page == BB_NAV_PLAY && nav_steps(s) ? BB_NAV_LED_LIT : BB_NAV_LED_DIM;
    case BB_NAV_BR_QUICK_SAVE: case BB_NAV_BR_STORE: return nav_save_blocked(f) ? BB_NAV_LED_DARK : BB_NAV_LED_DIM;
    case BB_NAV_BR_SONG_EDIT:
        if (song & BB_NAV_SONG_F_DELETE) return BB_NAV_LED_BREATH;                      /* the delete waits for you */
        return (song & BB_NAV_SONG_F_ROWS) ? BB_NAV_LED_DIM : BB_NAV_LED_DARK;
    case BB_NAV_BR_SONG_REC: return (song & BB_NAV_SONG_F_ROWS) ? BB_NAV_LED_DIM : BB_NAV_LED_DARK;
    case BB_NAV_BR_SWING: return f && f->swing_on ? BB_NAV_LED_LIT : BB_NAV_LED_DIM;
    case BB_NAV_BR_SCALE: case BB_NAV_BR_ROOT:
        if (s->root_wait) return BB_NAV_LED_BREATH;
        return s->scale != BB_NAV_SCALE_OFF ? BB_NAV_LED_LIT : BB_NAV_LED_DIM;
    case BB_NAV_BR_HOME:
        return s->help || s->landing ? BB_NAV_LED_LIT : BB_NAV_LED_DIM;
    case BB_NAV_BR_TRANSPORT:
        if (!f) return BB_NAV_LED_DIM;
        return (f->queued & BB_NAV_Q_START) ? BB_NAV_LED_BREATH : f->running ? BB_NAV_LED_LIT : BB_NAV_LED_DIM;
    case BB_NAV_BR_REC: return s->rec_down ? BB_NAV_LED_LIT : BB_NAV_LED_DIM;
    case BB_NAV_BR_CHOOSE: case BB_NAV_BR_LOAD:
        if (f && (s->page == BB_NAV_KIT || s->page == BB_NAV_PATTERN)) {
            uint8_t bit = s->page == BB_NAV_KIT ? 1u : 2u;
            if (s->q_late & bit) return BB_NAV_LED_BREATH;                                 /* retry */
            if (f->list_cursor != NONE_IDX && f->list_cursor != f->list_playing &&
                !((f->queued & (bit == 1u ? BB_NAV_Q_KIT : BB_NAV_Q_PATTERN)) && f->list_queued == f->list_cursor))
                return BB_NAV_LED_LIT;                                                     /* confirm waits */
        }
        return BB_NAV_LED_DIM;
    default: return BB_NAV_LED_DIM;
    }
}

/* Button role descriptors: the word the EDIT caption shows and the HELP text. Short texts (under 12 bytes) stay in
 * the app, as Lane H's rule keeps them (BB_NAV_HX); longer ones are BBTX texts. */
#define BB_NAV_HX 0x8000u
static const char nav_short[][16] = {
    "", "Effects", "Freeze", "Redo", "Copy row", "Delete row", "Sound on pause", "Tempo", "Key octave", "Lock a step",
    "Scale lock", "Next mode", "Play drums", "Play notes", "Step pitch", "Effect tab", "Set root", "MIDI clock",
    "Mute a group", "Solo a group", "Add a section", "Record a take", "Save globals", "Section page", "Next section", "Source bank", "Kit builder", "Lane to write", "Song arranger", "MIDI channels", "Dillo kit view", "Layer on / off", "Swing on / off", "Mute this part", "Keep the sound", "Dillo in or out",
    "Band, next bar", "Song to choose", "Takes the song", "Songs/eras/acid", "Song list"
};
enum { HX_NONE, HX_EFFECTS, HX_FREEZE, HX_REDO, HX_COPY_ROW, HX_DELETE_ROW, HX_PATCH, HX_TEMPO, HX_KEY_OCTAVE,
       HX_LOCK_STEP, HX_SCALE_LOCK, HX_NEXT_MODE, HX_PLAY_DRUMS, HX_PLAY_NOTES, HX_STEP_PITCH, HX_EFFECT_TAB,
       HX_SET_ROOT, HX_MIDI_CLOCK,
       HX_MUTE_GROUP, HX_SOLO_GROUP, HX_ADD_SECTION, HX_RECORD_TAKE, HX_SAVE_GLOBALS, HX_SECTION_PAGE, HX_NEXT_SECTION, HX_SOURCE_BANK, HX_KIT_BUILDER, HX_LANE_TO_WRITE, HX_SONG_ARRANGER, HX_MIDI_CHANNELS, HX_DILLO_KIT_VIEW, HX_LAYER_ON_OFF, HX_SWING_ON_OFF, HX_MUTE_THIS_PART, HX_LOAD_THE_PATCH, HX_DILLO_IN_OR_OUT,
       HX_ALL_PARTS, HX_SONG_PICK, HX_TAKES, HX_COLLECTIONS, HX_SONG_LIST, HX_COUNT };
#define HX(n) (BB_NAV_HX | (n))
_Static_assert(sizeof nav_short / sizeof nav_short[0] == HX_COUNT, "nav_short and its names differ");
typedef struct { char word[8]; uint16_t help; } bb_nav_btn_desc;
static const bb_nav_btn_desc nav_btn_desc[BB_NAV_BUTTON_ROLES] = {
    [BB_NAV_BR_NONE] = {"", 0},
    [BB_NAV_BR_FX] = {"FX", HX(HX_EFFECTS)}, [BB_NAV_BR_GLOBAL] = {"GLOBAL", TX_TEMPO_SWING_KEY},
    [BB_NAV_BR_SAVE_PAGE] = {"SAVE", TX_H_SAVE_LOAD_SCENES}, [BB_NAV_BR_SOUND] = {"SOUND", TX_SOUND_CONTROLS},
    [BB_NAV_BR_EVOLVE] = {"EVOLVE", TX_H_EVOLVE_EUCLID_RANDOM},
    [BB_NAV_BR_LIVE] = {"PLAY", TX_PLAY_KEYS}, [BB_NAV_BR_STEPS] = {"STEPS", TX_WRITE_STEPS},
    [BB_NAV_BR_THROW] = {"THROW", TX_DUB_THROW_HOLD}, [BB_NAV_BR_TAP] = {"TAP", TX_TAP_TEMPO_2},
    [BB_NAV_BR_QUICK_SAVE] = {"SAVE", TX_QUICK_SAVE}, [BB_NAV_BR_REVERT] = {"REVERT", TX_REVERT_DILLO},
    [BB_NAV_BR_RECORD] = {"RECORD", TX_RECORD_DRUMS}, [BB_NAV_BR_STEP_BANK] = {"BANK", TX_H_STEPS_NEXT_BANK},
    [BB_NAV_BR_LAYER] = {"LAYER", HX(HX_LAYER_ON_OFF)}, [BB_NAV_BR_UNDO] = {"UNDO", TX_UNDO_PATTERN_EDIT},
    [BB_NAV_BR_MUTE] = {"MUTE", HX(HX_MUTE_THIS_PART)},
    [BB_NAV_BR_SONG] = {"SONG", HX(HX_SONG_ARRANGER)}, [BB_NAV_BR_CHOOSE] = {"CHOOSE", TX_CONFIRM},
    [BB_NAV_BR_LOAD] = {"KEEP", HX(HX_LOAD_THE_PATCH)}, [BB_NAV_BR_SAVE_AS] = {"SAVE AS", TX_SAVE_AS_USER_1_4},
    [BB_NAV_BR_STORE] = {"STORE", TX_STORE}, [BB_NAV_BR_CAPTURE] = {"CAPTURE", TX_H_CAPTURE_THE_PATTERN},
    [BB_NAV_BR_APPLY] = {"APPLY", TX_H_APPLY_THE_GENERATOR}, [BB_NAV_BR_TYPE] = {"TYPE", TX_STEP_TYPE},
    [BB_NAV_BR_SWING] = {"SWING", HX(HX_SWING_ON_OFF)}, [BB_NAV_BR_SCALE] = {"SCALE", HX(HX_SCALE_LOCK)},
    [BB_NAV_BR_ROOT] = {"ROOT", TX_H_ROOT_NEXT_KEY},
    [BB_NAV_BR_MODE] = {"MODE", HX(HX_NEXT_MODE)}, [BB_NAV_BR_EVO_TOGGLE] = {"EVOLVE", TX_EVOLVE_ON_OFF},
    [BB_NAV_BR_OCT_STEPS] = {"BANK", TX_STEP_BANK}, [BB_NAV_BR_OCT_KEYS] = {"OCTAVE", HX(HX_KEY_OCTAVE)},
    [BB_NAV_BR_OCT_LIST] = {"PAGE", TX_LIST_PAGE}, [BB_NAV_BR_OCT_SECTIONS] = {"PAGE", HX(HX_SECTION_PAGE)},
    [BB_NAV_BR_OCT_BANK] = {"TAB", TX_H_PREVIOUS_NEXT_TAB},
    [BB_NAV_BR_HOME] = {"HOME", TX_H_TAP_BACK_HOLD_HELP}, [BB_NAV_BR_PANIC] = {"PANIC", TX_ALL_NOTES_OFF_2},
    [BB_NAV_BR_TRANSPORT] = {"PLAY", TX_START_STOP}, [BB_NAV_BR_REC] = {"REC", TX_H_HOLD_REC_CHORDS},
    [BB_NAV_BR_SONG_EDIT] = {"CUE", TX_H_CUE_OR_LOAD_THE_ROW}, [BB_NAV_BR_SONG_ADD] = {"ADD", HX(HX_ADD_SECTION)},
    [BB_NAV_BR_SONG_REC] = {"RECORD", HX(HX_RECORD_TAKE)}, [BB_NAV_BR_GLOBALS_SAVE] = {"SAVE", HX(HX_SAVE_GLOBALS)},
    [BB_NAV_BR_LEAVE] = {"BACK", TX_BACK_ONE_LEVEL}, [BB_NAV_BR_SONGS] = {"SONGS", HX(HX_SONG_LIST)},
};
static const char *nav_btn_word(uint8_t role) { return role < BB_NAV_BUTTON_ROLES ? nav_btn_desc[role].word : ""; }
uint16_t bb_nav_button_help(uint8_t role) { return role < BB_NAV_BUTTON_ROLES ? nav_btn_desc[role].help : 0; }

/* Returns 1 when the scan must be drained (page, key mode or knob bank changed). */
static int nav_button(bb_nav_state *s, bb_nav_out *o, uint8_t b, int rec, const bb_nav_in *in)
{
    const bb_nav_facts *f = &in->facts;
    const int dir = b == BB_NAV_B_OCTUP ? 1 : -1;
    const uint8_t role = bb_nav_button_role(s, f, b, rec);
    uint8_t count;
    if (nav_button_led_rule(s, f, b, rec) == BB_NAV_LED_DARK) {
        /* dark: nothing happens; say why where there is a reason */
        if (role == BB_NAV_BR_QUICK_SAVE || role == BB_NAV_BR_STORE) o->message = f->saving ? BB_NAV_MSG_WAIT : BB_NAV_MSG_STOP_FIRST;
        else if (role == BB_NAV_BR_NONE && nav_page_buttons(s) &&
                 ((rec && (b == BB_NAV_B_FX || b == BB_NAV_B_GLO || (b == BB_NAV_B_ENV && nav_is_drum(s)))) ||
                  (b == BB_NAV_B_SCL && !nav_is_drum(s))))
            o->message = BB_NAV_MSG_NOT_IN_BUILD;
        else if (role == BB_NAV_BR_NONE && (b == BB_NAV_B_OCTDN || b == BB_NAV_B_OCTUP) && !nav_is_drum(s) &&
                 ((s->page == BB_NAV_PLAY && !nav_steps(s)) || s->page == BB_NAV_SOUND))
            o->message = BB_NAV_MSG_OCTAVE_LIMIT;
        return 0;
    }
    switch (role) {
    case BB_NAV_BR_FX: return nav_enter(s, o, BB_NAV_FX);
    case BB_NAV_BR_THROW:
        if (!s->throw_on) { s->throw_on = 1; o->actions |= BB_NAV_A_THROW_ON; o->message = BB_NAV_MSG_THROW; }
        if (!(in->held & B(FX))) { s->throw_on = 0; o->actions |= BB_NAV_A_THROW_OFF; }  /* tap inside one scan */
        return 0;
    case BB_NAV_BR_GLOBAL: return nav_enter(s, o, BB_NAV_GLOBAL);
    case BB_NAV_BR_TAP: nav_tap(s, o, in->now_ms); return 0;
    case BB_NAV_BR_SAVE_PAGE: return nav_enter(s, o, BB_NAV_SAVE);
    case BB_NAV_BR_QUICK_SAVE: o->actions |= BB_NAV_A_QUICK_SAVE; return 0;
    case BB_NAV_BR_REVERT: o->actions |= BB_NAV_A_REVERT; return 0;
    case BB_NAV_BR_SOUND: return nav_enter(s, o, BB_NAV_SOUND);
    case BB_NAV_BR_RECORD: o->actions |= BB_NAV_A_RECORD; return 0;
    case BB_NAV_BR_STEP_BANK: o->actions |= BB_NAV_A_STEP_BANK_NEXT; return nav_enter_play(s, o, BB_NAV_PLAY_STEPS);
    case BB_NAV_BR_STEPS: return nav_enter_play(s, o, BB_NAV_PLAY_STEPS);
    case BB_NAV_BR_LAYER: o->actions |= BB_NAV_A_LAYER_ENABLE; return 0;
    case BB_NAV_BR_LIVE: return nav_enter_play(s, o, BB_NAV_PLAY_LIVE);
    case BB_NAV_BR_UNDO: o->actions |= BB_NAV_A_UNDO; return 0;
    case BB_NAV_BR_MUTE: o->actions |= BB_NAV_A_MUTE; return 0;
    case BB_NAV_BR_SONG: o->actions |= BB_NAV_A_HOME_AGAIN; return 0;   /* EDIT: SONG (ARP/SEQ open PLAY) */
    case BB_NAV_BR_SONGS:                                     /* 2.1 P3b: HOME/CONDUCT -> HOME/SONGS */
        s->bank = BB_NAV_HOME_SONGS; s->landing = 0; o->page_changed = 1;
        return 1;
#if BB_WITH_USER_KITS
    case BB_NAV_BR_SAVE_AS:
        s->bank = BB_NAV_KIT_SAVE_AS; o->actions |= BB_NAV_A_BUILD; o->message = BB_NAV_MSG_SAVE_AS;
        return 1;
#endif
    case BB_NAV_BR_STORE:
#if BB_WITH_USER_KITS
        if (s->page == BB_NAV_KIT) { o->actions |= BB_NAV_A_UK_STORE; return 0; }
#endif
        o->actions |= BB_NAV_A_SAVE_STORE;
        return 0;
    case BB_NAV_BR_CHOOSE: case BB_NAV_BR_LOAD:
        if (s->page == BB_NAV_KIT) {
            o->actions |= BB_NAV_A_KIT_CONFIRM;
            s->q_since[0] = 0; s->q_late &= (uint8_t)~1u;      /* retry restarts the wait */
        } else {
            o->actions |= BB_NAV_A_PATTERN_CONFIRM;
            s->q_since[1] = 0; s->q_late &= (uint8_t)~2u;
        }
        return 0;
    case BB_NAV_BR_CAPTURE: case BB_NAV_BR_APPLY: o->actions |= BB_NAV_A_EVO_CAPTURE; return 0;
    case BB_NAV_BR_TYPE: o->actions |= BB_NAV_A_EDIT_TYPE; return 0;
    case BB_NAV_BR_SWING:
        o->actions |= BB_NAV_A_SWING_TOGGLE;
        o->message = f->swing_on ? BB_NAV_MSG_SWING_OFF : BB_NAV_MSG_SWING_ON;
        return 0;
    case BB_NAV_BR_ROOT:
        s->root_wait = 1;
        o->actions |= BB_NAV_A_ROOT_WAIT;
        o->message = BB_NAV_MSG_ROOT_WAIT;
        return 0;
    case BB_NAV_BR_SCALE:
        s->scale = (uint8_t)((s->scale + 1u) % BB_NAV_SCALES);
        s->root_wait = 0;
        o->actions |= BB_NAV_A_SCALE;
        o->message = (uint8_t)(BB_NAV_MSG_SCALE_OFF + s->scale);
        return 0;
    case BB_NAV_BR_EVO_TOGGLE: o->actions |= BB_NAV_A_EVO_TOGGLE; return 0;
    case BB_NAV_BR_EVOLVE:
        if (nav_enter(s, o, BB_NAV_EVOLVE)) { o->actions |= BB_NAV_A_EVO_OPEN; return 1; }
        return 0;
    case BB_NAV_BR_MODE: o->actions |= BB_NAV_A_GEN_MODE; return 0;    /* LFO again: EVOLVE -> EUCLID -> RANDOM */
    case BB_NAV_BR_OCT_STEPS: o->oct = BB_NAV_OCT_STEP_BANK; o->oct_dir = (int8_t)dir; return 0;
    case BB_NAV_BR_OCT_KEYS: nav_octave(s, o, dir); return 0;
    case BB_NAV_BR_OCT_SECTIONS: return nav_section_page(s, o, dir);
    case BB_NAV_BR_OCT_LIST: o->oct = BB_NAV_OCT_LIST_PAGE; o->oct_dir = (int8_t)dir; return 0;
    case BB_NAV_BR_OCT_BANK:
        count = bb_nav_bank_count(s, s->page);
        s->bank = (uint8_t)((int)s->bank + dir);
        if (s->bank >= count) s->bank = 0;                    /* (the resolver checked the room) */
        if (s->page == BB_NAV_FX) s->fx_tab = s->bank;
        o->message = nav_entry_message(s);
        return 1;
    case BB_NAV_BR_SONG_EDIT: case BB_NAV_BR_SONG_ADD: o->actions |= BB_NAV_A_SONG_EDIT; return 0;
    case BB_NAV_BR_GLOBALS_SAVE: o->actions |= BB_NAV_A_GLOBALS_SAVE; return 0;
    case BB_NAV_BR_LEAVE:
        if (s->page == BB_NAV_SONG) return nav_go_home(s, o);   /* GLO leaves SONG for STUDIO, as in 2.0 */
        s->bank = BB_NAV_GLOBAL_MAIN; o->message = nav_entry_message(s); o->page_changed = 1;
        return 1;
    default: return 0;
    }
}


/* PLAY: transport from every page, never a page change. */
static void nav_play(bb_nav_state *s, bb_nav_out *o, int rec, const bb_nav_facts *f)
{
    if (rec) {
        if (nav_has(s, BB_NAV_F_RESTART)) { o->actions |= BB_NAV_A_RESTART; o->message = BB_NAV_MSG_RESTART; }
        else o->message = BB_NAV_MSG_NOT_IN_BUILD;
        return;
    }
    if (s->page == BB_NAV_SAVE && f->saving) { o->message = BB_NAV_MSG_WAIT; return; }  /* safe-save policy */
    o->actions |= BB_NAV_A_TRANSPORT;
    /* 1 = PATTERN: confirm-then-start; RC2 (Joe 10:26) 2 = the HOME/SONGS list: CHOOSE + start + PERFORM when stopped */
    o->pattern_play = s->page == BB_NAV_PATTERN ? 1u : s->page == BB_NAV_STUDIO && s->bank == BB_NAV_HOME_SONGS ? 2u : 0u;
}

/* Queued kit/pattern that has not started: show the retry hint once. */
static uint8_t nav_queue_watch(bb_nav_state *s, const bb_nav_in *in)
{
    unsigned i;
    uint8_t late = 0;
    for (i = 0; i < 2; i++) {
        uint8_t q = i ? BB_NAV_Q_PATTERN : BB_NAV_Q_KIT;
        if (!(in->facts.queued & q)) { s->q_since[i] = 0; s->q_late &= (uint8_t)~(1u << i); continue; }
        if (!s->q_since[i]) { s->q_since[i] = in->now_ms | 1u; continue; }
        if (s->queue_timeout_ms && !(s->q_late & (1u << i)) && !late &&
            (uint32_t)(in->now_ms - s->q_since[i]) >= s->queue_timeout_ms)
            late = (uint8_t)(i + 1);
    }
    return late;
}

/* Key presses on the normal path. Returns 1 if the drum voice changed. */
static int nav_key_presses(bb_nav_state *s, const bb_nav_in *in, bb_nav_out *o, int rec)
{
    const uint32_t held = in->keys_held & BB_NAV_KEY_MASK;
    const uint32_t p = in->keys & BB_NAV_KEY_MASK & ~s->keys_blocked;
    uint8_t k, arg, acc_hold;
    int voice_changed = 0;
    {
        uint8_t a;
        acc_hold = bb_nav_key_role(s, BB_NAV_BLACK(9), rec, &a) == BB_NAV_K_ACCENT_HOLD &&
                   (held & ~s->keys_blocked & (1u << BB_NAV_BLACK(9)));
    }
    for (k = 0; k < BB_NAV_KEYS; k++) {
        uint8_t role, flags = 0;
        const uint32_t bit = 1u << k;
        if (!(p & bit)) continue;
        role = bb_nav_key_role(s, k, rec, &arg);
        if (role == BB_NAV_K_NONE || role == BB_NAV_K_ACCENT_HOLD) continue;
        if (nav_key_led(s, &in->facts, k, held, rec) == BB_NAV_LED_DARK) continue;   /* 2.1 P1: a dark key never acts */
        if (bb_nav_key_is_sound(role) && (rec || (role == BB_NAV_K_STEP && acc_hold))) flags = BB_NAV_KF_ACCENT;
        switch (role) {
        case BB_NAV_K_ROOT:
            s->root = arg; s->root_wait = 0;
            o->message = BB_NAV_MSG_ROOT_SET;
            break;
        case BB_NAV_K_SLOT_LOAD:
            if (nav_save_blocked(&in->facts)) {
                o->message = in->facts.saving ? BB_NAV_MSG_WAIT : BB_NAV_MSG_STOP_FIRST;
                continue;
            }
            break;
        case BB_NAV_K_VOICE:
            voice_changed |= s->voice != arg;
            s->voice = arg;
            break;
        default:
            break;
        }
        /* 2.0 ratchets: a held drum step waits for its release, so hold + K4 can set a ratchet without
         * toggling the hit. A tap within one scan, REC/accent steps and Dillo steps act at once as before. */
        if (role == BB_NAV_K_STEP && !flags && nav_is_drum(s) && (held & bit) && !s->step_hold) {
            s->step_hold = bit; s->step_hold_key = k; s->step_hold_arg = arg; s->step_hold_used = 0;
            continue;
        }
        if (role == BB_NAV_K_NOTE || role == BB_NAV_K_FILL) {
            nav_release_keys(s, o, bit);                     /* missed release */
            nav_emit(o, k, role, arg, flags);
            s->keys_active |= bit; s->act_role[k] = role; s->act_arg[k] = arg;
            if (!(held & bit)) nav_release_keys(s, o, bit);  /* tap inside one scan */
        } else {
            nav_emit(o, k, role, arg, flags);
        }
    }
    return voice_changed;
}

/* --------------------------------------------------------------- input */

void bb_nav_input(bb_nav_state *s, const bb_nav_in *in, bb_nav_out *o)
{
    const uint32_t pressed = in->pressed, held = in->held;
    const uint32_t kheld = in->keys_held & BB_NAV_KEY_MASK, kedge = in->keys & BB_NAV_KEY_MASK;
    const uint32_t hold = kheld | kedge;
    const int rec = (held & B(REC)) != 0;
    const uint32_t others = pressed & ~(B(HOME) | B(REC));  /* panel actions */
    const uint32_t chord = pressed & ~B(HOME);                /* incl. REC     */
    int any_enc = 0, nav = 0, skip_knobs = 0, skip_keys = 0;
    uint8_t late;
    unsigned i;
    int16_t enc[BB_NAV_ENCODERS];
    const int rec_was = s->rec_down;
    memset(o, 0, sizeof *o);
    s->rec_down = (uint8_t)rec;                                  /* 2.1 P1: LEDs and legend show the REC map */
    s->gen_mode = in->facts.gen_mode < BB_NAV_MODES ? in->facts.gen_mode : BB_NAV_MODE_EVOLVE;
    s->sec_seen = in->facts.section;
    s->sec_count = in->facts.section_count;
    if (s->sec_page >= nav_section_pages(s)) s->sec_page = 0;
    nav_enc_latch(s, in, rec, enc);
    for (i = 0; i < BB_NAV_ENCODERS; i++) any_enc |= in->enc[i] != 0;
    s->keys_blocked &= kheld & ~kedge;   /* lifted (or freshly re-pressed) keys unblock */

    /* 1. UBOOT: both OCT held. Pass straight through, change nothing else;
     *    keys touched now never sound later; owed releases wait. */
    if ((held & BOTH_OCT) == BOTH_OCT) {
        nav_reset_home(s);
        s->keys_blocked |= hold & ~s->keys_active;
        o->uboot = 1;
        nav_finish(s, o);
        return;
    }

    /* 2. Owed releases come out on every other scan. */
    nav_release_keys(s, o, s->keys_active & ~kheld);
    if (s->throw_on && !(held & B(FX))) { s->throw_on = 0; o->actions |= BB_NAV_A_THROW_OFF; }
    late = nav_queue_watch(s, in);

    /* 3. REC+HOME: panic on press, page unchanged. The release does nothing. */
    if ((pressed & B(HOME)) && rec) {
        nav_reset_home(s);
        if (held & B(HOME)) { s->home_down = 1; s->home_used = 1; s->home_since = in->now_ms; }
        o->actions |= BB_NAV_A_PANIC;
        o->message = BB_NAV_MSG_PANIC;
        nav_release_keys(s, o, s->keys_active);   /* panic: every key, whatever its role */
        nav_drain(s, o, hold);
        nav_finish(s, o);
        return;
    }

    /* 4a. 2.0 N5: REC held alone for the hold time shows this page's help (its REC chords). Unlike HOME's help it
     * is not modal: any button, key or knob hides it at once and that chord works as it always did. */
    if (pressed & B(REC)) { s->rec_since = in->now_ms; s->rec_used = 0; s->rec_help = 0; s->rec_shown = 0; }
    if (!rec) {
        /* 2.1 P1: on SONG a clean REC tap (no chord, no help shown) records a take, as 2.0's REC press did */
        if (rec_was && !s->rec_used && !s->rec_shown && !s->home_down &&
            bb_nav_button_role(s, &in->facts, BB_NAV_B_REC, 0) == BB_NAV_BR_SONG_REC &&
            nav_button_led_rule(s, &in->facts, BB_NAV_B_REC, 0) != BB_NAV_LED_DARK)
            o->actions |= BB_NAV_A_RECORD;
        s->rec_used = 0; s->rec_help = 0;
    }
    else if ((pressed & ~B(REC)) || any_enc || kedge) { s->rec_used = 1; s->rec_help = 0; }
    else if (!s->rec_used && (uint32_t)(in->now_ms - s->rec_since) >= s->hold_ms) { s->rec_help = 1; s->rec_shown = 1; }

    /* 4. HOME gesture bookkeeping: press starts, chord cancels, hold = help. */
    if (pressed & B(HOME)) {
        nav_reset_home(s);
        s->home_down = 1;
        s->home_since = in->now_ms;
    }
    if (s->home_down && !s->help) {
        if (chord || any_enc || kedge) s->home_used = 1;   /* chord: no tap */
        else if (!s->home_used && (held & B(HOME)) &&
                 (uint32_t)(in->now_ms - s->home_since) >= s->hold_ms) {
            s->help = 1; s->home_used = 1;
        }
    }

    /* 5. Help overlay is modal for everything except transport. 2.1 P1: OCT pages the rows; any other key, button
     *    or knob is explained (its row is picked) and does nothing else. PLAY still starts and stops. */
    if (s->help) {
        if (pressed & B(PLAY)) nav_play(s, o, rec, &in->facts);
        if (held & B(HOME)) {
            uint8_t ex = 0;
            if (pressed & BOTH_OCT) {
                unsigned n = bb_nav_help(s, &in->facts, 0, 0, 0, 0, 0, 0);
                unsigned pages = (n + BB_NAV_HELP_PAGE - 1u) / BB_NAV_HELP_PAGE;
                if ((pressed & B(OCTUP)) && s->help_page + 1u < pages) s->help_page++;
                if ((pressed & B(OCTDN)) && s->help_page) s->help_page--;
                s->explain = 0;
            } else if (kedge) ex = (uint8_t)(BB_NAV_EX_KEY | __builtin_ctz(kedge));
            else if (pressed & ~B(HOME)) ex = (uint8_t)(BB_NAV_EX_BUTTON | __builtin_ctz(pressed & ~B(HOME)));
            else for (i = 0; i < BB_NAV_ENCODERS; i++) if (in->enc[i]) { ex = (uint8_t)(BB_NAV_EX_ENCODER | i); break; }
            if (ex) {
                int found = -1;
                s->explain = ex;
                (void)bb_nav_help(s, &in->facts, 0, 0, 0, 0, ex, &found);
                if (found >= 0) s->help_page = (uint8_t)((unsigned)found / BB_NAV_HELP_PAGE);
            }
        }
        if (!(held & B(HOME))) nav_reset_home(s);
        nav_drop_routes(o);
        s->keys_blocked |= hold & ~s->keys_active;
        nav_finish(s, o);
        return;
    }

    /* 6. HOME release: a clean tap goes home, always the same place. */
    if (s->home_down && !(held & B(HOME))) {
        int tap_ = !s->home_used;
        nav_reset_home(s);
        if (tap_) {
#if BB_WITH_USER_KITS
            /* SAVE AS -> BUILD -> the KIT list: one level per tap (D1: HOME goes back, never forward). */
            if (s->page == BB_NAV_KIT && s->bank) {
                s->bank--; o->actions |= BB_NAV_A_BUILD;
                o->message = s->bank ? BB_NAV_MSG_BUILD : nav_entry_message(s);
                nav_drain(s, o, hold); nav_finish(s, o);
                return;
            }
#endif
            /* 2.1 P1: SONG ADD -> SONG, MIDI / CLOCK -> GLOBAL: one level per tap, as BUILD */
            if ((s->page == BB_NAV_SONG || s->page == BB_NAV_GLOBAL) && s->bank) {
                s->bank = 0; o->page_changed = 1;
                o->message = s->page == BB_NAV_GLOBAL ? BB_NAV_MSG_GLOBAL : BB_NAV_MSG_NONE;
                nav_drain(s, o, hold); nav_finish(s, o);
                return;
            }
            /* 2.1 P2: HOME from the Dillo PATCH page puts back the sound from before the audition */
            if (s->page == BB_NAV_KIT && !nav_is_drum(s)) o->actions |= BB_NAV_A_RESTORE;
            /* HOME only ever goes back. SONG opens from EDIT on STUDIO (bb_nav_edit_label "SONG"). */
            (void)nav_go_home(s, o);
            nav_drain(s, o, hold);
            nav_finish(s, o);
            return;
        }
    }

    /* 7. PLAY: transport from every page; never changes page. */
    if (pressed & B(PLAY)) {
        nav_play(s, o, rec, &in->facts);
        nav_drop_routes(o);
        nav_finish(s, o);
        return;
    }

    /* 8. One panel button per scan, in fixed priority order. */
    for (i = 0; i < sizeof nav_button_order; i++) {
        if (pressed & BB_NAV_BIT(nav_button_order[i])) {
            nav = nav_button(s, o, nav_button_order[i], rec, in);
            break;
        }
    }
    if (nav) { nav_drain(s, o, hold); nav_finish(s, o); return; }

    /* 9. The three big encoders: same job on every page (2.1 P1: SONG and MIDI keep 2.0's rule: they do nothing
     *    there, except ALGORITHM on SONG ADD, which moves the source's bank as on PATTERN). */
    if (s->page == BB_NAV_SONG || nav_midi(s)) {
        if (s->page == BB_NAV_SONG && s->bank == BB_NAV_SONG_ADD && enc[BB_NAV_E_ALGO]) {
            bb_nav_route *r = &o->enc[BB_NAV_E_ALGO];
            r->target = BB_NAV_TO_SONG_BANK; r->delta = enc[BB_NAV_E_ALGO];
        }
    } else {
        int16_t d;
        if ((d = enc[BB_NAV_E_SELECT]) != 0) {
            bb_nav_route *r = &o->enc[BB_NAV_E_SELECT];
            if (rec) { r->target = BB_NAV_TO_TEMPO; r->flags = BB_NAV_RF_COALESCE; r->delta = d; }
            else {
                nav_drain(s, o, hold);      /* focus changes: nothing else this scan */
#if BB_WITH_USER_KITS
                if (s->page == BB_NAV_KIT && s->bank) { s->bank = BB_NAV_KIT_LIST; o->actions |= BB_NAV_A_BUILD; }
#endif
                r->target = BB_NAV_TO_INSTRUMENT; r->delta = d;
                nav_finish(s, o);
                return;
            }
        }
        if ((d = enc[BB_NAV_E_PRESET]) != 0) {
            bb_nav_route *r = &o->enc[BB_NAV_E_PRESET];
            if (rec) {
                if (nav_is_drum(s)) {
                    /* voice changes knob labels and step keys: no knob/key edits this scan */
                    r->target = BB_NAV_TO_VOICE; r->delta = d;
                    skip_knobs = skip_keys = 1;
                } else if (s->page == BB_NAV_PLAY && nav_steps(s)) {
                    r->target = BB_NAV_TO_STEP_PITCH; r->flags = BB_NAV_RF_COALESCE; r->delta = d;
                }
            } else {
                bb_nav_route keep = o->enc[BB_NAV_E_SELECT];
                if (nav_enter(s, o, BB_NAV_KIT)) {
                    nav_drain(s, o, hold); o->enc[BB_NAV_E_SELECT] = keep;   /* tempo still applies */
                    o->enc[BB_NAV_E_PRESET].target = BB_NAV_TO_KIT; o->enc[BB_NAV_E_PRESET].delta = d;
                    nav_finish(s, o);
                    return;
                }
                r->target = BB_NAV_TO_KIT; r->delta = d;
            }
        }
        if ((d = enc[BB_NAV_E_ALGO]) != 0) {
            bb_nav_route *r = &o->enc[BB_NAV_E_ALGO];
            if (rec) { r->target = BB_NAV_TO_SECTION; r->delta = d; }
            else if (s->page == BB_NAV_STUDIO && s->bank == BB_NAV_HOME_SONGS) { r->target = BB_NAV_TO_PATTERN; r->delta = d; }
            else {
                bb_nav_route keep = o->enc[BB_NAV_E_SELECT];
                if (nav_enter(s, o, BB_NAV_PATTERN)) {
                    nav_drain(s, o, hold); o->enc[BB_NAV_E_SELECT] = keep;
                    o->enc[BB_NAV_E_ALGO].target = BB_NAV_TO_PATTERN; o->enc[BB_NAV_E_ALGO].delta = d;
                    nav_finish(s, o);
                    return;
                }
                r->target = BB_NAV_TO_PATTERN; r->delta = d;
            }
        }
    }

    /* 10a. 2.0 ratchets: release of the held drum step toggles it unless K4 was turned meanwhile;
     * while it is held, K4 sets that step's ratchet instead of its usual role. */
    if (s->step_hold) {
        if (!(kheld & s->step_hold) || s->page != BB_NAV_PLAY || !nav_steps(s) || !nav_is_drum(s)) {
            if (!s->step_hold_used && s->page == BB_NAV_PLAY && nav_steps(s) && nav_is_drum(s))
                nav_emit(o, s->step_hold_key, BB_NAV_K_STEP, s->step_hold_arg, 0);
            s->step_hold = 0; s->step_hold_used = 0;
        } else if (enc[BB_NAV_E_K4]) {
            bb_nav_route *r = &o->enc[BB_NAV_E_K4];
            r->target = BB_NAV_TO_RATCHET; r->role = 0; r->index = s->step_hold_arg;
            r->flags = BB_NAV_RF_COALESCE; r->delta = enc[BB_NAV_E_K4];
            enc[BB_NAV_E_K4] = 0; s->step_hold_used = 1;
        }
    }

    /* 10. K1-K4: the page's four labelled roles, all "latest value wins". */
    if (!skip_knobs) {
        uint8_t roles[4], idx[4];
        nav_knob_roles(s, roles, idx);
        for (i = 0; i < 4; i++) {
            bb_nav_route *r = &o->enc[BB_NAV_E_K1 + i];
            int16_t d = enc[BB_NAV_E_K1 + i];
            if (!d || roles[i] == BB_NAV_R_NONE) continue;
            r->target = BB_NAV_TO_KNOB; r->role = roles[i]; r->index = idx[i];
            r->flags = BB_NAV_RF_COALESCE; r->delta = d;
        }
    }

    /* 10c. 2.1 P1: keys that move inside the page (FX tabs, the MIDI banks, SONG ADD and back). Every key's and
     *      knob's meaning changes, so the scan is drained; SONG ADD is also told to the firmware (its prompt). */
    if (!others && !skip_keys) {
        uint32_t p = kedge & ~s->keys_blocked & BB_NAV_KEY_MASK;
        while (p) {
            uint8_t k = (uint8_t)__builtin_ctz(p), a, r = bb_nav_key_role(s, k, rec, &a), bank = 0xffu;
            p &= p - 1u;
            if (nav_key_led(s, &in->facts, k, kheld, rec) == BB_NAV_LED_DARK) continue;
            if (r == BB_NAV_K_FX_TAB) bank = a;
            else if (r == BB_NAV_K_SUBPAGE && s->page == BB_NAV_GLOBAL && a < 2u)
                bank = (uint8_t)(a ? BB_NAV_GLOBAL_CLOCK : BB_NAV_GLOBAL_MIDI);
            else if (r == BB_NAV_K_BACK) bank = 0;
            else if (r == BB_NAV_K_SONG_ADD) bank = BB_NAV_SONG_ADD;
            if (bank == 0xffu) continue;
            s->bank = bank;
            if (s->page == BB_NAV_FX) s->fx_tab = bank;
            o->message = r == BB_NAV_K_SONG_ADD ? (a ? BB_NAV_MSG_SONG_ADD : BB_NAV_MSG_NONE) : nav_entry_message(s);
            o->page_changed = 1;
            nav_drain(s, o, hold);
            if (r == BB_NAV_K_SONG_ADD || r == BB_NAV_K_BACK) nav_emit(o, k, r, a, 0);
            nav_finish(s, o);
            return;
        }
    }
#if BB_WITH_USER_KITS
    /* 10b. KIT black 1 (drum A): BUILD on, or back to the list. Every key's meaning changes, so drain. */
    if (!others && !skip_keys && (kedge & ~s->keys_blocked & (1u << BB_NAV_BLACK(1)))) {
        uint8_t a;
        if (bb_nav_key_role(s, BB_NAV_BLACK(1), rec, &a) == BB_NAV_K_BUILD) {
            s->bank = s->bank ? BB_NAV_KIT_LIST : BB_NAV_KIT_BUILD;
            o->actions |= BB_NAV_A_BUILD;
            o->message = s->bank ? BB_NAV_MSG_BUILD : nav_entry_message(s);
            nav_drain(s, o, hold); nav_finish(s, o);
            return;
        }
    }
#endif
    /* 11. Keys: any panel button in the same scan wins over key presses. */
    if (!others && !skip_keys && nav_key_presses(s, in, o, rec) && s->page == BB_NAV_SOUND) {
        for (i = BB_NAV_E_K1; i < BB_NAV_ENCODERS; i++) {   /* labels changed under the knobs */
            o->enc[i].target = BB_NAV_TO_NONE; o->enc[i].role = 0; o->enc[i].index = 0;
            o->enc[i].flags = 0; o->enc[i].delta = 0;
        }
    }
    if (others || skip_keys) s->keys_blocked |= kedge & kheld;   /* swallowed press: lift first */

    /* 12. Late queue hint, if nothing else is being said. */
    if (late && !o->message) {
        o->message = late == 1 ? BB_NAV_MSG_KIT_LATE : BB_NAV_MSG_PATTERN_LATE;
        s->q_late |= late;
    }
    nav_finish(s, o);
}

/* Use the same role resolver and HOME state machine as ordinary input, then
 * retain only non-authoring events. A STEPS key can never become a live pad. */
void bb_nav_input_count_busy(bb_nav_state *s,const bb_nav_in *in,bb_nav_out *out)
{
    if(!s||!in||!out)return;
    bb_nav_in safe=*in;uint32_t pads=0;uint8_t arg;
    for(unsigned k=0;k<BB_NAV_KEYS;k++)
        if(bb_nav_key_role(s,(uint8_t)k,(in->held&B(REC))!=0,&arg)==BB_NAV_K_PAD)pads|=1u<<k;
    safe.pressed&=B(HOME);safe.held&=B(HOME)|B(REC)|B(FX);
    memset(safe.enc,0,sizeof safe.enc);safe.keys&=pads;
    s->keys_blocked|=in->keys&~pads; /* blocked authoring never replays after the wait */
    bb_nav_input(s,&safe,out);
    unsigned keep=0;
    for(unsigned i=0;i<out->kev_count;i++){
        bb_nav_key_ev event=out->kev[i];
        if(event.role==BB_NAV_K_PAD||((event.flags&BB_NAV_KF_RELEASE)&&
           (event.role==BB_NAV_K_NOTE||event.role==BB_NAV_K_FILL)))out->kev[keep++]=event;
    }
    out->kev_count=(uint8_t)keep;out->actions&=BB_NAV_A_PANIC|BB_NAV_A_THROW_OFF;
    memset(out->enc,0,sizeof out->enc);out->oct=0;out->oct_dir=0;out->uboot=0;
}

/* ---------------------------------------------------------------- the key role table (2.1 P1)
 * One descriptor per key role: the legend word, the HELP text and the light rule. nav_key_led switches on the
 * rule, not on the role, so a new role cannot be added without choosing a rule, a word and a text (the table is
 * indexed by role and its length is checked; the nav_p1 host test checks every role that can appear has all three). */
enum {
    LR_DARK,        /* never offered (a feature not in this build)              */
    LR_DIM,         /* half: works here                                         */
    LR_NOTE,        /* half; root keys bright under a scale                     */
    LR_STEP,        /* bright when the step is set (EVOLVE: when it is locked)   */
    LR_KIT_SLOT,    /* pulse queued, bright playing, half stored, else off      */
    LR_AUDIBLE,     /* group / part: bright when heard, half when muted         */
    LR_SOLO,        /* REC map: bright when that group / part is the solo       */
    LR_FILL, LR_SECT_STEP, LR_KILL, LR_VOICE, LR_HOLD, LR_BANK_PREV, LR_BANK_NEXT, LR_SECTION, LR_PICK, LR_SLOT,
    LR_DISPLAY, LR_ROOT,
    LR_EVO_PART,    /* bright when that part evolves                            */
    LR_EVO_MOVE,    /* bright when that attribute may move                      */
    LR_GEN_VOICE,   /* bright on EUCLID's lane                                  */
    LR_TAB,         /* FX tab: bright = shown, half = available                 */
    LR_SUBPAGE,     /* GLOBAL: bright on the MIDI bank shown                    */
    LR_SONG_ROWS,   /* SONG COPY / DELETE: off with no rows; DELETE pulses while it waits */
    LR_UNDO, LR_REDO, LR_SONG_DILLO,
    LR_BUILD, LR_CURSOR,
    LR_TAKE,        /* SONGS / ERAS / PRESETS: bright = this part takes the selection          */
    LR_COLL         /* 2.1 P3b: bright = the collection shown                                    */
};
typedef struct { char word[8]; uint16_t help; uint8_t led; } bb_nav_role_desc;
static const bb_nav_role_desc nav_role_desc[BB_NAV_KEY_ROLES] = {
    [BB_NAV_K_NONE] = {"", 0, LR_DARK},
    [BB_NAV_K_PAD] = {"PADS", HX(HX_PLAY_DRUMS), LR_DIM}, [BB_NAV_K_NOTE] = {"NOTES", HX(HX_PLAY_NOTES), LR_NOTE},
    [BB_NAV_K_STEP] = {"STEPS", TX_STEPS_1_16, LR_STEP},
    [BB_NAV_K_KIT_SLOT] = {"KITS", 0, LR_KIT_SLOT}, [BB_NAV_K_KIT_SLOT_STORE] = {"STORE", 0, LR_DIM},
    [BB_NAV_K_GROUP_MUTE] = {"MUTE", HX(HX_MUTE_GROUP), LR_AUDIBLE}, [BB_NAV_K_GROUP_SOLO] = {"SOLO", HX(HX_SOLO_GROUP), LR_SOLO},
    [BB_NAV_K_FILL] = {"FILL", 0, LR_FILL},
    [BB_NAV_K_SECTION_PREV] = {"SECT", TX_H_PREVIOUS_NEXT_SECTION, LR_SECT_STEP},
    [BB_NAV_K_SECTION_NEXT] = {"SECT", TX_H_PREVIOUS_NEXT_SECTION, LR_SECT_STEP},
    [BB_NAV_K_KILL] = {"KILL", TX_H_MUTE_A_B_DILLO_ALL_DRUMS, LR_KILL}, [BB_NAV_K_VOICE] = {"VOICE", TX_CHOOSE_VOICE, LR_VOICE},
    [BB_NAV_K_ACCENT_HOLD] = {"ACCENT", TX_H_ACCENT_WHILE_HELD, LR_HOLD},
    [BB_NAV_K_BANK_PREV] = {"BANK", TX_STEP_BANK, LR_BANK_PREV}, [BB_NAV_K_BANK_NEXT] = {"BANK", TX_STEP_BANK, LR_BANK_NEXT},
    [BB_NAV_K_STEP_PITCH] = {"PITCH", HX(HX_STEP_PITCH), LR_DIM},
    [BB_NAV_K_INSTR_MUTE] = {"MUTE", TX_H_MUTE_A_B_DILLO, LR_AUDIBLE}, [BB_NAV_K_INSTR_SOLO] = {"SOLO", TX_H_SOLO_A_B_DILLO, LR_SOLO},
    [BB_NAV_K_SECTION] = {"SECTION", TX_H_SECTION_AT_THE_BAR, LR_SECTION}, [BB_NAV_K_PICK] = {"PICK", TX_QUICK_PICK, LR_PICK},
    [BB_NAV_K_SLOT] = {"SLOT", TX_SLOT_1_4, LR_SLOT}, [BB_NAV_K_SLOT_LOAD] = {"LOAD", TX_LOAD_SLOT, LR_SLOT},
    [BB_NAV_K_ANIMATION] = {"ANIM", 0, LR_DISPLAY}, [BB_NAV_K_SCOPE] = {"SCOPE", 0, LR_DISPLAY},
    [BB_NAV_K_METERS] = {"METERS", 0, LR_DISPLAY},
    [BB_NAV_K_EVO_KEY] = {"PARTS", TX_PARTS_TO_EVOLVE, LR_EVO_PART}, [BB_NAV_K_EVO_LOCK] = {"LOCK", HX(HX_LOCK_STEP), LR_STEP},
    [BB_NAV_K_EVO_FREEZE] = {"FREEZE", HX(HX_FREEZE), LR_DIM}, [BB_NAV_K_EVO_HOME] = {"RETURN", TX_H_BACK_TO_THE_ORIGINAL, LR_DIM},
    [BB_NAV_K_ROOT] = {"ROOT", HX(HX_SET_ROOT), LR_ROOT}, [BB_NAV_K_GEN_MODE] = {"MODE", HX(HX_NEXT_MODE), LR_DIM},
    [BB_NAV_K_SUBPAGE] = {"", 0, LR_SUBPAGE},                     /* word and text by arg: nav_subpage */
    [BB_NAV_K_EVO_MOVE] = {"MOVES", TX_H_RHYTHM_PITCH_ACCENT_SLIDE, LR_EVO_MOVE},
    [BB_NAV_K_GEN_VOICE] = {"LANE", HX(HX_LANE_TO_WRITE), LR_GEN_VOICE}, [BB_NAV_K_FX_TAB] = {"TABS", HX(HX_EFFECT_TAB), LR_TAB},
    [BB_NAV_K_BACK] = {"BACK", TX_BACK_ONE_LEVEL, LR_DIM}, [BB_NAV_K_SONG_ADD] = {"ADD", HX(HX_ADD_SECTION), LR_DIM},
    [BB_NAV_K_SONG_COPY] = {"COPY", HX(HX_COPY_ROW), LR_SONG_ROWS}, [BB_NAV_K_SONG_DELETE] = {"DELETE", HX(HX_DELETE_ROW), LR_SONG_ROWS},
    [BB_NAV_K_SONG_UNDO] = {"UNDO", TX_UNDO, LR_UNDO}, [BB_NAV_K_SONG_REDO] = {"REDO", HX(HX_REDO), LR_REDO},
    [BB_NAV_K_SONG_DILLO] = {"DILLO", HX(HX_DILLO_IN_OR_OUT), LR_SONG_DILLO},
    [BB_NAV_K_CONDUCT] = {"SECTION", HX(HX_ALL_PARTS), LR_SECTION}, [BB_NAV_K_SONG_PICK] = {"SONG", HX(HX_SONG_PICK), LR_PICK},
    [BB_NAV_K_TAKE] = {"TAKE", HX(HX_TAKES), LR_TAKE}, [BB_NAV_K_COLLECTION] = {"LIST", HX(HX_COLLECTIONS), LR_COLL},
#if BB_WITH_USER_KITS
    [BB_NAV_K_BUILD] = {"BUILD", HX(HX_KIT_BUILDER), LR_BUILD}, [BB_NAV_K_LANE] = {"LANE", TX_LANE_CHOOSE_HEAR, LR_CURSOR},
    [BB_NAV_K_UK_SLOT] = {"SLOT", TX_SAVE_AS_SLOT_1_4, LR_CURSOR},
#endif
};
/* GLOBAL's sub-page keys: word and text by arg (0 MIDI, 1 CLOCK, 3 SONG, 4 DILLO VIEW) */
static const char nav_sub_word[5][11] = {"MIDI", "CLOCK", "", "SONG", "DILLO VIEW"};
static const uint16_t nav_sub_help[5] = {HX(HX_MIDI_CHANNELS), HX(HX_MIDI_CLOCK), 0, HX(HX_SONG_ARRANGER), HX(HX_DILLO_KIT_VIEW)};

uint16_t bb_nav_key_help(uint8_t role, uint8_t arg)
{
    if (role == BB_NAV_K_SUBPAGE) return arg < 5u ? nav_sub_help[arg] : 0;
    return role < BB_NAV_KEY_ROLES ? nav_role_desc[role].help : 0;
}
const char *bb_nav_key_word(const bb_nav_state *s, uint8_t key, int rec)
{
    uint8_t a, r = bb_nav_key_role(s, key, rec, &a);
    if (r == BB_NAV_K_SUBPAGE) return a < 5u ? nav_sub_word[a] : "";
    return r < BB_NAV_KEY_ROLES ? nav_role_desc[r].word : "";
}
/* The text of a help id: a BBTX text, or one of the short app texts (BB_NAV_HX). */

const char *bb_nav_welcome_help(uint8_t kind, uint8_t control, int rec)
{
    if(rec)return 0;
    if((kind==BB_NAV_HR_KEYS&&control<16u)||
       (kind==BB_NAV_HR_BUTTON&&(control==BB_NAV_B_EDIT||control==BB_NAV_B_HOME)))
        return "open song list";
    if(kind==BB_NAV_HR_ENCODER&&control==BB_NAV_E_ALGO)return "select song";
    if(kind==BB_NAV_HR_BUTTON&&control==BB_NAV_B_PLAY)return "play selected song";
    return 0;
}

const char *bb_nav_help_text(uint16_t id)
{
    if (id & BB_NAV_HX) return (id & 0x7fffu) < HX_COUNT ? nav_short[id & 0x7fffu] : "";
    return id ? bb_tx(id) : "";
}

/* ---------------------------------------------------------------- LEDs */

static uint8_t nav_key_led(const bb_nav_state *s, const bb_nav_facts *f, uint8_t k, uint32_t keys_held, int rec)
{
    uint8_t arg, role = bb_nav_key_role(s, k, rec, &arg);
    const uint8_t q = f->queued;
    const uint8_t song = f->song;
    switch (role < BB_NAV_KEY_ROLES ? nav_role_desc[role].led : LR_DARK) {
    case LR_DARK: return BB_NAV_LED_DARK;
    case LR_NOTE:
        if (s->scale != BB_NAV_SCALE_OFF && k % nav_scale_len[s->scale] == 0) return BB_NAV_LED_LIT; /* root keys */
        return BB_NAV_LED_DIM;
    case LR_ROOT: return BB_NAV_LED_BREATH;
    case LR_STEP: return (f->steps >> arg) & 1u ? BB_NAV_LED_LIT : BB_NAV_LED_DIM;
    case LR_KIT_SLOT:
        if ((q & BB_NAV_Q_KIT) && f->kit_slot_queued == arg) return BB_NAV_LED_BREATH;
        if (f->kit_slot == arg) return BB_NAV_LED_LIT;
        return (f->kit_slots_stored >> arg) & 1u ? BB_NAV_LED_DIM : BB_NAV_LED_DARK;
    case LR_AUDIBLE:
        if (role == BB_NAV_K_GROUP_MUTE)
            return nav_audible(f->group_muted, f->group_solo, arg) ? BB_NAV_LED_LIT : BB_NAV_LED_DIM;
        return nav_audible(f->instr_muted, f->instr_solo, arg) ? BB_NAV_LED_LIT : BB_NAV_LED_DIM;
    case LR_SOLO:
        return ((role == BB_NAV_K_GROUP_SOLO ? f->group_solo : f->instr_solo) >> arg) & 1u ? BB_NAV_LED_LIT : BB_NAV_LED_DIM;
    case LR_FILL: return f->fill_on ? BB_NAV_LED_LIT : BB_NAV_LED_DIM;
    case LR_SECT_STEP: return (q & BB_NAV_Q_SECTION) ? BB_NAV_LED_BREATH : BB_NAV_LED_DIM;
    case LR_KILL:
        if (arg == 3) return (nav_audible(f->instr_muted, f->instr_solo, 0) || nav_audible(f->instr_muted, f->instr_solo, 1))
                             ? BB_NAV_LED_LIT : BB_NAV_LED_DIM;
        return nav_audible(f->instr_muted, f->instr_solo, arg) ? BB_NAV_LED_LIT : BB_NAV_LED_DIM;
    case LR_VOICE: return s->voice == arg ? BB_NAV_LED_LIT : BB_NAV_LED_DIM;
    case LR_HOLD: return (keys_held >> k) & 1u ? BB_NAV_LED_LIT : BB_NAV_LED_DIM;
    case LR_BANK_PREV: return f->step_bank > 0 ? BB_NAV_LED_DIM : BB_NAV_LED_DARK;
    case LR_BANK_NEXT: return f->step_bank + 1u < f->step_banks ? BB_NAV_LED_DIM : BB_NAV_LED_DARK;
    case LR_SECTION:
        if ((q & BB_NAV_Q_SECTION) && (f->section_queued == arg || ((f->sec_queued >> (arg & 63u)) & 1u))) return BB_NAV_LED_BREATH;
        if (f->section == arg || ((f->sec_lit >> (arg & 63u)) & 1u)) return BB_NAV_LED_LIT;   /* RC3: a combination */
        return arg < f->section_count ? BB_NAV_LED_DIM : BB_NAV_LED_DARK;
    case LR_PICK: {
        uint8_t qbit = s->page == BB_NAV_KIT ? BB_NAV_Q_KIT : BB_NAV_Q_PATTERN;
        if (arg >= f->list_count) return BB_NAV_LED_DARK;
        if ((q & qbit) && f->list_queued == arg) return BB_NAV_LED_BREATH;
        return f->list_playing == arg ? BB_NAV_LED_LIT : BB_NAV_LED_DIM;
    }
    case LR_SLOT: return f->slot == arg ? BB_NAV_LED_LIT : BB_NAV_LED_DIM;
    case LR_TAKE:
        return (f->home_take >> arg) & 1u ? BB_NAV_LED_LIT : BB_NAV_LED_DIM;
    case LR_COLL: return f->home_coll == arg ? BB_NAV_LED_LIT : BB_NAV_LED_DIM;
    case LR_DISPLAY: {
        uint8_t bit = role == BB_NAV_K_ANIMATION ? BB_NAV_DISP_ANIMATION : role == BB_NAV_K_SCOPE ? BB_NAV_DISP_SCOPE : BB_NAV_DISP_METERS;
        return (f->display & bit) ? BB_NAV_LED_LIT : BB_NAV_LED_DIM;
    }
    case LR_EVO_PART: return (f->evo_parts >> arg) & 1u ? BB_NAV_LED_LIT : BB_NAV_LED_DIM;
    case LR_EVO_MOVE: return (f->evo_moves >> (arg - 6u)) & 1u ? BB_NAV_LED_LIT : BB_NAV_LED_DIM;
    case LR_GEN_VOICE: return f->gen_voice == arg ? BB_NAV_LED_LIT : BB_NAV_LED_DIM;
    case LR_TAB: return s->bank == arg ? BB_NAV_LED_LIT : BB_NAV_LED_DIM;
    case LR_SUBPAGE: return s->bank && s->bank == arg + 1u ? BB_NAV_LED_LIT : BB_NAV_LED_DIM;
    case LR_SONG_ROWS:
        if (!(song & BB_NAV_SONG_F_ROWS)) return BB_NAV_LED_DARK;
        return role == BB_NAV_K_SONG_DELETE && (song & BB_NAV_SONG_F_DELETE) ? BB_NAV_LED_BREATH : BB_NAV_LED_DIM;
    case LR_UNDO: return (song & BB_NAV_SONG_F_UNDO) ? BB_NAV_LED_DIM : BB_NAV_LED_DARK;
    case LR_REDO: return (song & BB_NAV_SONG_F_REDO) ? BB_NAV_LED_DIM : BB_NAV_LED_DARK;
    case LR_SONG_DILLO: return (song & BB_NAV_SONG_F_DILLO) ? BB_NAV_LED_LIT : BB_NAV_LED_DIM;
#if BB_WITH_USER_KITS
    case LR_BUILD: return s->bank ? BB_NAV_LED_LIT : BB_NAV_LED_DIM;
    case LR_CURSOR: return f->list_cursor == arg ? BB_NAV_LED_LIT : BB_NAV_LED_DIM;
#endif
    default: return BB_NAV_LED_DIM;
    }
}
uint8_t bb_nav_key_led(const bb_nav_state *s, const bb_nav_facts *f, uint8_t key, uint32_t keys_held)
{
    return key < BB_NAV_KEYS ? nav_key_led(s, f, key, keys_held, s->rec_down) : BB_NAV_LED_DARK;
}

/* 2.1 P1: one rule everywhere. Buttons and keys light from the same resolvers dispatch uses; while REC is held
 * they show the REC map; HELP keeps every light (HOME bright as the help mark) so held HOME explains what is lit. */
void bb_nav_leds(const bb_nav_state *s, const bb_nav_facts *f, uint32_t keys_held, bb_nav_led_out *l)
{
    uint8_t b, k;
    const int rec = s->rec_down;
    memset(l, 0, sizeof *l);
    for (b = 0; b < BB_NAV_BUTTONS; b++) l->button[b] = nav_button_led_rule(s, f, b, b == BB_NAV_B_REC ? 0 : rec);
    if (rec && l->button[BB_NAV_B_PLAY] == BB_NAV_LED_DARK)   /* PLAY keeps showing the transport */
        l->button[BB_NAV_B_PLAY] = nav_button_led_rule(s, f, BB_NAV_B_PLAY, 0);
    for (k = 0; k < BB_NAV_KEYS; k++) l->key[k] = nav_key_led(s, f, k, keys_held, rec);
    if (s->help) l->button[BB_NAV_B_HOME] = BB_NAV_LED_LIT;   /* the help mark */
}

/* 2.0 D4: the page's one-line key legend, built from the key role table above, so it can never disagree with
 * routing or the LEDs (which come from the same roles; 2.1 P1: with REC held, the REC map). White keys read 1-16
 * and black keys B1-B11; neighbouring keys that do the same job share one range ("1-11 PADS  B1-4 MUTE  B5 FILL").
 * whites = 0 leaves the white-key runs out and spaces runs by one, not two: the caller asks for that when the whole
 * line would not fit the screen (PLAY drums, STEPS, EVOLVE: the page itself shows what the white keys do). Short
 * words only: they stay in the app (a table, not help text). */
static const char *nav_leg(const bb_nav_state *s, unsigned k)
{
    return bb_nav_key_word(s, (uint8_t)k, s->rec_down);
}
static int nav_leg_eq(const char *a, const char *b)       /* (the firmware links no strcmp) */
{
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}
static unsigned nav_leg_num(char *o, unsigned v)
{
    unsigned n = 0;
    if (v >= 10u) o[n++] = (char)('0' + v / 10u);
    o[n++] = (char)('0' + v % 10u);
    return n;
}
unsigned bb_nav_legend(const bb_nav_state *s, char *out, unsigned cap, int whites)
{
    unsigned n = 0, k = whites ? 0u : 16u, end = whites == 3 ? 16u : BB_NAV_KEYS;
    while (k < end) {
        const char *w = nav_leg(s, k);
        unsigned e = k + 1u, lim = k < 16u ? 16u : BB_NAV_KEYS, base = k < 16u ? 0u : 16u;
        unsigned len = 0;
        while (w[len]) len++;
        while (e < lim && nav_leg_eq(nav_leg(s, e), w)) e++;
        if (len && n + 11u + len <= cap) {   /* "  B10-11 " + the word + NUL */
            if (n) { out[n++] = ' '; if (whites == 1 || whites == 3) out[n++] = ' '; }   /* 0 and 2: one space */
            if (base) out[n++] = 'B';
            n += nav_leg_num(out + n, k - base + 1u);
            if (e - 1u > k) { out[n++] = '-'; n += nav_leg_num(out + n, e - base); }
            out[n++] = ' ';
            while (*w && n + 1u < cap) out[n++] = *w++;
        }
        k = e;
    }
    if (cap) out[n < cap ? n : cap - 1u] = 0;
    return n;
}

/* ---------------------------------------------------------------- HELP (2.1 P1)
 * Generated from the tables: one row per run of keys with the same role and text, one per button with a role (EDIT
 * and the page's own controls first), one for K1-K4 (their labels), one per big encoder. rec = 1 lists what REC
 * changes: the REC roles of keys, buttons and encoders, and "REC + sound keys: accent". Rows that do not fit one
 * screen page with OCT inside help. Nothing here acts: HELP only reads. */
typedef struct {
    bb_nav_help_row *rows;
    unsigned first, max, n;
    uint8_t explain;
    int found;
} nav_help_out;
static void nav_help_put(nav_help_out *h, uint8_t kind, uint8_t a, uint8_t b, uint8_t rec, uint8_t role, uint8_t arg,
                         uint16_t text, uint8_t ex_lo, uint8_t ex_hi)
{
    if (h->explain && h->explain >= ex_lo && h->explain <= ex_hi && h->found < 0) h->found = (int)h->n;
    if (h->rows && h->n >= h->first && h->n - h->first < h->max) {
        bb_nav_help_row *r = &h->rows[h->n - h->first];
        r->kind = kind; r->a = a; r->b = b; r->rec = rec; r->role = role; r->arg = arg; r->text = text;
    }
    h->n++;
}
static const uint8_t nav_help_button_order[] = {
    BB_NAV_B_EDIT, BB_NAV_B_SCL, BB_NAV_B_LFO, BB_NAV_B_OCTDN, BB_NAV_B_FX, BB_NAV_B_ENV, BB_NAV_B_ARP, BB_NAV_B_SEQ,
    BB_NAV_B_GLO, BB_NAV_B_SAVE, BB_NAV_B_HOME, BB_NAV_B_PLAY, BB_NAV_B_REC
};
unsigned bb_nav_help(const bb_nav_state *s, const bb_nav_facts *f, int rec, bb_nav_help_row *rows, unsigned first,
                     unsigned max, uint8_t explain, int *found)
{
    nav_help_out h;
    unsigned k, i;
    const int drum = nav_is_drum(s), quiet = s->page == BB_NAV_SONG || nav_midi(s);
    h.rows = rows; h.first = first; h.max = max; h.n = 0; h.explain = explain; h.found = -1;
    /* keys: runs of one role and one text */
    for (k = 0; k < BB_NAV_KEYS;) {
        uint8_t a0, r0 = bb_nav_key_role(s, (uint8_t)k, rec, &a0), lim = k < 16u ? 16u : BB_NAV_KEYS;
        uint16_t t0 = bb_nav_key_help(r0, a0);
        unsigned e = k + 1u;
        if (rec && bb_nav_key_is_sound(r0)) t0 = TX_ACCENT;                        /* REC + sound key = accent */
        while (e < lim) {
            uint8_t a1, r1 = bb_nav_key_role(s, (uint8_t)e, rec, &a1);
            uint16_t t1 = rec && bb_nav_key_is_sound(r1) ? (uint16_t)TX_ACCENT : bb_nav_key_help(r1, a1);
            if (t1 != t0 || (r1 != r0 && !t0)) break;            /* one row per run of one text (B6-7 SECTION) */
            e++;
        }
        if (r0 != BB_NAV_K_NONE && r0 != BB_NAV_K_ROOT) {
            uint8_t plain_a, plain = bb_nav_key_role(s, (uint8_t)k, 0, &plain_a);
            if (!rec || plain != r0 || bb_nav_key_is_sound(r0))
                nav_help_put(&h, BB_NAV_HR_KEYS, (uint8_t)k, (uint8_t)(e - 1u), (uint8_t)rec, r0, a0, t0,
                             (uint8_t)(BB_NAV_EX_KEY | k), (uint8_t)(BB_NAV_EX_KEY | (e - 1u)));
        }
        k = e;
    }
    /* 2.0 ratchets: a held drum step + K4 */
    if (!rec && s->page == BB_NAV_PLAY && nav_steps(s) && drum)
        nav_help_put(&h, BB_NAV_HR_HELD_K4, 0, 15, 0, BB_NAV_K_STEP, 0, TX_RATCHET_X1_X4, 0xffu, 0xffu);
    /* buttons, EDIT and the page's own first */
    for (i = 0; i < sizeof nav_help_button_order; i++) {
        uint8_t b = nav_help_button_order[i], r = bb_nav_button_role(s, f, b, rec), ex = (uint8_t)(BB_NAV_EX_BUTTON | b);
        if (b == BB_NAV_B_OCTDN) {                            /* OCT-/+ share one row */
            uint8_t r2 = bb_nav_button_role(s, f, BB_NAV_B_OCTUP, rec);
            if (r == BB_NAV_BR_NONE) r = r2;
            if (r != BB_NAV_BR_NONE && (!rec || r != bb_nav_button_role(s, f, b, 0)))
                nav_help_put(&h, BB_NAV_HR_BUTTON, b, BB_NAV_B_OCTUP, (uint8_t)rec, r, 0, bb_nav_button_help(r),
                             ex, (uint8_t)(BB_NAV_EX_BUTTON | BB_NAV_B_OCTUP));
            continue;
        }
        if (r == BB_NAV_BR_NONE || (rec && b == BB_NAV_B_REC)) continue;
        if (rec && r == bb_nav_button_role(s, f, b, 0)) continue;    /* the REC page lists what REC changes */
        if (b == BB_NAV_B_REC) {
            /* the REC modifier: one row; on SONG a tap records */
            nav_help_put(&h, BB_NAV_HR_BUTTON, b, 0, 0, r, 0, bb_nav_button_help(r), ex, ex);
            continue;
        }
        nav_help_put(&h, BB_NAV_HR_BUTTON, b, 0, (uint8_t)rec, r, 0, bb_nav_button_help(r), ex, ex);
    }
    /* K1-K4: one row; the screen carries the labels */
    if (!rec) {
        uint8_t r[4], idx[4], any = 0;
        nav_knob_roles(s, r, idx);
        for (i = 0; i < 4u; i++) any |= r[i] != BB_NAV_R_NONE;
        if (any) nav_help_put(&h, BB_NAV_HR_KNOBS, 4, 0, 0, r[0], 0, 0,
                              (uint8_t)(BB_NAV_EX_ENCODER | BB_NAV_E_K1), (uint8_t)(BB_NAV_EX_ENCODER | BB_NAV_E_K4));
    }
    /* the three big encoders */
    if (!quiet) {
        const uint8_t eS = (uint8_t)(BB_NAV_EX_ENCODER | BB_NAV_E_SELECT), eA = (uint8_t)(BB_NAV_EX_ENCODER | BB_NAV_E_ALGO),
                      eP = (uint8_t)(BB_NAV_EX_ENCODER | BB_NAV_E_PRESET);
        if (!rec) {
            nav_help_put(&h, BB_NAV_HR_ENCODER, BB_NAV_E_SELECT, 0, 0, 0, 0, TX_INSTRUMENT, eS, eS);
            nav_help_put(&h, BB_NAV_HR_ENCODER, BB_NAV_E_ALGO, 0, 0, 0, 0, TX_PATTERN, eA, eA);
#if BB_WITH_USER_KITS
            if (s->page == BB_NAV_KIT && s->bank == BB_NAV_KIT_BUILD)
                nav_help_put(&h, BB_NAV_HR_ENCODER, BB_NAV_E_PRESET, 0, 0, 0, 0, TX_LANE_S_KIT, eP, eP);
            else
#endif
            nav_help_put(&h, BB_NAV_HR_ENCODER, BB_NAV_E_PRESET, 0, 0, 0, 0, drum ? (uint16_t)TX_KIT : (uint16_t)HX(HX_PATCH), eP, eP);
        } else {
            nav_help_put(&h, BB_NAV_HR_ENCODER, BB_NAV_E_SELECT, 0, 1, 0, 0, HX(HX_TEMPO), eS, eS);
            nav_help_put(&h, BB_NAV_HR_ENCODER, BB_NAV_E_ALGO, 0, 1, 0, 0, HX(HX_NEXT_SECTION), eA, eA);
            if (drum) nav_help_put(&h, BB_NAV_HR_ENCODER, BB_NAV_E_PRESET, 0, 1, 0, 0, TX_VOICE, eP, eP);
            else if (s->page == BB_NAV_PLAY && nav_steps(s))
                nav_help_put(&h, BB_NAV_HR_ENCODER, BB_NAV_E_PRESET, 0, 1, 0, 0, HX(HX_STEP_PITCH), eP, eP);
        }
    } else if (!rec && s->page == BB_NAV_SONG && s->bank == BB_NAV_SONG_ADD) {
        const uint8_t eA = (uint8_t)(BB_NAV_EX_ENCODER | BB_NAV_E_ALGO);
        nav_help_put(&h, BB_NAV_HR_ENCODER, BB_NAV_E_ALGO, 0, 0, 0, 0, HX(HX_SOURCE_BANK), eA, eA);
    }
    if (found) *found = h.found;
    return h.n;
}
