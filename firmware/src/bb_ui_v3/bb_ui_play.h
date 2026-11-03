/* SPDX-License-Identifier: GPL-3.0-only
 * BB1 2.2 playability (claude/2.2/playability/SPEC.md): the playing pages drawn as the instrument.
 * DRUM PADS, DRUM STEPS, DILLO KEYS, DILLO STEPS and the five FX tabs. The firmware fills a bb_ui_play beside the
 * bb_ui_in each frame (bb_ui_play_firmware.inc; the capture gate fills it with example data) and hands it over with
 * bb_ui_play_set() before bb_ui_frame(). Plain state only: what the main loop already has (the UI mirror, the
 * navigation model, the step position, the keys). No audio-owner or interrupt state.
 *
 * Every page follows SPEC section 1: a key is drawn as a labelled button exactly when its LED is not dark
 * (bb_ui_in.key_led, the one LED table), bright exactly when its LED is lit; names in full; no cut text. */
#ifndef BB_UI_PLAY_H
#define BB_UI_PLAY_H
#include <stdint.h>

#define BB_PL_PADS 11
enum { BB_PL_FDBK, BB_PL_WET, BB_PL_TONE, BB_PL_DRIVE, BB_PL_WEAR, BB_PL_MOTION, BB_PL_SIZE, BB_PL_DAMP, BB_PL_SPRING_WET };
typedef struct {
    /* what moves frame to frame (each element hashes its own bit of these) */
    uint16_t pad_lit;                   /* pads flashing now: a pattern hit or a finger                  */
    uint32_t keys_sounding;             /* KEYS: keys whose note is sounding (held or the playing step)   */
    /* the rest: one hash for the page (bb_ui_play.c pl_els) */
    const char *pad_name[BB_PL_PADS];   /* the track names of the kit ("BASS DRUM", "CLOSED HAT")       */
    uint8_t pad_group[BB_PL_PADS];      /* the K1-K4 group of each track (KICK SN/CL HATS PERC), 0xFF none */
    uint8_t instr_muted;                /* bits A, B, DILLO (the kill / instrument mutes)                */
    /* Dillo */
    uint8_t keys;                       /* 1: Dillo KEYS (ARP), 0: Dillo STEPS (SEQ)                      */
    uint8_t key_note[27];               /* the MIDI note each key plays (KEYS) or sets (STEPS, black keys) */
    uint8_t step_note[16];              /* STEPS: MIDI note of each visible step (gate in step_gate)      */
    uint16_t step_gate, step_held;      /* steps with a note; the step(s) held under a finger             */
    /* FX: the values the knobs set, 0..255 unless noted */
    uint16_t echo_ms;                   /* ECHO TIME, ms                                                  */
    uint8_t heads;                      /* ECHO HEADS bits 1-3                                            */
    uint8_t fx[9];                      /* BB_PL_FDBK ..: in bb_ui_play_firmware.inc's parameter order     */
    uint8_t send[3][2];                 /* SENDS: KIT A / KIT B / DILLO into ECHO / SPRING                */
    uint8_t focus;                      /* the focused part (0 A, 1 B, 2 DILLO)                           */
} bb_ui_play;

void bb_ui_play_set(const bb_ui_play *p);

#ifndef BB_UI_HOOK_KEY
#define BB_UI_HOOK_KEY(key, bright) ((void)0)   /* host test: a key drawn as a button (bright = its lit look) */
#endif
#endif
