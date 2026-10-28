/* SPDX-License-Identifier: GPL-3.0-only
 * Big Beat navigation core v2. See bb_nav.h. Pure decisions, no side effects
 * outside the caller's bb_nav_state / bb_nav_out / bb_nav_led_out. */
#include "bb_nav.h"
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
    "STUDIO", "PLAY", "KIT", "PATTERN", "SOUND", "FX", "SAVE", "EVOLVE", "GLOBAL"
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
    "SLOT", "AMOUNT", "EVERY", "ROOT", "SCALE"
};

static const char *const nav_messages[BB_NAV_MESSAGES] = {
    "",
    "WAIT",
    "STUDIO / EDIT: OPEN",
    "PADS / SEQ: STEPS",
    "STEPS / ARP: LIVE",
    "KEYS / SEQ: STEPS",
    "KIT / EDIT: CHOOSE",
    "PATCH / EDIT: LOAD",
    "PATTERN / EDIT: CHOOSE",
    "SOUND / EDIT: MORE",
    "FX / EDIT: NEXT BANK",
    "SAVE / KEYS 1-4, EDIT: STORE",
    "EVOLVE / EDIT: CAPTURE",
    "GLOBAL / K1-K4",
    "CHOOSE PATTERN / PLAY: START",
    "ALL NOTES OFF",
    "RESTART FROM BAR 1",
    "SWING ON",
    "SWING OFF",
    "SCALE OFF",
    "SCALE MINOR",
    "SCALE MAJOR",
    "SCALE DORIAN",
    "SCALE PENTA",
    "ROOT: PRESS A KEY",
    "ROOT SET",
    "TAP TEMPO",
    "DUB THROW",
    "STOP FIRST",
    "OCTAVE LIMIT",
    "KIT WAITING / EDIT: RETRY",
    "PATTERN WAITING / EDIT: RETRY",
    "NOT IN THIS BUILD",
    "AUDIO FAULT / LOAD BLOCKED",
    "KIT NOT READY",
    "PATTERNS NOT READY",
    "SOUNDS NOT READY",
    "NO PATTERNS FOR THIS KIT",
    "KIT / PATTERN MISMATCH",
    "TIMING NOT SUPPORTED",
    "VOICE NOT SUPPORTED",
    "EXPRESSION NOT SUPPORTED",
    "PATTERN UNAVAILABLE",
    "SOUND REQUEST REFUSED",
    "SOUND ALREADY LOADED",
    "NO ALTERNATE VOICE",
    "LAYER OFF",
    "SOURCE LENGTH: READ ONLY",
    "SOURCE PATTERN: EVOLVE OFF",
    "SECTIONS 1-11",
    "SECTIONS 12-22",
    "SECTIONS 23-33",
    "SOUND / K1-K4"
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
static const char *const nav_fx_bank_names[5] = {"ECHO", "TAPE", "SPRING", "MOTION", "SENDS"};

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
static void nav_knob_roles(const bb_nav_state *s, uint8_t r[4], uint8_t idx[4]);

/* v2.1 section pages: how many pages the current pattern needs (>= 1). */
static uint8_t nav_section_pages(const bb_nav_state *s)
{
    unsigned n = s->sec_count ? (s->sec_count + BB_NAV_SECTION_KEYS - 1u) / BB_NAV_SECTION_KEYS : 1u;
    return (uint8_t)(n > BB_NAV_SECTION_PAGES ? BB_NAV_SECTION_PAGES : n);
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
    case BB_NAV_KIT: return nav_is_drum(s) ? BB_NAV_MSG_KIT : BB_NAV_MSG_PRESET;
    case BB_NAV_PATTERN: return BB_NAV_MSG_PATTERN;
    case BB_NAV_SOUND: return bb_nav_bank_count(s, BB_NAV_SOUND) > 1 ? BB_NAV_MSG_SOUND : BB_NAV_MSG_SOUND_KNOBS;
    case BB_NAV_FX: return BB_NAV_MSG_FX;
    case BB_NAV_SAVE: return BB_NAV_MSG_SAVE;
    case BB_NAV_EVOLVE: return BB_NAV_MSG_EVOLVE;
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
    nav_drop_routes(o);
    nav_release_keys(s, o, s->keys_active);
    s->keys_blocked |= hold;
    o->block_held_keys = 1;
}

/* Enter a page (idempotent). Returns 1 when the visible page changed. */
static int nav_enter(bb_nav_state *s, bb_nav_out *o, uint8_t page)
{
    int changed = s->page != page;
    if (changed) {
        s->page = page; s->bank = 0; s->root_wait = 0; o->message = nav_entry_message(s);
        /* v2.1: section keys open on the page of the playing section */
        if (page == BB_NAV_STUDIO || page == BB_NAV_PATTERN) {
            uint8_t p = s->sec_seen == NONE_IDX ? 0 : (uint8_t)(s->sec_seen / BB_NAV_SECTION_KEYS);
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
    return changed;
}

static void nav_reset_home(bb_nav_state *s)
{
    s->home_down = s->home_used = s->help = 0;
    s->home_since = 0;
}

static void nav_finish(const bb_nav_state *s, bb_nav_out *o)
{
    o->page = s->page;
    o->bank = s->bank;
    o->help = s->help;
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
    case BB_NAV_KIT: return BB_NAV_KEYS_KIT;
    case BB_NAV_PATTERN: return BB_NAV_KEYS_PATTERN;
    case BB_NAV_SOUND: return nav_is_drum(s) ? BB_NAV_KEYS_VOICE : BB_NAV_KEYS_NOTES;
    case BB_NAV_SAVE: return BB_NAV_KEYS_SLOT;
    case BB_NAV_EVOLVE: return BB_NAV_KEYS_EVOLVE;
    case BB_NAV_GLOBAL: return BB_NAV_KEYS_GLOBAL;
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
        if (white) return n < 3 ? (rec ? BB_NAV_K_INSTR_SOLO : BB_NAV_K_INSTR_MUTE) : BB_NAV_K_NONE;
        *arg = (uint8_t)(s->sec_page * BB_NAV_SECTION_KEYS + n);
        return BB_NAV_K_SECTION;
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
        *arg = n;
        return white ? BB_NAV_K_PICK : BB_NAV_K_NONE;
    case BB_NAV_PATTERN:
        *arg = white ? n : (uint8_t)(s->sec_page * BB_NAV_SECTION_KEYS + n);
        return white ? BB_NAV_K_PICK : BB_NAV_K_SECTION;
    case BB_NAV_SAVE:
        *arg = n;
        if (white && n < 4) return rec ? BB_NAV_K_SLOT_LOAD : BB_NAV_K_SLOT;
        return BB_NAV_K_NONE;
    case BB_NAV_GLOBAL:
        /* The ANIMATION / SCOPE / METERS display toggles belonged to the legacy faces. Every
         * player-facing page is now the shared v3 frame, which has no scope view and is always
         * animated, so offering them would be a false choice: the keys are dark and inert. */
        return BB_NAV_K_NONE;
    case BB_NAV_EVOLVE:
        *arg = n;
        if (white) return rec ? BB_NAV_K_EVO_LOCK : BB_NAV_K_EVO_KEY;
        if (n == 0) return BB_NAV_K_EVO_FREEZE;
        if (n == 1) return BB_NAV_K_EVO_HOME;
        return BB_NAV_K_NONE;
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
        r[0] = BB_NAV_R_BPM; r[1] = BB_NAV_R_SWING; r[2] = BB_NAV_R_AB_BALANCE; r[3] = BB_NAV_R_TRANSPOSE;
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
}

const char *bb_nav_role_label(uint8_t role) { return role < BB_NAV_ROLES ? nav_role_labels[role] : ""; }
const char *bb_nav_page_name(const bb_nav_state *s)
{
    if (s->page == BB_NAV_KIT && !nav_is_drum(s)) return "PATCH";
    return s->page < BB_NAV_PAGES ? nav_page_names[s->page] : "";
}
const char *bb_nav_message_text(uint8_t msg) { return msg < BB_NAV_MESSAGES ? nav_messages[msg] : ""; }
const char *bb_nav_scale_name(uint8_t scale) { return scale < BB_NAV_SCALES ? nav_scale_names[scale] : ""; }

const char *bb_nav_bank_name(const bb_nav_state *s)
{
    if (s->page == BB_NAV_FX && s->bank < 5) return nav_fx_bank_names[s->bank];
    if (s->page != BB_NAV_SOUND) return "";
    switch (s->instrument) {
    case BB_NAV_INSTR_DRUM: return s->bank ? "COLOUR" : "BASIC";
    case BB_NAV_INSTR_DRUM_909: return "PAGE";
    case BB_NAV_INSTR_TELEGRAM: return s->bank ? "MOD" : "OSC";
    default: return "TONE";
    }
}

static int nav_save_blocked(const bb_nav_facts *f) { return f->saving || f->running; }

/* What EDIT does on this page, shown on screen. "" = nothing (LED dark). */
const char *bb_nav_edit_label(const bb_nav_state *s, const bb_nav_facts *f)
{
    switch (s->page) {
    case BB_NAV_STUDIO: return "OPEN";
    case BB_NAV_KIT: return nav_is_drum(s) ? "CHOOSE" : "LOAD";
    case BB_NAV_PATTERN: return "CHOOSE";
    case BB_NAV_SOUND: case BB_NAV_FX: return bb_nav_bank_count(s, s->page) > 1 ? "NEXT" : "";
    case BB_NAV_SAVE: return nav_save_blocked(f) ? "" : "STORE";
    case BB_NAV_EVOLVE: return "CAPTURE";
    case BB_NAV_PLAY: return (!nav_is_drum(s) && nav_steps(s)) ? "TYPE" : "";
    default: return "";
    }
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
    o->message = (uint8_t)(BB_NAV_MSG_SECTIONS_1 + s->sec_page);
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

/* Returns 1 when the scan must be drained (page, key mode or knob bank changed). */
static int nav_button(bb_nav_state *s, bb_nav_out *o, uint8_t b, int rec, const bb_nav_in *in)
{
    const bb_nav_facts *f = &in->facts;
    uint8_t count;
    switch (b) {
    case BB_NAV_B_FX:
        if (!rec) return nav_enter(s, o, BB_NAV_FX);
        if (!nav_has(s, BB_NAV_F_THROW)) { o->message = BB_NAV_MSG_NOT_IN_BUILD; return 0; }
        if (!s->throw_on) { s->throw_on = 1; o->actions |= BB_NAV_A_THROW_ON; o->message = BB_NAV_MSG_THROW; }
        if (!(in->held & B(FX))) { s->throw_on = 0; o->actions |= BB_NAV_A_THROW_OFF; }  /* tap inside one scan */
        return 0;
    case BB_NAV_B_GLO:
        if (!rec) return nav_enter(s, o, BB_NAV_GLOBAL);
        if (nav_has(s, BB_NAV_F_TAP)) nav_tap(s, o, in->now_ms);
        else o->message = BB_NAV_MSG_NOT_IN_BUILD;
        return 0;
    case BB_NAV_B_SAVE:
        if (!rec) return nav_enter(s, o, BB_NAV_SAVE);
        if (nav_save_blocked(f)) o->message = f->saving ? BB_NAV_MSG_WAIT : BB_NAV_MSG_STOP_FIRST;
        else o->actions |= BB_NAV_A_QUICK_SAVE;
        return 0;
    case BB_NAV_B_ENV:
        if (rec) { o->actions |= BB_NAV_A_REVERT; return 0; }
        return nav_enter(s, o, BB_NAV_SOUND);
    case BB_NAV_B_SEQ:
        if (rec) o->actions |= BB_NAV_A_STEP_BANK_NEXT;
        return nav_enter_play(s, o, BB_NAV_PLAY_STEPS);
    case BB_NAV_B_ARP:
        if (rec) { o->actions |= BB_NAV_A_LAYER_ENABLE; return 0; }
        return nav_enter_play(s, o, BB_NAV_PLAY_LIVE);
    case BB_NAV_B_EDIT:
        if (rec) { o->actions |= BB_NAV_A_MUTE; return 0; }
        switch (s->page) {
        case BB_NAV_STUDIO: return nav_enter_play(s, o, s->play_keys);
        case BB_NAV_KIT:
            o->actions |= BB_NAV_A_KIT_CONFIRM;
            s->q_since[0] = 0; s->q_late &= (uint8_t)~1u;      /* retry restarts the wait */
            return 0;
        case BB_NAV_PATTERN:
            o->actions |= BB_NAV_A_PATTERN_CONFIRM;
            s->q_since[1] = 0; s->q_late &= (uint8_t)~2u;
            return 0;
        case BB_NAV_SOUND: case BB_NAV_FX:
            count = bb_nav_bank_count(s, s->page);
            if (count < 2) return 0;
            s->bank = (uint8_t)((s->bank + 1u) % count);
            o->message = nav_entry_message(s);
            return 1;
        case BB_NAV_SAVE:
            if (nav_save_blocked(f)) o->message = f->saving ? BB_NAV_MSG_WAIT : BB_NAV_MSG_STOP_FIRST;
            else o->actions |= BB_NAV_A_SAVE_STORE;
            return 0;
        case BB_NAV_EVOLVE: o->actions |= BB_NAV_A_EVO_CAPTURE; return 0;
        case BB_NAV_PLAY:
            if (!nav_is_drum(s) && nav_steps(s)) o->actions |= BB_NAV_A_EDIT_TYPE;
            return 0;
        default: return 0;
        }
    case BB_NAV_B_SCL:
        if (nav_is_drum(s)) {
            if (rec) return 0;
            o->actions |= BB_NAV_A_SWING_TOGGLE;
            o->message = f->swing_on ? BB_NAV_MSG_SWING_OFF : BB_NAV_MSG_SWING_ON;
            return 0;
        }
        if (!nav_has(s, BB_NAV_F_SCALE)) { o->message = BB_NAV_MSG_NOT_IN_BUILD; return 0; }
        if (rec) {
            s->root_wait = 1;
            o->actions |= BB_NAV_A_ROOT_WAIT;
            o->message = BB_NAV_MSG_ROOT_WAIT;
        } else {
            s->scale = (uint8_t)((s->scale + 1u) % BB_NAV_SCALES);
            s->root_wait = 0;
            o->actions |= BB_NAV_A_SCALE;
            o->message = (uint8_t)(BB_NAV_MSG_SCALE_OFF + s->scale);
        }
        return 0;
    case BB_NAV_B_LFO:
        if (rec) { o->actions |= BB_NAV_A_EVO_TOGGLE; return 0; }
        if (nav_enter(s, o, BB_NAV_EVOLVE)) { o->actions |= BB_NAV_A_EVO_OPEN; return 1; }
        return 0;
    case BB_NAV_B_OCTDN:
    case BB_NAV_B_OCTUP: {
        int dir = b == BB_NAV_B_OCTUP ? 1 : -1;
        switch (s->page) {
        case BB_NAV_PLAY:
            if (nav_steps(s)) { o->oct = BB_NAV_OCT_STEP_BANK; o->oct_dir = (int8_t)dir; }
            else if (!nav_is_drum(s)) nav_octave(s, o, dir);
            return 0;
        case BB_NAV_STUDIO:
            return nav_section_page(s, o, dir);
        case BB_NAV_PATTERN:
            if (rec) return nav_section_page(s, o, dir);
            /* fall through */
        case BB_NAV_KIT:
            o->oct = BB_NAV_OCT_LIST_PAGE; o->oct_dir = (int8_t)dir;
            return 0;
        case BB_NAV_SOUND: case BB_NAV_FX: {
            int next = (int)s->bank + dir;
            count = bb_nav_bank_count(s, s->page);
            if (count < 2) {
                if (s->page == BB_NAV_SOUND && !nav_is_drum(s)) nav_octave(s, o, dir);
                return 0;
            }
            if (next < 0 || next >= (int)count) return 0;
            s->bank = (uint8_t)next;
            o->message = nav_entry_message(s);
            return 1;
        }
        default: return 0;
        }
    }
    default:
        return 0;
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
    o->pattern_play = s->page == BB_NAV_PATTERN;
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
    memset(o, 0, sizeof *o);
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
        nav_drain(s, o, hold);
        nav_finish(s, o);
        return;
    }

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

    /* 5. Help overlay is modal for everything except transport. */
    if (s->help) {
        if (pressed & B(PLAY)) nav_play(s, o, rec, &in->facts);
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

    /* 9. The three big encoders: same job on every page. */
    {
        int16_t d;
        if ((d = enc[BB_NAV_E_SELECT]) != 0) {
            bb_nav_route *r = &o->enc[BB_NAV_E_SELECT];
            if (rec) { r->target = BB_NAV_TO_TEMPO; r->flags = BB_NAV_RF_COALESCE; r->delta = d; }
            else {
                nav_drain(s, o, hold);      /* focus changes: nothing else this scan */
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

/* ---------------------------------------------------------------- LEDs */

static uint8_t nav_key_led(const bb_nav_state *s, const bb_nav_facts *f, uint8_t k, uint32_t keys_held)
{
    uint8_t arg, role = bb_nav_key_role(s, k, 0, &arg);
    const uint8_t q = f->queued;
    switch (role) {
    case BB_NAV_K_NONE: return BB_NAV_LED_DARK;
    case BB_NAV_K_NOTE:
        if (s->scale != BB_NAV_SCALE_OFF && k % nav_scale_len[s->scale] == 0) return BB_NAV_LED_LIT; /* root keys */
        return BB_NAV_LED_DIM;
    case BB_NAV_K_ROOT: return BB_NAV_LED_BREATH;
    case BB_NAV_K_STEP: return (f->steps >> arg) & 1u ? BB_NAV_LED_LIT : BB_NAV_LED_DIM;
    case BB_NAV_K_KIT_SLOT:
        if ((q & BB_NAV_Q_KIT) && f->kit_slot_queued == arg) return BB_NAV_LED_BREATH;
        if (f->kit_slot == arg) return BB_NAV_LED_LIT;
        return (f->kit_slots_stored >> arg) & 1u ? BB_NAV_LED_DIM : BB_NAV_LED_DARK;
    case BB_NAV_K_GROUP_MUTE:
        return nav_audible(f->group_muted, f->group_solo, arg) ? BB_NAV_LED_LIT : BB_NAV_LED_DIM;
    case BB_NAV_K_FILL: return f->fill_on ? BB_NAV_LED_LIT : BB_NAV_LED_DIM;
    case BB_NAV_K_SECTION_PREV: case BB_NAV_K_SECTION_NEXT:
        return (q & BB_NAV_Q_SECTION) ? BB_NAV_LED_BREATH : BB_NAV_LED_DIM;
    case BB_NAV_K_KILL:
        if (arg == 3) return (nav_audible(f->instr_muted, f->instr_solo, 0) || nav_audible(f->instr_muted, f->instr_solo, 1))
                             ? BB_NAV_LED_LIT : BB_NAV_LED_DIM;
        return nav_audible(f->instr_muted, f->instr_solo, arg) ? BB_NAV_LED_LIT : BB_NAV_LED_DIM;
    case BB_NAV_K_INSTR_MUTE:
        return nav_audible(f->instr_muted, f->instr_solo, arg) ? BB_NAV_LED_LIT : BB_NAV_LED_DIM;
    case BB_NAV_K_VOICE: return s->voice == arg ? BB_NAV_LED_LIT : BB_NAV_LED_DIM;
    case BB_NAV_K_ACCENT_HOLD:
        return (keys_held >> k) & 1u ? BB_NAV_LED_LIT : BB_NAV_LED_DIM;
    case BB_NAV_K_BANK_PREV: return f->step_bank > 0 ? BB_NAV_LED_DIM : BB_NAV_LED_DARK;
    case BB_NAV_K_BANK_NEXT: return f->step_bank + 1u < f->step_banks ? BB_NAV_LED_DIM : BB_NAV_LED_DARK;
    case BB_NAV_K_SECTION:
        if ((q & BB_NAV_Q_SECTION) && f->section_queued == arg) return BB_NAV_LED_BREATH;
        if (f->section == arg) return BB_NAV_LED_LIT;
        return arg < f->section_count ? BB_NAV_LED_DIM : BB_NAV_LED_DARK;
    case BB_NAV_K_PICK: {
        uint8_t qbit = s->page == BB_NAV_KIT ? BB_NAV_Q_KIT : BB_NAV_Q_PATTERN;
        if (arg >= f->list_count) return BB_NAV_LED_DARK;
        if ((q & qbit) && f->list_queued == arg) return BB_NAV_LED_BREATH;
        return f->list_playing == arg ? BB_NAV_LED_LIT : BB_NAV_LED_DIM;
    }
    case BB_NAV_K_SLOT: return f->slot == arg ? BB_NAV_LED_LIT : BB_NAV_LED_DIM;
    case BB_NAV_K_ANIMATION: return (f->display & BB_NAV_DISP_ANIMATION) ? BB_NAV_LED_LIT : BB_NAV_LED_DIM;
    case BB_NAV_K_SCOPE: return (f->display & BB_NAV_DISP_SCOPE) ? BB_NAV_LED_LIT : BB_NAV_LED_DIM;
    case BB_NAV_K_METERS: return (f->display & BB_NAV_DISP_METERS) ? BB_NAV_LED_LIT : BB_NAV_LED_DIM;
    default: return BB_NAV_LED_DIM;
    }
}

void bb_nav_leds(const bb_nav_state *s, const bb_nav_facts *f, uint32_t keys_held, bb_nav_led_out *l)
{
    uint8_t b, k;
    memset(l, 0, sizeof *l);
    l->button[BB_NAV_B_PLAY] = (f->queued & BB_NAV_Q_START) ? BB_NAV_LED_BREATH :
                               f->running ? BB_NAV_LED_LIT : BB_NAV_LED_DIM;
    if (s->help) { l->button[BB_NAV_B_HOME] = BB_NAV_LED_LIT; return; }
    /* Dim = does something here without REC. Derived by dry-running the
     * real input path, so LEDs can never disagree with routing. */
    for (b = 0; b < BB_NAV_BUTTONS; b++) {
        bb_nav_state t = *s;
        bb_nav_in in;
        bb_nav_out o;
        if (b == BB_NAV_B_PLAY) continue;
        if (b == BB_NAV_B_HOME || b == BB_NAV_B_REC) { l->button[b] = BB_NAV_LED_DIM; continue; }
        memset(&in, 0, sizeof in);
        in.pressed = in.held = BB_NAV_BIT(b);
        in.facts = *f;
        t.home_down = 0; t.home_used = 0; t.help = 0; t.home_since = 0; t.throw_on = 0; t.keys_active = 0;
        bb_nav_input(&t, &in, &o);
        if (o.actions || o.page_changed || o.oct || t.bank != s->bank || t.play_keys != s->play_keys)
            l->button[b] = BB_NAV_LED_DIM;
    }
    /* lit = you are here / it is on */
    switch (s->page) {
    case BB_NAV_PLAY: l->button[nav_steps(s) ? BB_NAV_B_SEQ : BB_NAV_B_ARP] = BB_NAV_LED_LIT; break;
    case BB_NAV_SOUND: l->button[BB_NAV_B_ENV] = BB_NAV_LED_LIT; break;
    case BB_NAV_FX: l->button[BB_NAV_B_FX] = BB_NAV_LED_LIT; break;
    case BB_NAV_SAVE: l->button[BB_NAV_B_SAVE] = BB_NAV_LED_LIT; break;
    case BB_NAV_EVOLVE: l->button[BB_NAV_B_LFO] = BB_NAV_LED_LIT; break;
    case BB_NAV_GLOBAL: l->button[BB_NAV_B_GLO] = BB_NAV_LED_LIT; break;
    default: break;
    }
    if (s->page == s->home_page && (s->page != BB_NAV_PLAY || !nav_steps(s))) l->button[BB_NAV_B_HOME] = BB_NAV_LED_LIT;
    if (f->evolve_on) l->button[BB_NAV_B_LFO] = BB_NAV_LED_LIT;
    if (l->button[BB_NAV_B_SCL] != BB_NAV_LED_DARK) {
        if (nav_is_drum(s)) { if (f->swing_on) l->button[BB_NAV_B_SCL] = BB_NAV_LED_LIT; }
        else if (s->root_wait) l->button[BB_NAV_B_SCL] = BB_NAV_LED_BREATH;
        else if (s->scale != BB_NAV_SCALE_OFF) l->button[BB_NAV_B_SCL] = BB_NAV_LED_LIT;
    }
    if (s->page == BB_NAV_KIT || s->page == BB_NAV_PATTERN) {
        uint8_t bit = s->page == BB_NAV_KIT ? 1u : 2u;
        if (s->q_late & bit) l->button[BB_NAV_B_EDIT] = BB_NAV_LED_BREATH;          /* retry */
        else if (f->list_cursor != NONE_IDX && f->list_cursor != f->list_playing &&
                 !((f->queued & (bit == 1u ? BB_NAV_Q_KIT : BB_NAV_Q_PATTERN)) && f->list_queued == f->list_cursor))
            l->button[BB_NAV_B_EDIT] = BB_NAV_LED_LIT;                              /* confirm waits */
    }
    for (k = 0; k < BB_NAV_KEYS; k++) l->key[k] = nav_key_led(s, f, k, keys_held);
}
