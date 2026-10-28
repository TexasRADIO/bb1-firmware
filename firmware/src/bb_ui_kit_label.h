/* SPDX-License-Identifier: GPL-3.0-only
 * The kit name a drum lane shows (STUDIO, legacy kit line), r9.9. A sampled kit is named exactly as the
 * KIT page names it: the picker's sample label for the SOUNDING sample id from the guarded audio status
 * (bb_audio_sample_status_layer). Native 808 and 909 keep their names. Pure, host-tested by
 * claude-code/integration/tests/test_kit_label.c. (Integration lead, #4 6471457725.) */
#ifndef BB_UI_KIT_LABEL_H
#define BB_UI_KIT_LABEL_H
static inline const char *bb_ui_kit_label(int family_909, const char *native_name,
                                          const char *const *sample_names, unsigned sample_count,
                                          int status_ok, unsigned selected, unsigned id_808)
{
    if (family_909) return "Oh Nine Oh";
    if (sample_names && sample_count && status_ok && selected != id_808 && selected < sample_count && sample_names[selected])
        return sample_names[selected];
    return native_name;
}
#endif
