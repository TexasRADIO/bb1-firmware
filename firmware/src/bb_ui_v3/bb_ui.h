/* SPDX-License-Identifier: GPL-3.0-only
 * Big Beat UI module, v3: controls and information first (claude/VISUAL-V3-INFO-FIRST-2026-10-27.md,
 * Joe 05:30), the Armadillo small (a transport and beat indicator made from OUR plate), animation last;
 * kit and pattern names in Taylor Rushing's type (Joe 05:31). Everything else of the approved v2
 * (claude/VISUAL-AUDIT-2026-10-27.md) stands: frame, colours, type sizes, knob bar.
 * Drawn with the firmware's own API (src/gfx.c: cv_* strips, AF_S / AF_M fonts).
 *
 * The firmware fills a bb_ui_in with plain musical state each UI frame (no globals are read) and
 * calls bb_ui_frame(). The module redraws only the regions whose state changed, most important
 * first, until a pixel budget is spent; what is left waits for the next frame (frames are
 * skipped, audio is never delayed).
 *
 * Navigation stays in claude-code/navigation (bb_nav): the page, K1-K4 labels and the key /
 * button LED levels come from bb_nav_page_name(), bb_nav_knobs() and bb_nav_leds(); this module
 * only draws them.
 *
 * Build: bb_ui.c is a unity-build unit like the firmware's ui_*.c files: include it after gfx.c
 * and build/gen/bb_armadillo.h (it uses their static functions and data).
 * C99, no heap, no floats, no globals except the strip clip (four ints). */
#ifndef BB_UI_H
#define BB_UI_H
#include <stdint.h>

/* ------------------------------------------------------------ tokens (audit §4.2) */
#define BB_UI_BG    0x0000u
#define BB_UI_TEXT  0xF79Du
#define BB_UI_GREY  0x83EFu
#define BB_UI_FAINT 0x2124u
#define BB_UI_KIT_A 0xFD68u
#define BB_UI_KIT_B 0x4E7Cu
#define BB_UI_DILLO 0xFB14u
#define BB_UI_RED   0xFA26u

/* ------------------------------------------------------------ pages */
enum {
    BB_UI_STUDIO,       /* HOME: three instrument lanes                                   */
    BB_UI_PLAY_DRUMS,   /* ARP on a drum kit: now row, four group lanes, master, knobs    */
    BB_UI_STEPS,        /* SEQ on a drum kit: 11 x 16 matrix + selected voice             */
    BB_UI_PLAY_DILLO,   /* ARP / SEQ on Dillo: piano roll + filter curve                  */
    BB_UI_KIT,          /* PRESETS: kit / patch browser                                   */
    BB_UI_PATTERN,      /* ALGORITHM: song timeline + fill countdown                      */
    BB_UI_FX,           /* FX: the echo machine                                           */
    BB_UI_KNOBS,        /* SOUND, SAVE, EVOLVE, GLOBAL: title + four meters                */
    BB_UI_ATTRACT,      /* 30 s idle on STUDIO: the full engraving, the only full-size one */
    BB_UI_PAGES
};
/* which page draws a bb_nav page (nav_page = bb_nav_page, play_keys =
 * BB_NAV_PLAY_LIVE/STEPS, synth = the focused instrument is Dillo/Telegram) */
uint8_t bb_ui_page_for(uint8_t nav_page, uint8_t play_keys, uint8_t synth);

enum { BB_UI_INST_A, BB_UI_INST_B, BB_UI_INST_DILLO };        /* colour identity */
enum { BB_UI_LED_DARK, BB_UI_LED_DIM, BB_UI_LED_LIT, BB_UI_LED_BREATH };   /* = BB_NAV_LED_* */
enum { BB_UI_Q_KIT = 1u, BB_UI_Q_PATTERN = 2u, BB_UI_Q_SECTION = 4u, BB_UI_Q_START = 8u }; /* = BB_NAV_Q_* */

/* events since the last frame (a bitmask the firmware ORs in from the sequencer / audio side
 * and clears after each frame). v3 uses BEAT (beat dots), the group hits (lane level flash),
 * KNOB (pop-up) and any input (attract timer); the rest are accepted and ignored. */
enum {
    BB_EV_BEAT   = 1u << 0,   /* a quarter note                                  */
    BB_EV_BAR    = 1u << 1,   /* a bar start (with BEAT)                         */
    BB_EV_KICK   = 1u << 2,   /* group hits: KICK, SN/CL, HATS, PERC             */
    BB_EV_SNARE  = 1u << 3,
    BB_EV_HATS   = 1u << 4,
    BB_EV_PERC   = 1u << 5,
    BB_EV_ACCENT = 1u << 6,   /* an accented hit (REC held)                      */
    BB_EV_NOTE   = 1u << 7,   /* a Dillo note                                    */
    BB_EV_NOTE_ACCENT = 1u << 8,
    BB_EV_TAP    = 1u << 9,   /* tap tempo                                       */
    BB_EV_KNOB   = 1u << 10,  /* K1-K4 turned; which in ev_knob                  */
    BB_EV_INPUT  = 1u << 11   /* any other key / button / encoder                */
};
#define BB_EV_HITS (BB_EV_KICK | BB_EV_SNARE | BB_EV_HATS | BB_EV_PERC)

/* one knob column */
enum { BB_UI_KN_MUTE = 1u, BB_UI_KN_OFF = 2u /* column unused */ };
typedef struct {
    const char *label;      /* from bb_nav_knobs()                      */
    const char *text;       /* value as text ("100", "1/8", "ON") ...   */
    uint8_t bar;            /* value bar 0..255                         */
    uint8_t flags;          /* BB_UI_KN_*                               */
} bb_ui_knob;

/* one STUDIO lane */
enum { BB_UI_LN_MUTED = 1u, BB_UI_LN_NEXT = 2u, BB_UI_LN_FOCUS = 4u, BB_UI_LN_SOLO = 8u, BB_UI_LN_OFF = 16u };
typedef struct {
    const char *name, *pattern, *part;   /* "KIT A", "Posse On Broadway", "HOOK 3/8" */
    const char *next;                    /* the queued section / pattern ("HOOK"), or 0 */
    uint16_t steps, accents;             /* this bar's 16 steps (bit 0 = step 1)    */
    uint8_t level;                       /* live output level 0..255                */
    uint8_t flags;                       /* BB_UI_LN_*                              */
} bb_ui_lane;

#define BB_UI_SECTIONS 11   /* = BB_NAV_SECTION_KEYS: one black key per drawn section */
#define BB_UI_LIST 5
#define BB_UI_NO 0xFFu

typedef struct {
    uint32_t now_ms;
    uint8_t page;            /* BB_UI_*                                            */
    uint8_t help;            /* HELP overlay (hold HOME)                           */
    uint8_t instr;           /* BB_UI_INST_* of the focused instrument             */
    const char *page_name;   /* bb_nav_page_name() / bank name: "PLAY", "FX  ECHO" */
    const char *instr_name;  /* "KIT A", "KIT B", "DILLO"                          */
    /* transport and status */
    uint8_t running, beat;   /* beat 0..3 within the bar                           */
    uint8_t step;            /* playhead 0..15                                     */
    uint32_t ticks;          /* sequencer ticks, 4 per quarter note (bb_ticks, as the installed
                                bb_playbook_face.inc reads it): the tower rings are born on them */
    uint16_t bpm_x10;
    uint8_t accent;          /* REC held: the instrument dot turns red             */
    uint8_t queued;          /* BB_UI_Q_*                                          */
    uint8_t loading;         /* a kit / sound is being prepared                    */
    uint8_t fault;           /* show the fault toast; text below                   */
    const char *fault_text;  /* one line, plain words                              */
    const char *notice;      /* the firmware's current message (bb_say), or 0: shown as one
                                line in the toast slot, never over a fault          */
    uint8_t level;           /* master output level 0..255 (attract rays)          */
    /* events since the last frame (BB_EV_*) */
    uint32_t events;
    uint8_t ev_knob;
    /* knob bar + key map strip (mirrors the key LEDs) */
    bb_ui_knob knob[4];
    uint8_t knob_turning;    /* 0..3: show the pop-up (bb_ui keeps it 600 ms) / BB_UI_NO */
    uint8_t key_led[27];     /* BB_UI_LED_* from bb_nav_leds(): white 1-16, black 1-11 */
    uint32_t keys_down;      /* held keys (drawn white)                            */
    uint32_t keys_red;       /* keys whose function is engaged as a cut: muted group, kill */
    /* STUDIO */
    bb_ui_lane lane[3];
    /* PLAY drums (v3 §2) */
    const char *pattern_name;    /* the playing pattern ("Posse On Broadway")      */
    const char *section_name;    /* its current section ("HOOK")                   */
    uint8_t bar, bars;           /* bar n of N in the section (1-based; 0 = hide)  */
    const char *next_name;       /* what is queued for the boundary ("VERSE", "KIT B"), or 0 */
    uint16_t gsteps[4], gacc[4]; /* this bar's 16 steps per group (KICK SN/CL HATS PERC) */
    uint8_t group_level[4];      /* K1-K4 group levels 0..255 (the knob values)    */
    uint8_t group_out[4];        /* live output level per group 0..255             */
    uint8_t group_muted;         /* bits KICK SN/CL HATS PERC                      */
    uint8_t out_peak[2];         /* master output L / R 0..255 (the published output peak) */
    uint8_t dsp;                 /* audio block time / budget, 0..255 (255 = the whole budget) */
    uint8_t fill;                /* fill playing                                   */
    /* STEPS (drums) */
    const char *voice_name;  /* "CLOSED HAT"                                       */
    uint8_t voice;           /* selected voice 0..10                               */
    uint16_t vsteps[11], vacc[11];
    uint8_t bank, banks;     /* 16-step bank                                       */
    /* PLAY Dillo */
    const char *patch, *scale;   /* "Rubber Acid", "A MIN"                         */
    int8_t note[16];         /* semitones above the lowest shown note, -1 rest     */
    uint16_t note_acc, note_slide;
    uint8_t cutoff, reso, envmod;    /* 0..255: the filter curve                   */
    int8_t octave;
    /* KIT / PATTERN browsers */
    const char *title, *subtitle;    /* big name, one grey line                    */
    const char *item[BB_UI_LIST];
    uint8_t items, cursor, playing_item, queued_item;
    uint8_t progress;        /* 0..255 of the way to the boundary while queued     */
    uint8_t sections, sec_cur, sec_next;   /* sec_next BB_UI_NO = none            */
    const char *sec_name[BB_UI_SECTIONS];
    uint8_t sec_bars[BB_UI_SECTIONS];
    uint8_t sec_pos;         /* 0..255 through the current section                 */
    uint8_t fill_beats;      /* beats until the fill lands (0 = none)              */
    /* FX */
    uint16_t echo_ms;        /* delay time                                         */
    uint8_t repeats;         /* feedback 0..255                                    */
    uint8_t heads;           /* bits: heads 1-3 on                                 */
    uint8_t banks_fx, bank_fx;
} bb_ui_in;

/* ------------------------------------------------------------ state */
#define BB_UI_MAX_EL 64
typedef struct {
    uint16_t px;             /* pixels sent this frame                              */
    uint8_t regions;         /* regions drawn                                       */
    uint8_t waiting;         /* dirty regions left for a later frame                */
    uint8_t full;            /* the whole screen was drawn (page change)            */
} bb_ui_stats;

/* the tower's quarter-note rings: a port of the installed bb_pb_motion (bb_playbook_face.inc) */
typedef struct { uint32_t born; uint8_t active; } bb_ui_wave;
typedef struct {
    bb_ui_wave wave[4];
    uint32_t tick;
    uint8_t slot, seen;
} bb_ui_rings;

#define BB_UI_ROLL_STEPS 9   /* STOP: the plate re-inks into the ball in 5 ordered steps, BIG BEAT in 4 */

typedef struct {
    uint8_t page;            /* drawn page (BB_UI_NO: none yet)                     */
    uint8_t help;
    uint8_t n_el;
    uint32_t drawn[BB_UI_MAX_EL];    /* state hash of what is on the screen per element */
    uint32_t flash_t[4];     /* group hit times (lane level flash), |1              */
    uint32_t pop_t;          /* knob pop-up shown since, |1                         */
    uint8_t pop_k;
    uint32_t beat_t;         /* last beat, |1 (attract rays)                        */
    uint32_t idle_t;         /* last input on STUDIO (attract after 30 s)           */
    uint8_t roll;            /* 0 = the plate .. BB_UI_ROLL_STEPS = the ball with BIG BEAT */
    uint32_t roll_t;         /* last roll step                                      */
    bb_ui_rings rings;
    uint32_t budget_px;      /* pixels per frame (0 = unlimited)                    */
    bb_ui_stats st;
} bb_ui;

void bb_ui_init(bb_ui *u, uint32_t now_ms, uint8_t running);
/* one UI frame: update the rings and the roll, redraw what changed within the budget. */
const bb_ui_stats *bb_ui_frame(bb_ui *u, const bb_ui_in *in);
/* force a whole-screen redraw on the next frame (after a firmware overlay) */
void bb_ui_invalidate(bb_ui *u);

#define BB_UI_POPUP_MS 600u
#define BB_UI_FLASH_MS 120u
#define BB_UI_ATTRACT_MS 30000u
#define BB_UI_RING_MS 900u       /* installed: a ring lives 900 ms                  */
#define BB_UI_MINI_MS 66u        /* the mini and its rings: ~15 frames a second     */
#define BB_UI_ROLL_MS 90u        /* one roll step (the lettering: 60 ms)            */
#define BB_UI_BUDGET_PX 24000u   /* ~32 ms of SPI at 12 MHz per frame by default    */

#endif
