/* SPDX-License-Identifier: GPL-3.0-only
 * BB1 2.3: Learn mode state reporter.
 *
 * Sends the current navigation state as SysEx when it changes.
 * A browser page at thebigbeat.app/learn listens via WebMIDI and
 * highlights the relevant guide section in real time.
 *
 * Message format (USB-MIDI SysEx, 19 bytes):
 *   F0 00 22 24 35 01 [fw-id] [page] [bank] [mode] [knob] [bpm-hi] [bpm-lo]
 *      [instrument] [running] [section] [help-page] [fx-tab] F7
 *
 * Byte 5 = 0x01 (learn state report; other types reserved).
 * Byte 6 = firmware identity (BB_LEARN_FW_*).
 * All data bytes are 7-bit (0..127) per MIDI spec. */
#ifndef BB_LEARN_REPORT_H
#define BB_LEARN_REPORT_H
#include <stdint.h>

#define BB_LEARN_SYSEX_ID_0 0x00u
#define BB_LEARN_SYSEX_ID_1 0x22u
#define BB_LEARN_SYSEX_ID_2 0x24u
#define BB_LEARN_SYSEX_ID_3 0x35u
#define BB_LEARN_MSG_STATE   0x01u

/* Firmware identity — auto-detected by the Sideman browser page */
#define BB_LEARN_FW_BB1      0x01u
#define BB_LEARN_FW_COMPAS   0x02u
#define BB_LEARN_FW_ELITE    0x03u
#define BB_LEARN_FW_KICK     0x04u

typedef struct {
    uint8_t page;        /* bb_nav_page */
    uint8_t bank;        /* sub-bank */
    uint8_t mode;        /* play_keys, knob_mode, etc. */
    uint8_t knob;        /* last active knob (0..3 = K1..K4, 4..6 = SELECT/ALGO/PRESET) */
    uint16_t bpm;
    uint8_t instrument;  /* BB_TOP_* or machine index */
    uint8_t running;
    uint8_t section;     /* current song section 0..54 */
    uint8_t help;        /* help page (0 = not in help) */
    uint8_t fx_tab;      /* current FX tab */
    uint8_t conduct_scope; /* conductor scope */
} bb_learn_state;

/* Returns 1 if the state changed since last report. */
int bb_learn_state_changed(const bb_learn_state *prev, const bb_learn_state *now);

/* Encode state into a SysEx message buffer. Returns message length (including F0/F7).
 * Buffer must be at least 19 bytes. fw_id is one of BB_LEARN_FW_*. */
int bb_learn_encode(const bb_learn_state *s, uint8_t fw_id, uint8_t *buf, unsigned capacity);

/* Pack a SysEx byte range into USB-MIDI event packets (4 bytes each).
 * Returns the number of packets written to out[]. */
int bb_learn_pack_usb(const uint8_t *sysex, unsigned len, uint32_t *out, unsigned capacity);

#endif
