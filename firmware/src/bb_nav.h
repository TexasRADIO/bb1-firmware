/* SPDX-License-Identifier: GPL-3.0-only
 * Big Beat navigation core v2 — the controls map Joe approved on 2026-10-27
 * (claude/CONTROLS-AUDIT-2026-10-27.md §3-§5).
 *
 * Pure decision module: it never sends commands, never touches hardware and
 * never reads globals. Each scan the firmware passes the panel edges/levels,
 * the encoder detents, the key edges/levels and a few facts it already knows
 * (running, queued kit/pattern/section/start, mutes, solos ...). The module
 * returns ACTIONS, encoder ROUTES and KEY EVENTS that the firmware performs
 * with its existing helpers, plus an LED picture for every button and key.
 *
 * C99, no heap, no floats, no globals (all state in bb_nav_state; the only
 * statics are const tables), deterministic. See DESIGN.md (Joe's guide) and
 * INTEGRATION.md (wiring, v1 -> v2 delta). */
#ifndef BB_NAV_H
#define BB_NAV_H
#include <stdint.h>

#ifndef BB_WITH_USER_KITS
#define BB_WITH_USER_KITS 0
#endif
#define BB_NAV_VERSION 2
#define BB_NAV_REVISION 1   /* v2.1: FM-1 keyboard order, paged sections, held-layer encoder latch */

/* Logical buttons, same order as panel.c's B_* enum. Masks use 1u<<index of
 * these LOGICAL ids (convert from matrix ids with panel.btn[]). */
enum {
    BB_NAV_B_FX, BB_NAV_B_SCL, BB_NAV_B_ENV, BB_NAV_B_LFO, BB_NAV_B_EDIT,
    BB_NAV_B_GLO, BB_NAV_B_HOME, BB_NAV_B_SAVE, BB_NAV_B_ARP, BB_NAV_B_SEQ,
    BB_NAV_B_PLAY, BB_NAV_B_REC, BB_NAV_B_OCTDN, BB_NAV_B_OCTUP, BB_NAV_BUTTONS
};
#define BB_NAV_BIT(b) (1u << (b))
/* Logical encoders, same order as panel.c's EN_* enum. */
enum {
    BB_NAV_E_SELECT, BB_NAV_E_ALGO, BB_NAV_E_PRESET,
    BB_NAV_E_K1, BB_NAV_E_K2, BB_NAV_E_K3, BB_NAV_E_K4, BB_NAV_ENCODERS
};
#if BB_WITH_USER_KITS
/* 2.0 kit builder: BUILD is a sub-mode of KIT on drum A, held in bb_nav_state.bank (0 on every page change, so
 * any other page leaves it). HOME steps back one level: SAVE AS -> BUILD -> the KIT list. */
enum { BB_NAV_KIT_LIST, BB_NAV_KIT_BUILD, BB_NAV_KIT_SAVE_AS };
#define BB_NAV_UK_LANES 17u
#define BB_NAV_HAS_BUILD 1
#endif
/* Logical keys: bit 0..15 = white keys 1..16 (left to right),
 * bit 16..26 = black keys 1..11 (left to right). The FM-1 keyboard runs
 * F..G (27 keys, F3..G5 on the panel; fm1_input.h note bit 0 = F). v2.1:
 * NOTE args follow that physical chromatic order, bottom F = MIDI 41 at key
 * octave 0 (the installed Dillo key base). v2.0 assumed a C..D keyboard. */
#define BB_NAV_WHITE(n) ((uint8_t)((n) - 1))          /* n = 1..16 */
#define BB_NAV_BLACK(n) ((uint8_t)(16 + (n) - 1))     /* n = 1..11 */
#define BB_NAV_KEYS 27
#define BB_NAV_KEY_MASK 0x7ffffffu
/* v2.1: section keys are paged, 11 per page (STUDIO: OCT-/+; PATTERN:
 * REC+OCT-/+). Black key n on page p = section p*11+n (0-based). */
#define BB_NAV_SECTION_KEYS 11u
#define BB_NAV_SECTION_PAGES 5u    /* sections 1..55 (2.1 P3b: CR-8000 has 43, TR-66 54) */
/* RC2 (Joe 10:32): on PERFORM (HOME/CONDUCT) the white keys are the sections, 16 a page: 4 pages, 1..64 */
#define BB_NAV_CONDUCT_KEYS 16u
#define BB_NAV_CONDUCT_PAGES 4u
/* v2.1: an encoder turning while its job changes (REC, help, page, bank,
 * focus) keeps its old job until it has been still this long; detents in
 * between are dropped (SLOOP 2.8.1 FX-fade -> tempo crossover). */
#define BB_NAV_ENC_GUARD_MS 250u

/* The one page variable. Only one page is ever current. */
typedef enum {
    BB_NAV_STUDIO,   /* band overview; HOME destination (default)            */
    BB_NAV_PLAY,     /* focused instrument: LIVE keys (ARP) or STEPS (SEQ)    */
    BB_NAV_KIT,      /* PRESETS: browse kit (drums) or patch (synth)          */
    BB_NAV_PATTERN,  /* ALGORITHM: browse pattern                             */
    BB_NAV_SOUND,    /* ENV: edit selected drum voice or the synth            */
    BB_NAV_FX,       /* FX: four effect banks + SENDS bank                    */
    BB_NAV_SAVE,     /* SAVE: four session slots                              */
    BB_NAV_EVOLVE,   /* LFO: pattern movement                                 */
    BB_NAV_GLOBAL,   /* GLO: tempo, swing, balance, transpose, display        */
    BB_NAV_SONG,     /* 2.1 P1: the SONG arranger (GLOBAL white 4, EDIT on STUDIO) */
    BB_NAV_PAGES
} bb_nav_page;

/* 2.1 P1: sub-pages (bb_nav_state.bank). GLOBAL white 1 / 2 open MIDI CHANNELS / MIDI CLOCK; SONG white 1 opens ADD. */
enum { BB_NAV_GLOBAL_MAIN, BB_NAV_GLOBAL_MIDI, BB_NAV_GLOBAL_CLOCK };
enum { BB_NAV_SONG_ROWS, BB_NAV_SONG_ADD };
/* 2.1 P1: the three EVOLVE modes (LFO steps them on EVOLVE; black 3 is the same as LFO there). */
enum { BB_NAV_MODE_EVOLVE, BB_NAV_MODE_EUCLID, BB_NAV_MODE_RANDOM, BB_NAV_MODES };

/* What the focused instrument is (bb_nav_set_instrument). */
typedef enum {
    BB_NAV_INSTR_DRUM,      /* native/sample drum layer (808 family)      */
    BB_NAV_INSTR_DRUM_909,  /* 909 family: sound banks = its control pages */
    BB_NAV_INSTR_DILLO,
    BB_NAV_INSTR_TELEGRAM,
    BB_NAV_INSTRS
} bb_nav_instrument;

/* PLAY page key mode, kept while visiting other pages. */
enum { BB_NAV_PLAY_LIVE, BB_NAV_PLAY_STEPS };
#define BB_NAV_PLAY_PADS BB_NAV_PLAY_LIVE   /* v1 name */

/* Scale lock (SCL on a synth). Order = SCL cycle order. */
enum { BB_NAV_SCALE_OFF, BB_NAV_SCALE_MINOR, BB_NAV_SCALE_MAJOR,
       BB_NAV_SCALE_DORIAN, BB_NAV_SCALE_PENTA, BB_NAV_SCALES };

/* Optional functions (audit §5 items 6, 10, 11, 14-17). Codex decides which
 * ship after measuring size; a disabled gesture does nothing except show
 * NOT IN THIS BUILD, and its LED is dark. */
enum {
    BB_NAV_F_TAP        = 1u << 0,  /* item 6:  REC+GLO tap tempo            */
    BB_NAV_F_SCALE      = 1u << 1,  /* item 10: SCL scale lock + root        */
    BB_NAV_F_RESTART    = 1u << 2,  /* item 11: REC+PLAY restart             */
    BB_NAV_F_FAV_KITS   = 1u << 3,  /* item 14: white 12-16 favourite kits   */
    BB_NAV_F_FILL       = 1u << 4,  /* item 15: black 5 momentary fill       */
    BB_NAV_F_THROW      = 1u << 5,  /* item 16: REC+FX hold dub throw        */
    BB_NAV_F_STEP_PITCH = 1u << 6   /* item 17: Dillo steps black keys pitch */
};
#define BB_NAV_F_ALL     0x7fu
#define BB_NAV_F_MINIMUM 0u          /* items 1-9, 12, 13 only */

/* How the firmware should draw / treat keys this scan (informational: the
 * key EVENTS below are what to perform). */
typedef enum {
    BB_NAV_KEYS_NONE,      /* no key does anything                         */
    BB_NAV_KEYS_STEPS,     /* PLAY steps                                   */
    BB_NAV_KEYS_PADS,      /* PLAY live, drums                             */
    BB_NAV_KEYS_NOTES,     /* synth keyboard (PLAY live, SOUND)            */
    BB_NAV_KEYS_STUDIO,
    BB_NAV_KEYS_KIT,
    BB_NAV_KEYS_PATTERN,
    BB_NAV_KEYS_VOICE,     /* SOUND, drums: choose voice                   */
    BB_NAV_KEYS_SLOT,
    BB_NAV_KEYS_EVOLVE,
    BB_NAV_KEYS_GLOBAL,
    BB_NAV_KEY_MODES
} bb_nav_key_mode;

/* What one key does on the current page. Each key has exactly ONE role per
 * page (and REC state); see bb_nav_key_role(). */
typedef enum {
    BB_NAV_K_NONE,
    /* sound keys: REC held = accent (never a second job) */
    BB_NAV_K_PAD,           /* arg drum voice 0..10                         */
    BB_NAV_K_NOTE,          /* arg MIDI note (octave + scale applied)       */
    BB_NAV_K_STEP,          /* arg step 0..15 of the current 16-step bank   */
    /* function keys: REC held = their second job, never accent */
    BB_NAV_K_KIT_SLOT,      /* arg 0..4: queue favourite kit at next bar    */
    BB_NAV_K_KIT_SLOT_STORE,/* REC+: store current kit in that slot         */
    BB_NAV_K_GROUP_MUTE,    /* arg group 0..3 KICK SN/CL HATS PERC          */
    BB_NAV_K_GROUP_SOLO,    /* REC+                                         */
    BB_NAV_K_FILL,          /* hold: press = fill on, RELEASE flag = off    */
    BB_NAV_K_SECTION_PREV, BB_NAV_K_SECTION_NEXT,   /* queued               */
    BB_NAV_K_KILL,          /* arg 0 DRUM A, 1 DRUM B, 2 DILLO, 3 ALL DRUMS */
    BB_NAV_K_VOICE,         /* arg voice: select it (call bb_nav_set_voice) */
    BB_NAV_K_ACCENT_HOLD,   /* modifier: held = steps written accented      */
    BB_NAV_K_BANK_PREV, BB_NAV_K_BANK_NEXT,         /* 16-step bank         */
    BB_NAV_K_STEP_PITCH,    /* arg MIDI note for the selected Dillo step    */
    BB_NAV_K_INSTR_MUTE,    /* arg 0 DRUM A, 1 DRUM B, 2 DILLO              */
    BB_NAV_K_INSTR_SOLO,    /* REC+                                         */
    BB_NAV_K_SECTION,       /* arg section 0..10: launch (queued)           */
    BB_NAV_K_PICK,          /* arg 0..15: quick pick on the list page       */
    BB_NAV_K_SLOT,          /* arg 0..3: choose save slot                   */
    BB_NAV_K_SLOT_LOAD,     /* REC+: load that slot                         */
    BB_NAV_K_ANIMATION, BB_NAV_K_SCOPE, BB_NAV_K_METERS,  /* GLOBAL display */
    BB_NAV_K_EVO_KEY,       /* arg 0..15: existing evolve key               */
    BB_NAV_K_EVO_LOCK,      /* REC+: existing evolve step lock              */
    BB_NAV_K_EVO_FREEZE, BB_NAV_K_EVO_HOME,
    BB_NAV_K_ROOT,          /* after REC+SCL: arg = new root 0..11          */
    BB_NAV_K_GEN_MODE,       /* EVOLVE black3: Evolve / Euclid / Random */
    BB_NAV_K_SUBPAGE,        /* GLOBAL: arg 0 MIDI, 1 CLOCK (bb_nav banks), 3 SONG, 4 DILLO VIEW (firmware opens) */
    /* 2.1 P1 */
    BB_NAV_K_EVO_MOVE,       /* EVOLVE white 7-10: rhythm / pitch / accent / slide (arg 6..9, as EVO_KEY) */
    BB_NAV_K_GEN_VOICE,      /* EUCLID, drums: white 1-16 choose the lane it writes (arg 0..15) */
    BB_NAV_K_FX_TAB,         /* FX white 1-5: ECHO / TAPE DRIVE / SPRING / MOTION / SENDS (arg tab) */
    BB_NAV_K_BACK,           /* MIDI black 1, SONG ADD white 1: back one level */
    BB_NAV_K_SONG_ADD,       /* SONG white 1 / 2: choose a section to add (arg 1: with the A/B/DILLO prompt) */
    BB_NAV_K_SONG_COPY, BB_NAV_K_SONG_DELETE, BB_NAV_K_SONG_UNDO, BB_NAV_K_SONG_REDO,
    BB_NAV_K_SONG_DILLO,     /* SONG ADD white 3: Dillo in / out of the added section */
    BB_NAV_K_CONDUCT,        /* 2.1 P3b HOME/CONDUCT black: arg section; every part on the focused song, next bar */
    BB_NAV_K_SONG_PICK,      /* 2.1 P3b HOME/SONGS white 1-16: arg 0..15, the list cursor */
    BB_NAV_K_TAKE,           /* 2.1 P3b HOME/SONGS black 1-3: arg part 0 A, 1 B, 2 DILLO takes the song (toggle) */
    BB_NAV_K_COLLECTION,     /* 2.1 P3b HOME/SONGS black 5-7: arg 0 SONGS, 1 ERAS, 2 ACID */
#if BB_WITH_USER_KITS
    BB_NAV_K_BUILD,          /* 2.0 kit builder: KIT black 1 opens BUILD, in BUILD it goes back to the list */
    BB_NAV_K_LANE,           /* BUILD: arg lane 0..16 (white 1-11, black 2-7): focus + hear it */
    BB_NAV_K_UK_SLOT,        /* SAVE AS: arg USER slot 0..3 */
#endif
    BB_NAV_KEY_ROLES
} bb_nav_key_role_t;

enum { BB_NAV_KF_ACCENT = 1u << 0, BB_NAV_KF_RELEASE = 1u << 1 };
typedef struct { uint8_t key, role, arg, flags; } bb_nav_key_ev;
#define BB_NAV_KEV_MAX 56           /* 27 presses + 27 owed releases + spare */

/* Where an encoder detent goes. */
typedef enum {
    BB_NAV_TO_NONE,        /* drained: firmware reads and discards it      */
    BB_NAV_TO_INSTRUMENT,  /* SELECT: DRUM A, DRUM B, DILLO                */
    BB_NAV_TO_TEMPO,       /* REC+SELECT                    (coalesce)     */
    BB_NAV_TO_SECTION,     /* REC+ALGORITHM: queue section live            */
    BB_NAV_TO_KIT,         /* PRESETS: move kit/patch cursor               */
    BB_NAV_TO_PATTERN,     /* ALGORITHM: move pattern preview              */
    BB_NAV_TO_VOICE,       /* REC+PRESETS, drums: selected voice           */
    BB_NAV_TO_STEP_PITCH,  /* REC+PRESETS, Dillo steps: step pitch         */
    BB_NAV_TO_KNOB,        /* K1-K4: role in .role, control index .index   */
    BB_NAV_TO_RATCHET,     /* 2.0: K4 while a drum step key is held: .index step 0..15, .delta turns */
    BB_NAV_TO_SONG_BANK,   /* 2.1 P1: ALGORITHM on SONG ADD: the source's bank                */
    BB_NAV_TARGETS
} bb_nav_target;
/* Route flags: COALESCE = continuous value, "latest value wins": the
 * firmware may merge it with a not-yet-sent command for the same
 * target/role/index instead of taking another queue slot. */
enum { BB_NAV_RF_COALESCE = 1u << 0 };

/* K1-K4 roles. Each maps to one existing parameter (INTEGRATION.md §4). */
typedef enum {
    BB_NAV_R_NONE,
    BB_NAV_R_GROUP_KICK, BB_NAV_R_GROUP_SNCL, BB_NAV_R_GROUP_HATS, BB_NAV_R_GROUP_PERC,
    BB_NAV_R_LAYER_LEVEL, BB_NAV_R_TUNE, BB_NAV_R_DECAY, BB_NAV_R_SWING,
    BB_NAV_R_VOICE_LEVEL, BB_NAV_R_DRIVE, BB_NAV_R_TONE, BB_NAV_R_ATTACK, BB_NAV_R_DIST,
    BB_NAV_R_SNAPPY, BB_NAV_R_MODE, BB_NAV_R_909_CONTROL,
    BB_NAV_R_SYNTH_LEVEL, BB_NAV_R_CUTOFF, BB_NAV_R_RESO, BB_NAV_R_ENV, BB_NAV_R_SYNTH_DECAY,
    BB_NAV_R_ACCENT, BB_NAV_R_SYNTH_DRIVE, BB_NAV_R_SLIDE, BB_NAV_R_WAVE,
    BB_NAV_R_SQUARE, BB_NAV_R_SAW, BB_NAV_R_SUB, BB_NAV_R_MOD, BB_NAV_R_LFO, BB_NAV_R_GLIDE,
    BB_NAV_R_MIX_DRUM_A, BB_NAV_R_MIX_DRUM_B, BB_NAV_R_MIX_DILLO, BB_NAV_R_FX_RETURN,
    BB_NAV_R_BPM, BB_NAV_R_AB_BALANCE, BB_NAV_R_TRANSPOSE,
    BB_NAV_R_ECHO_TIME, BB_NAV_R_ECHO_FDBK, BB_NAV_R_ECHO_HEADS, BB_NAV_R_ECHO_WET,
    BB_NAV_R_ECHO_TONE, BB_NAV_R_ECHO_DRIVE, BB_NAV_R_ECHO_WEAR, BB_NAV_R_SPRING_SIZE,
    BB_NAV_R_SPRING_DAMP, BB_NAV_R_SPRING_WET, BB_NAV_R_ECHO_MOTION,
    BB_NAV_R_SEND_ECHO, BB_NAV_R_SEND_SPRING,
    BB_NAV_R_SLOT, BB_NAV_R_EVO_AMOUNT, BB_NAV_R_EVO_EVERY, BB_NAV_R_EVO_ROOT, BB_NAV_R_EVO_SCALE,
    /* 2.1 P1: SONG (rows / ADD) and the GLOBAL MIDI banks */
    BB_NAV_R_SONG_ROW, BB_NAV_R_SONG_REPEATS, BB_NAV_R_SONG_BARS, BB_NAV_R_SONG_LOOP,
    BB_NAV_R_SONG_TARGET, BB_NAV_R_SONG_GROUP, BB_NAV_R_SONG_PART, BB_NAV_R_SONG_LENGTH,
    BB_NAV_R_MIDI_CH_A, BB_NAV_R_MIDI_CH_B, BB_NAV_R_MIDI_CH_BASS, BB_NAV_R_MIDI_CH_LEAD,
    BB_NAV_R_CLOCK_IN, BB_NAV_R_CLOCK_FOLLOW, BB_NAV_R_CLOCK_TRANSPORT, BB_NAV_R_CLOCK_OUT,
#if BB_WITH_USER_KITS
    BB_NAV_R_UK_LEVEL, BB_NAV_R_UK_TUNE, BB_NAV_R_UK_DECAY, BB_NAV_R_UK_VOICE,   /* BUILD: the focused lane */
#endif
    BB_NAV_ROLES
} bb_nav_role;

/* One-shot actions (bitmask in bb_nav_out.actions). */
enum {
    BB_NAV_A_TRANSPORT      = 1u << 0,  /* PLAY: start/stop                          */
    BB_NAV_A_RESTART        = 1u << 1,  /* REC+PLAY: restart from bar 1   (F_RESTART) */
    BB_NAV_A_PANIC          = 1u << 2,  /* REC+HOME: all notes off, keep playing      */
    BB_NAV_A_KIT_CONFIRM    = 1u << 3,  /* EDIT on KIT (also the retry)               */
    BB_NAV_A_PATTERN_CONFIRM= 1u << 4,  /* EDIT on PATTERN (also the retry)           */
    BB_NAV_A_MUTE           = 1u << 5,  /* REC+EDIT: mute/unmute focused instrument   */
    BB_NAV_A_EDIT_TYPE      = 1u << 6,  /* EDIT on Dillo steps: GATE/ACCENT/SLIDE     */
    BB_NAV_A_SWING_TOGGLE   = 1u << 7,  /* SCL, drums                                 */
    BB_NAV_A_SCALE          = 1u << 8,  /* SCL, synth: out.scale is the new scale     */
    BB_NAV_A_ROOT_WAIT      = 1u << 9,  /* REC+SCL, synth: next key sets the root     */
    BB_NAV_A_LAYER_ENABLE   = 1u << 10, /* REC+ARP: current layer on/off              */
    BB_NAV_A_STEP_BANK_NEXT = 1u << 11, /* REC+SEQ                                    */
    BB_NAV_A_REVERT         = 1u << 12, /* REC+ENV: sound back to its preset          */
    BB_NAV_A_EVO_OPEN       = 1u << 13, /* entered EVOLVE: capture if not ready       */
    BB_NAV_A_EVO_TOGGLE     = 1u << 14, /* REC+LFO: evolve on/off                     */
    BB_NAV_A_EVO_CAPTURE    = 1u << 15, /* EDIT on EVOLVE                             */
    BB_NAV_A_TAP            = 1u << 16, /* REC+GLO: a tap was registered  (F_TAP)     */
    BB_NAV_A_TEMPO_SET      = 1u << 17, /* out.tempo_x10 from taps (coalesce)         */
    BB_NAV_A_THROW_ON       = 1u << 18, /* REC+FX pressed          (F_THROW)          */
    BB_NAV_A_THROW_OFF      = 1u << 19, /* FX released after a throw (always paired)  */
    BB_NAV_A_SAVE_STORE     = 1u << 20, /* EDIT on SAVE: store to chosen slot         */
    BB_NAV_A_QUICK_SAVE     = 1u << 21, /* REC+SAVE: store to current slot            */
    BB_NAV_A_KEY_OCTAVE     = 1u << 22, /* out.key_octave changed (keys only)         */
    BB_NAV_A_HOME_AGAIN     = 1u << 23, /* EDIT on STUDIO: open SONG (HOME only goes back)  */
    BB_NAV_A_UNDO           = 1u << 24, /* REC+EDIT on PLAY/STEPS: undo the last pattern edit or kept take */
    BB_NAV_A_RECORD         = 1u << 25, /* REC+SEQ on PLAY/STEPS, drums: record into the playing pattern;
                                           a REC tap on SONG: record a take into the selected row */
#if BB_WITH_USER_KITS
    BB_NAV_A_BUILD          = 1u << 26, /* KIT: BUILD or SAVE AS opened or closed; out.bank says which */
    BB_NAV_A_UK_STORE       = 1u << 27, /* EDIT on SAVE AS: store the kit in the chosen USER slot */
#endif
    BB_NAV_A_GEN_MODE       = 1u << 28, /* 2.1 P1: LFO (or black 3) on EVOLVE: next mode            */
    BB_NAV_A_SONG_EDIT      = 1u << 29, /* 2.1 P1: EDIT on SONG: cue / load / confirm delete / add   */
    BB_NAV_A_GLOBALS_SAVE   = 1u << 30  /* 2.1 P1: EDIT or SAVE on MIDI: save the global settings  */
#define BB_NAV_HAS_HOME_AGAIN 1
#define BB_NAV_HAS_RATCHET_HOLD 1
};
#define BB_NAV_A_RESTORE 0x80000000u  /* 2.1 P2: HOME tap on the Dillo PATCH page: the sound from before the audition */
#define BB_NAV_HAS_UNDO 1   /* 2.0: BB_NAV_A_UNDO exists (tests compile on older trees without it) */
#define BB_NAV_HAS_RECORD 1 /* 2.0: BB_NAV_A_RECORD exists */

/* What OCT-/OCT+ mean this scan (firmware performs). KEY_OCTAVE is done by
 * the module itself (out.key_octave); it never transposes the pattern. */
typedef enum {
    BB_NAV_OCT_NONE, BB_NAV_OCT_STEP_BANK, BB_NAV_OCT_KEY_OCTAVE, BB_NAV_OCT_LIST_PAGE,
    BB_NAV_OCT_TARGETS
} bb_nav_oct;

/* Messages: one busy message and one message per real cause. */
typedef enum {
    BB_NAV_MSG_NONE,
    BB_NAV_MSG_WAIT,
    /* page entry hints */
    BB_NAV_MSG_STUDIO, BB_NAV_MSG_PLAY_PADS, BB_NAV_MSG_PLAY_STEPS, BB_NAV_MSG_PLAY_KEYS,
    BB_NAV_MSG_KIT, BB_NAV_MSG_PRESET, BB_NAV_MSG_PATTERN, BB_NAV_MSG_SOUND,
    BB_NAV_MSG_FX, BB_NAV_MSG_SAVE, BB_NAV_MSG_EVOLVE, BB_NAV_MSG_GLOBAL, BB_NAV_MSG_KIT_CHOSEN,
    /* gesture feedback the module knows by itself */
    BB_NAV_MSG_PANIC, BB_NAV_MSG_RESTART, BB_NAV_MSG_SWING_ON, BB_NAV_MSG_SWING_OFF,
    BB_NAV_MSG_SCALE_OFF, BB_NAV_MSG_SCALE_MINOR, BB_NAV_MSG_SCALE_MAJOR,
    BB_NAV_MSG_SCALE_DORIAN, BB_NAV_MSG_SCALE_PENTA, BB_NAV_MSG_ROOT_WAIT, BB_NAV_MSG_ROOT_SET,
    BB_NAV_MSG_TAP, BB_NAV_MSG_THROW, BB_NAV_MSG_STOP_FIRST, BB_NAV_MSG_OCTAVE_LIMIT,
    BB_NAV_MSG_KIT_LATE, BB_NAV_MSG_PATTERN_LATE, BB_NAV_MSG_NOT_IN_BUILD,
    /* failure causes reported by firmware helpers (unchanged from v1) */
    BB_NAV_MSG_AUDIO_FAULT, BB_NAV_MSG_KIT_NOT_READY, BB_NAV_MSG_PATTERNS_NOT_READY,
    BB_NAV_MSG_SOUNDS_NOT_READY, BB_NAV_MSG_NO_PATTERNS_FOR_KIT, BB_NAV_MSG_KIT_MISMATCH,
    BB_NAV_MSG_TIMING_UNSUPPORTED, BB_NAV_MSG_VOICE_UNSUPPORTED, BB_NAV_MSG_EXPRESSION_UNSUPPORTED,
    BB_NAV_MSG_PATTERN_UNAVAILABLE, BB_NAV_MSG_SOUND_REFUSED, BB_NAV_MSG_SOUND_LOADED,
    BB_NAV_MSG_NO_ALT_VOICE, BB_NAV_MSG_LAYER_OFF, BB_NAV_MSG_SOURCE_READ_ONLY,
    BB_NAV_MSG_EVOLVE_OFF,
    /* v2.1 */
    BB_NAV_MSG_SECTIONS_1, BB_NAV_MSG_SECTIONS_2, BB_NAV_MSG_SECTIONS_3, BB_NAV_MSG_SOUND_KNOBS,
    /* 2.1 P1 */
    BB_NAV_MSG_SONG, BB_NAV_MSG_MIDI, BB_NAV_MSG_CLOCK, BB_NAV_MSG_SONG_ADD, BB_NAV_MSG_EUCLID, BB_NAV_MSG_RANDOM,
#if BB_WITH_USER_KITS
    BB_NAV_MSG_BUILD, BB_NAV_MSG_SAVE_AS, BB_NAV_MSG_KIT_BUILD,
#endif
    /* RC1: section pages 4-5 (P3b's 5 pages); app texts, so the BBTX block is unchanged */
    BB_NAV_MSG_SECTIONS_4, BB_NAV_MSG_SECTIONS_5,
    BB_NAV_MSG_PERFORM_1, BB_NAV_MSG_PERFORM_2, BB_NAV_MSG_PERFORM_3, BB_NAV_MSG_PERFORM_4,   /* SECTIONS 1-16 .. 49-64 */
    BB_NAV_MESSAGES
} bb_nav_msg;

/* Facts the firmware already knows, passed in every scan (bb_nav_in.facts)
 * and to bb_nav_leds(). 0xFF = "none" for index fields. */
enum { BB_NAV_Q_KIT = 1u << 0, BB_NAV_Q_PATTERN = 1u << 1, BB_NAV_Q_SECTION = 1u << 2,
       BB_NAV_Q_START = 1u << 3 };
enum { BB_NAV_DISP_ANIMATION = 1u << 0, BB_NAV_DISP_SCOPE = 1u << 1, BB_NAV_DISP_METERS = 1u << 2 };
#define BB_NAV_NONE_IDX 0xFFu
typedef struct {
    uint8_t running;          /* transport running                               */
    uint8_t queued;           /* BB_NAV_Q_*: waiting for the next boundary       */
    uint8_t saving;           /* a session read/write is in progress             */
    uint8_t section;          /* playing section 0..10                           */
    uint8_t section_queued;   /* queued section (valid with Q_SECTION)           */
    uint8_t section_count;    /* sections in this pattern (keys beyond are dark) */
    uint8_t kit_slot;         /* favourite slot now playing (0..4 / NONE)        */
    uint8_t kit_slot_queued;  /* favourite slot queued (valid with Q_KIT)        */
    uint8_t kit_slots_stored; /* bit per favourite slot that holds a kit         */
    uint8_t list_count;       /* items on the visible KIT/PATTERN list page      */
    uint8_t list_cursor;      /* highlighted (browsed) item on that page / NONE  */
    uint8_t list_playing;     /* item actually playing, on that page / NONE      */
    uint8_t list_queued;      /* item queued (valid with Q_KIT or Q_PATTERN)     */
    uint8_t instr_muted;      /* bit0 DRUM A, bit1 DRUM B, bit2 DILLO            */
    uint8_t instr_solo;       /* same bits                                       */
    uint8_t group_muted;      /* 4 bits, focused drum layer                      */
    uint8_t group_solo;       /* 4 bits                                          */
    uint8_t swing_on, evolve_on, fill_on;
    uint8_t step_bank, step_banks; /* current 16-step bank, number of banks       */
    uint16_t steps;           /* steps set for the selected voice, this bank     */
    uint8_t slot;             /* chosen save slot 0..3                           */
    uint8_t display;          /* BB_NAV_DISP_*                                   */
    /* 2.1 P1 (on EVOLVE, steps above holds the LOCKED steps of this bank instead) */
    uint8_t gen_mode;         /* BB_NAV_MODE_*                                   */
    uint8_t gen_voice;        /* EUCLID's lane 0..15                             */
    uint8_t evo_parts;        /* bit per part evolving (0 A, 1 B, 2 DILLO)       */
    uint8_t evo_moves;        /* bit per attribute: rhythm, pitch, accent, slide */
    uint8_t song;             /* BB_NAV_SONG_F_*                                 */
    uint8_t home_take;        /* 2.1 P3b: HOME/SONGS parts that take a song (bit 0 A, 1 B, 2 DILLO) */
    uint8_t home_coll;        /* 2.1 P3b: HOME/SONGS collection: 0 SONGS, 1 ERAS, 2 ACID           */
    uint64_t sec_lit, sec_queued;   /* 2.1 RC3: the sections of a playing / queued combination (bit = section) */
} bb_nav_facts;
enum { BB_NAV_HOME_CONDUCT, BB_NAV_HOME_SONGS };   /* 2.1 P3b: STUDIO (HOME) banks */
enum { BB_NAV_SONG_F_ROWS = 1u << 1,   /* the song has rows                                                */
       BB_NAV_SONG_F_UNDO = 1u << 2, BB_NAV_SONG_F_REDO = 1u << 3,
       BB_NAV_SONG_F_DILLO = 1u << 4,  /* ADD: the added section has a Dillo part                         */
       BB_NAV_SONG_F_DELETE = 1u << 5  /* a delete waits for EDIT                                          */ };

typedef struct {
    uint8_t target;  /* bb_nav_target */
    uint8_t role;    /* bb_nav_role when target == BB_NAV_TO_KNOB */
    uint8_t index;   /* 909 control index (bank*4+knob); else knob 0..3 */
    uint8_t flags;   /* BB_NAV_RF_* */
    int16_t delta;   /* detents to apply (0 when target is NONE) */
} bb_nav_route;

/* The single state struct. Nothing else is remembered. */
typedef struct {
    /* configuration */
    uint8_t home_page;      /* BB_NAV_STUDIO (approved) or BB_NAV_PLAY */
    uint8_t rec_down;       /* 2.1 P1: REC was held at the last scan: LEDs, legend and help use the REC map */
    uint16_t hold_ms;       /* HOME hold for help */
    uint16_t queue_timeout_ms; /* queued kit/pattern older than this: retry hint */
    uint8_t features;       /* BB_NAV_F_* */
    /* navigation */
    uint8_t page;           /* bb_nav_page: the ONLY page value */
    uint8_t bank;           /* sub-bank of page; 0 on every page change */
    uint8_t play_keys;      /* BB_NAV_PLAY_LIVE / BB_NAV_PLAY_STEPS */
    uint8_t landing;        /* top-level destination: SELECT changes destination here */
    /* HOME gesture + help overlay */
    uint8_t home_down, home_used, help;
    uint8_t rec_used, rec_help;   /* 2.0 N5: REC held alone shows this page's chords (non-modal) */
    uint8_t rec_shown;            /* 2.1 P1: REC help was shown during this hold (a REC tap on SONG records) */
    uint32_t step_hold;           /* 2.0 ratchets: the drum step key held (bit), toggled on release unless K4 turned */
    uint8_t step_hold_key, step_hold_arg, step_hold_used;
    uint8_t explain;              /* 2.1 P1: HELP up: the control pressed to be explained (BB_NAV_EX_*), 0 none */
    uint32_t rec_since;
    uint32_t home_since;
    /* context supplied by firmware */
    uint8_t instrument, sound_banks, voice;
    /* performance state owned by the module */
    uint8_t scale, root, root_wait;
    int8_t key_octave;      /* -2..+2, live synth keys only */
    uint8_t throw_on;       /* dub throw active: THROW_OFF owed on FX release */
    uint8_t tap_run;        /* a tap sequence is in progress */
    uint8_t tap_n;          /* stored tap intervals 0..3 */
    uint16_t tap_iv[3];
    uint32_t tap_last;      /* ms of last tap (valid with tap_run) */
    uint32_t q_since[2];    /* queued kit / pattern since (ms|1); 0 = not queued */
    uint8_t q_late;         /* bit0 kit, bit1 pattern: retry hint already shown */
    uint8_t fx_tab;         /* 2.1 P1: the FX tab last shown (FX opens on it) */
    uint8_t help_page;      /* 2.1 P1: HELP rows page (OCT inside help) */
    uint8_t gen_mode;       /* 2.1 P1: BB_NAV_MODE_* (copied from facts each scan: EVOLVE's keys depend on it) */
    /* keys */
    uint32_t keys_blocked;  /* held across a drain: ignored until released */
    uint32_t keys_active;   /* held NOTE/FILL keys: a RELEASE event is owed */
    uint8_t act_role[BB_NAV_KEYS], act_arg[BB_NAV_KEYS];
    /* v2.1 */
    uint8_t focus;          /* firmware focus id (row: DRUM A, DRUM B, DILLO) */
    uint8_t sec_page;       /* section key page 0..BB_NAV_SECTION_PAGES-1 */
    uint8_t sec_seen, sec_count;   /* last facts.section / section_count */
    uint16_t enc_guard_ms;  /* BB_NAV_ENC_GUARD_MS; 0 = latch off */
    uint32_t enc_sig[BB_NAV_ENCODERS];   /* job each encoder had last scan */
    uint32_t enc_moved[BB_NAV_ENCODERS]; /* ms|1 of the last detent; 0 = never */
    uint32_t enc_hold[BB_NAV_ENCODERS];  /* ms|1 the latch last (re)armed; 0 = none */
    uint32_t enc_dropped;   /* detents dropped by the latch (diagnostic count) */
} bb_nav_state;

typedef struct {
    uint32_t pressed;       /* logical button press edges this scan */
    uint32_t held;          /* logical buttons currently down */
    int16_t enc[BB_NAV_ENCODERS]; /* detents read this scan (firmware reads all 7) */
    uint32_t keys;          /* logical key press edges (27 bits) */
    uint32_t keys_held;     /* logical key levels (27 bits) */
    uint32_t now_ms;
    bb_nav_facts facts;
} bb_nav_in;

typedef struct {
    uint8_t page, bank;     /* after this scan (mirror of state) */
    uint8_t page_changed;
    uint8_t help;           /* draw help overlay */
    uint8_t uboot;          /* both OCT held: do NOTHING else this scan */
    uint8_t pattern_play;   /* TRANSPORT on PATTERN (1): confirm-then-start when stopped; on HOME/SONGS (2, RC2): the song */
    uint8_t oct;            /* bb_nav_oct */
    int8_t oct_dir;         /* -1 / +1 (0 if none) */
    int8_t key_octave;      /* mirror of state */
    uint8_t scale, root;    /* mirror of state */
    uint8_t key_mode;       /* bb_nav_key_mode for this scan */
    uint8_t block_held_keys;/* a drain happened: firmware stops its own held-note state */
    uint8_t message;        /* bb_nav_msg to show (0 = keep current) */
    uint16_t tempo_x10;     /* with A_TEMPO_SET: BPM x 10 */
    uint32_t actions;       /* BB_NAV_A_* */
    bb_nav_route enc[BB_NAV_ENCODERS];
    uint8_t kev_count;
    bb_nav_key_ev kev[BB_NAV_KEV_MAX];
} bb_nav_out;

/* LED language (§3.6). */
enum { BB_NAV_LED_DARK, BB_NAV_LED_DIM, BB_NAV_LED_LIT, BB_NAV_LED_BREATH };
typedef struct { uint8_t button[BB_NAV_BUTTONS]; uint8_t key[BB_NAV_KEYS]; } bb_nav_led_out;

void bb_nav_init(bb_nav_state *s, uint8_t home_page, uint16_t hold_ms);
void bb_nav_set_features(bb_nav_state *s, uint8_t features);
void bb_nav_input(bb_nav_state *s, const bb_nav_in *in, bb_nav_out *out);
/* Count-ACK wait: HOME keeps its normal back/help semantics; only actual live
 * PAD roles and owed releases survive. All authoring/encoder routes are blocked.
 * The firmware admits PLAY as STOP separately, never as a deferred START. */
void bb_nav_input_count_busy(bb_nav_state *s,const bb_nav_in *in,bb_nav_out *out);

/* Firmware feedback (call after performing the corresponding action). */
void bb_nav_set_instrument(bb_nav_state *s, uint8_t instrument, uint8_t sound_banks);
void bb_nav_set_voice(bb_nav_state *s, uint8_t voice);
/* v2.1: which physical instrument has focus (DRUM A/B are both DRUM). Part
 * of every K1-K4 job, so a turn cannot continue onto the other layer. */
void bb_nav_set_focus(bb_nav_state *s, uint8_t focus);
/* After KIT_CONFIRM (or a kit key) succeeded: continue to PATTERN. */
void bb_nav_kit_confirmed(bb_nav_state *s, bb_nav_out *out);
/* Firmware-forced page change. Owed key releases are emitted in out. */
void bb_nav_goto(bb_nav_state *s, uint8_t page, bb_nav_out *out);
/* Busy/hold path where bb_nav_input is not called: forget HOME gesture/help.
 * Owed releases (notes, fill, throw) stay owed and come out next scan. */
void bb_nav_cancel(bb_nav_state *s);

/* Read-only queries for drawing. */
const char *bb_nav_page_name(const bb_nav_state *s);
/* The page name with the control that opens it ("PATTERN \xB7 ALGO"); the status bar's label (2.0, N1). */
const char *bb_nav_page_label(const bb_nav_state *s);
#define BB_NAV_HAS_PAGE_LABEL 1
const char *bb_nav_bank_name(const bb_nav_state *s);
/* RC1: the status bar's page text, one composition for the firmware (bb_v3_common) and the capture gate: the page and
 * its bank ("FX  TAPE DRIVE", "SOUND  COLOUR"), else the label of the control that opens the page (N1). Returns the
 * length; out is always terminated. */
unsigned bb_nav_status_name(const bb_nav_state *s, char *out, unsigned n);
/* RC1: the HELP title's page where the screen layout is another page's (HOME/SONGS uses the PATTERN list, the Dillo
 * PATCH page and the kit builder's BUILD / SAVE AS the KIT list): "SONGS", "PATCH", "BUILD", "SAVE AS"; else NULL. */
const char *bb_nav_help_word(const bb_nav_state *s);
/* Welcome-card overrides; NULL keeps the normal control description. */
const char *bb_nav_welcome_help(uint8_t kind, uint8_t control, int rec);
uint8_t bb_nav_bank_count(const bb_nav_state *s, uint8_t page);
uint8_t bb_nav_section_page(const bb_nav_state *s);   /* v2.1: 0 = sections 1-11 */
void bb_nav_knobs(const bb_nav_state *s, uint8_t roles[4], const char *labels[4]);
const char *bb_nav_role_label(uint8_t role);
const char *bb_nav_edit_label(const bb_nav_state *s, const bb_nav_facts *f); /* "" = EDIT does nothing */
uint8_t bb_nav_keys_for(const bb_nav_state *s);
uint8_t bb_nav_key_role(const bb_nav_state *s, uint8_t key, int rec, uint8_t *arg);
/* 2.0 D4: the one-line key legend from the role table ("1-11 PADS  B1-4 MUTE"); whites = 0: black keys only;
 * 2.1 P1: whites = 2: every key, single-spaced; 3: white keys only (SONG's key line when the whole one is too wide).
 * Writes at most cap bytes with the NUL; returns the length. */
unsigned bb_nav_legend(const bb_nav_state *s, char *out, unsigned cap, int whites);
#define BB_NAV_HAS_LEGEND 1
int bb_nav_key_is_sound(uint8_t role);
void bb_nav_leds(const bb_nav_state *s, const bb_nav_facts *f, uint32_t keys_held, bb_nav_led_out *l);
const char *bb_nav_message_text(uint8_t msg);

/* ------------------------------------------------------------ 2.1 P1: one table
 * Every key role and every button role has ONE descriptor: its legend word (app), its help text (BBTX id) and its
 * light rule. LEDs, the legend, the EDIT caption and HELP are all read from these tables and from the same two
 * resolvers dispatch uses (bb_nav_key_role, bb_nav_button_role), so they cannot disagree. Rule: off = does
 * nothing here now; half (DIM) = works here; bright (LIT) = on / playing / you are here; pulse (BREATH) = waiting
 * for the bar or for you. A dark key or button never acts (nav_key_presses, nav_button). */
typedef enum {
    BB_NAV_BR_NONE,
    BB_NAV_BR_FX, BB_NAV_BR_GLOBAL, BB_NAV_BR_SAVE_PAGE, BB_NAV_BR_SOUND, BB_NAV_BR_EVOLVE,   /* open a page */
    BB_NAV_BR_LIVE, BB_NAV_BR_STEPS,                       /* ARP / SEQ: PLAY live keys / steps */
    BB_NAV_BR_THROW, BB_NAV_BR_TAP, BB_NAV_BR_QUICK_SAVE, BB_NAV_BR_REVERT, BB_NAV_BR_RECORD, BB_NAV_BR_STEP_BANK,
    BB_NAV_BR_LAYER, BB_NAV_BR_UNDO, BB_NAV_BR_MUTE,       /* REC chords */
    BB_NAV_BR_SONG, BB_NAV_BR_CHOOSE, BB_NAV_BR_LOAD, BB_NAV_BR_SAVE_AS, BB_NAV_BR_STORE, BB_NAV_BR_CAPTURE,
    BB_NAV_BR_APPLY, BB_NAV_BR_TYPE,                       /* EDIT */
    BB_NAV_BR_SWING, BB_NAV_BR_SCALE, BB_NAV_BR_ROOT,      /* SCL */
    BB_NAV_BR_MODE, BB_NAV_BR_EVO_TOGGLE,                  /* LFO on EVOLVE, REC+LFO */
    BB_NAV_BR_OCT_STEPS, BB_NAV_BR_OCT_KEYS, BB_NAV_BR_OCT_LIST, BB_NAV_BR_OCT_SECTIONS, BB_NAV_BR_OCT_BANK,
    BB_NAV_BR_HOME, BB_NAV_BR_PANIC, BB_NAV_BR_TRANSPORT, BB_NAV_BR_REC,
    BB_NAV_BR_SONG_EDIT, BB_NAV_BR_SONG_ADD, BB_NAV_BR_SONG_REC, BB_NAV_BR_GLOBALS_SAVE, BB_NAV_BR_LEAVE,
    BB_NAV_BR_SONGS,         /* 2.1 P3b: EDIT on HOME/CONDUCT opens HOME/SONGS */
    BB_NAV_BUTTON_ROLES
} bb_nav_button_role_t;
/* What button b does on this page with REC up (rec = 0) or held (rec = 1); NONE = nothing (its LED is dark).
 * f may be NULL (then "now" checks that need facts, such as SAVE while running, are skipped). */
uint8_t bb_nav_button_role(const bb_nav_state *s, const bb_nav_facts *f, uint8_t b, int rec);
uint16_t bb_nav_button_help(uint8_t role);                 /* help id of its text (bb_nav_help_text) */
uint16_t bb_nav_key_help(uint8_t role, uint8_t arg);       /* help id of a key role's text */
const char *bb_nav_help_text(uint16_t id);                 /* the text of a help id (BBTX, or a short app text) */
const char *bb_nav_key_word(const bb_nav_state *s, uint8_t key, int rec);   /* its legend word ("" = none) */
uint8_t bb_nav_key_led(const bb_nav_state *s, const bb_nav_facts *f, uint8_t key, uint32_t keys_held);
const char *bb_nav_mode_name(uint8_t mode);                /* EVOLVE / EUCLID / RANDOM */
#define BB_NAV_HAS_P1 1

/* HELP rows, generated from the tables above for the page under the overlay (rec = 1: the REC chords). */
enum { BB_NAV_HR_KEYS, BB_NAV_HR_BUTTON, BB_NAV_HR_KNOBS, BB_NAV_HR_ENCODER,
       BB_NAV_HR_HELD_K4 /* STEPS, drums: a held step key + K4 (the ratchet) */ };
typedef struct {
    uint8_t kind;     /* BB_NAV_HR_*                                                          */
    uint8_t a, b;     /* KEYS: first and last key; BUTTON: button; KNOBS: 0..3 = K1..K4, 4 = all; ENCODER: encoder */
    uint8_t rec;      /* a REC chord                                                          */
    uint8_t role;     /* key role / button role / knob role                                   */
    uint8_t arg;
    uint16_t text;    /* BBTX id (0: KNOBS rows carry the labels themselves)                  */
} bb_nav_help_row;
#define BB_NAV_HELP_ROWS 40u
#define BB_NAV_HELP_PAGE 9u          /* rows drawn per HELP page; OCT-/+ page inside HELP */
/* Writes rows first .. first+max-1 (rows may be NULL with max 0) and returns how many rows the page has. When
 * explain is not 0, *found is set to the index of its row (-1: the control does nothing here). */
unsigned bb_nav_help(const bb_nav_state *s, const bb_nav_facts *f, int rec, bb_nav_help_row *rows, unsigned first,
                     unsigned max, uint8_t explain, int *found);
/* Press to explain: while HOME is held, a key, button or knob is explained instead of acting. */
enum { BB_NAV_EX_KEY = 0x20u, BB_NAV_EX_BUTTON = 0x40u, BB_NAV_EX_ENCODER = 0x80u };   /* | index */
/* Hooks for later phases (P2 Dillo audition, P3 conductor) live in the same tables: a new role needs a descriptor. */
const char *bb_nav_scale_name(uint8_t scale);

#endif
