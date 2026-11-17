/* SPDX-License-Identifier: GPL-3.0-only
 * BB1 2.3: Screen companion — compact UI state cast over USB SysEx.
 *
 * Sends a binary snapshot of the essential bb_ui_in fields at ~10 Hz,
 * so a browser page can render a synchronised copy of the display.
 *
 * Protocol: SysEx F0 00 22 24 35 02 [payload...] F7
 *   Byte 5 = 0x02 distinguishes Screen from Sideman (0x01).
 *   Payload is 7-bit encoded (high bit cleared, MSBs packed separately).
 *
 * The browser page decodes and renders using Canvas, matching the
 * firmware's layout with the same fonts, colours and spacing.
 *
 * Copyright (C) 2026 Big Beat Project */
#ifndef BB_SCREEN_CAST_H
#define BB_SCREEN_CAST_H
#include <stdint.h>

/* Maximum raw (8-bit) payload before 7-bit encoding */
#define BB_CAST_MAX_RAW 128

/* Screen cast message type (byte 5 of SysEx, after F0 00 22 24 35) */
#define BB_CAST_TYPE 0x02

/* Payload layout (all uint8 unless noted):
 *  0     page (BB_UI_*)
 *  1     instr (BB_UI_INST_*)
 *  2     running
 *  3     beat (0-3)
 *  4     step (playhead 0-15)
 *  5-6   bpm_x10 (uint16 LE)
 *  7     accent (REC held)
 *  8     queued (BB_UI_Q_*)
 *  9     fault
 * 10     level (master output 0-255)
 * 11-12  events (uint16 LE, BB_EV_*)
 * 13     ev_knob
 * -- knobs (4 × 3 bytes = 12) --
 * 14     knob[0].bar
 * 15     knob[0].flags
 * 16     knob[1].bar
 * 17     knob[1].flags
 * 18     knob[2].bar
 * 19     knob[2].flags
 * 20     knob[3].bar
 * 21     knob[3].flags
 * 22     knob_turning
 * -- transport detail --
 * 23     bar
 * 24     bars
 * 25     fill
 * 26     fill_beats
 * -- groups (PLAY drums) --
 * 27-30  group_level[0..3]
 * 31-34  group_out[0..3]
 * 35     group_muted (bits)
 * 36-37  out_peak[0..1]
 * 38     dsp
 * -- steps (drums, 16 × 2 bits = 4 bytes per group) --
 * 39-42  gsteps packed (group 0-3 as uint16 LE × 4 = 8 bytes)
 * ...
 * -- Dillo (when page == PLAY_DILLO) --
 *        note[0..15] as signed bytes
 *        cutoff, reso, envmod
 * -- FX --
 *        echo_ms (uint16 LE), heads, fx_delay_type
 *        fx_dist_type, fx_drive, fx_comp, fx_comp_gr
 *        fx_rv_decay, fx_rv_level, fx_motion, fx_wear
 * -- sections --
 *        sections, sec_cur, sec_next, sec_pos
 * -- string IDs (text block indices, 0 = none) --
 *        page_name_id, notice_id
 *
 * Total: ~80-100 bytes raw → ~92-115 bytes 7-bit encoded → ~96-120 SysEx
 */

/* Encode the current UI state into a SysEx-ready buffer.
 * Returns the total SysEx message length including F0/F7,
 * or 0 if the state hasn't changed since the last encode.
 * `in` points to bb_ui_in (bb_ui_v3/bb_ui.h), cast from void* to
 * avoid a circular include. */
int bb_cast_encode(const void *in, uint8_t *buf, int maxlen);

/* Reset change detection (e.g. after a page change forces a full send) */
void bb_cast_reset(void);

#endif
