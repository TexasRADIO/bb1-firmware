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
#define BB_NAV_SECTION_PAGES 3u    /* sections 1..33 */
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
    BB_NAV_PAGES
} bb_nav_page;

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
    BB_NAV_A_KEY_OCTAVE     = 1u << 22  /* out.key_octave changed (keys only)         */
};

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
} bb_nav_facts;

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
    uint16_t hold_ms;       /* HOME hold for help */
    uint16_t queue_timeout_ms; /* queued kit/pattern older than this: retry hint */
    uint8_t features;       /* BB_NAV_F_* */
    /* navigation */
    uint8_t page;           /* bb_nav_page: the ONLY page value */
    uint8_t bank;           /* sub-bank of page; 0 on every page change */
    uint8_t play_keys;      /* BB_NAV_PLAY_LIVE / BB_NAV_PLAY_STEPS */
    /* HOME gesture + help overlay */
    uint8_t home_down, home_used, help;
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
    uint8_t pattern_play;   /* TRANSPORT on PATTERN: confirm-then-start when stopped */
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
const char *bb_nav_bank_name(const bb_nav_state *s);
uint8_t bb_nav_bank_count(const bb_nav_state *s, uint8_t page);
uint8_t bb_nav_section_page(const bb_nav_state *s);   /* v2.1: 0 = sections 1-11 */
void bb_nav_knobs(const bb_nav_state *s, uint8_t roles[4], const char *labels[4]);
const char *bb_nav_role_label(uint8_t role);
const char *bb_nav_edit_label(const bb_nav_state *s, const bb_nav_facts *f); /* "" = EDIT does nothing */
uint8_t bb_nav_keys_for(const bb_nav_state *s);
uint8_t bb_nav_key_role(const bb_nav_state *s, uint8_t key, int rec, uint8_t *arg);
int bb_nav_key_is_sound(uint8_t role);
void bb_nav_leds(const bb_nav_state *s, const bb_nav_facts *f, uint32_t keys_held, bb_nav_led_out *l);
const char *bb_nav_message_text(uint8_t msg);
const char *bb_nav_scale_name(uint8_t scale);

#endif
