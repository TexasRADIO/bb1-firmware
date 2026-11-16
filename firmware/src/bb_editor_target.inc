/* SPDX-License-Identifier: GPL-3.0-only
 * BB1 2.3 unity-target bridge for the shared editor adapter.
 * Include after usb.c, ota.c, bb_target_audio.c and bb_target_ui.c.
 */
#include "bb/bb_editor.h"
#include "bb/bb_learn_report.h"

static bb_editor_t bb_editor_target;

static int bb_editor_target_send(void *context, const uint8_t *sysex,
                                 unsigned length) {
    uint32_t packets[64];
    int count;
    (void)context;
    count = bb_learn_pack_usb(sysex, length, packets,
                              sizeof(packets) / sizeof(packets[0]));
    return count > 0 && midi_out_sysex_nb(packets, (unsigned)count);
}

static void bb_editor_target_init(void) {
    (void)bb_editor_init(&bb_editor_target, &bb_sound.commands,
                         &bb_sound.core, 0, bb_editor_target_send, 0);
}

/* Inspect only the editor manufacturer/family prefix. Other frames remain for
 * ota_service(), so ordinary install/recovery traffic keeps its owner. */
static void bb_editor_target_service(void) {
    const uint8_t *frame;
    uint32_t length;
    if (ota_frame_get(&frame, &length) && length >= 8u &&
        frame[0] == BB_ED_MFR_0 && frame[1] == BB_ED_MFR_1 &&
        frame[2] == BB_ED_MFR_2 && frame[3] == BB_ED_DEVICE &&
        frame[4] == BB_ED_FAMILY) {
        bb_editor_rx(&bb_editor_target, frame + 5u, (unsigned)(length - 5u));
        ota_frame_done();
    }
    bb_editor_tick(&bb_editor_target);
}
