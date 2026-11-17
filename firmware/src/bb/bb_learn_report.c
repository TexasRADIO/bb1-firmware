/* SPDX-License-Identifier: GPL-3.0-only
 * BB1 2.3: Learn mode state reporter.
 *
 * Encodes the current navigation state as a USB-MIDI SysEx message.
 * A browser page at thebigbeat.app/learn listens via WebMIDI and
 * highlights the relevant guide section in real time.
 *
 * Integration: the main loop (bb_target_draw / bb_v3_draw) fills a
 * bb_learn_state from its navigation module, calls bb_learn_state_changed()
 * to detect a transition, then bb_learn_encode() + bb_learn_send() to emit.
 * bb_learn_send() writes USB-MIDI SysEx packets to the provided ring;
 * the caller is responsible for draining that ring via the USB TX path.
 *
 * Copyright (C) 2026 Big Beat Project */
#include "bb_learn_report.h"
#include <string.h>

int bb_learn_state_changed(const bb_learn_state *prev, const bb_learn_state *now)
{
    return memcmp(prev, now, sizeof(bb_learn_state)) != 0;
}

int bb_learn_encode(const bb_learn_state *s, uint8_t fw_id, uint8_t *buf, unsigned capacity)
{
    /* Message: F0 00 22 24 35 01 [fw] [12 data bytes] F7 = 19 bytes */
    if (capacity < 19u)
        return 0;
    buf[0]  = 0xF0u;
    buf[1]  = BB_LEARN_SYSEX_ID_0;   /* 00 */
    buf[2]  = BB_LEARN_SYSEX_ID_1;   /* 22 */
    buf[3]  = BB_LEARN_SYSEX_ID_2;   /* 24 */
    buf[4]  = BB_LEARN_SYSEX_ID_3;   /* 35 */
    buf[5]  = BB_LEARN_MSG_STATE;     /* 01 */
    buf[6]  = fw_id & 0x7Fu;
    buf[7]  = s->page & 0x7Fu;
    buf[8]  = s->bank & 0x7Fu;
    buf[9]  = s->mode & 0x7Fu;
    buf[10] = s->knob & 0x7Fu;
    buf[11] = (uint8_t)((s->bpm >> 7) & 0x7Fu);   /* BPM high 7 bits */
    buf[12] = (uint8_t)(s->bpm & 0x7Fu);           /* BPM low 7 bits  */
    buf[13] = s->instrument & 0x7Fu;
    buf[14] = s->running & 0x01u;
    buf[15] = s->section & 0x7Fu;
    buf[16] = s->help & 0x7Fu;
    buf[17] = s->fx_tab & 0x7Fu;
    buf[18] = 0xF7u;
    return 19;
}

/* Pack one SysEx byte range into USB-MIDI event packets (4 bytes each).
 * CIN 4 = SysEx start/continue (3 bytes), CIN 5/6/7 = SysEx end (1/2/3 bytes).
 * Returns the number of 4-byte packets written to out[]. */
int bb_learn_pack_usb(const uint8_t *sysex, unsigned len, uint32_t *out, unsigned capacity)
{
    unsigned i = 0, n = 0;
    while (i < len) {
        unsigned remain = len - i;
        uint32_t pkt;
        if (n >= capacity)
            return (int)n;
        if (remain >= 3u && i + 3u < len) {
            /* CIN 4: SysEx start or continue, 3 bytes */
            pkt = 4u | ((uint32_t)sysex[i] << 8) |
                  ((uint32_t)sysex[i + 1] << 16) | ((uint32_t)sysex[i + 2] << 24);
            i += 3u;
        } else if (remain == 3u) {
            /* CIN 7: SysEx end, 3 bytes */
            pkt = 7u | ((uint32_t)sysex[i] << 8) |
                  ((uint32_t)sysex[i + 1] << 16) | ((uint32_t)sysex[i + 2] << 24);
            i += 3u;
        } else if (remain == 2u) {
            /* CIN 6: SysEx end, 2 bytes */
            pkt = 6u | ((uint32_t)sysex[i] << 8) | ((uint32_t)sysex[i + 1] << 16);
            i += 2u;
        } else {
            /* CIN 5: SysEx end, 1 byte */
            pkt = 5u | ((uint32_t)sysex[i] << 8);
            i += 1u;
        }
        out[n++] = pkt;
    }
    return (int)n;
}
