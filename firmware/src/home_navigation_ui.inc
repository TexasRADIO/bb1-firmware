/* SPDX-License-Identifier: GPL-3.0-only
 * Instrument-first HOME; no musical commands and no transport ownership. */
#define BB_INSTRUMENT_HOME 1
static int bb_play_face_visible(void) {
    return !bb_sound_pick_page&&!bb_mix_page&&!bb_library_page&&!bb_diag_page&&!bb_send_page&&!bb_effect_page&&
        !bb_scene_page&&!bb_evo_page&&!bb_drum_sound_page&&!bb_kit_page&&!bb_tone_page&&
        (bb_face.requested.page==BB_DRUMS?bb_pad_page:bb_keys_page[bb_face.requested.page==BB_TELEGRAM?1:0]);
}
