/* SPDX-License-Identifier: GPL-3.0-only
 * BB1 2.1 top-level musical destinations. Pure state: no audio, UI or device
 * access.  SELECT chooses a destination; local player focus is deliberately a
 * separate value owned by that destination. */
#ifndef BB_TOP_MODE_H
#define BB_TOP_MODE_H

enum {
    BB_TOP_CONDUCTOR,
    BB_TOP_BIG_BEAT,
    BB_TOP_DILLO,
    BB_TOP_MODES
};

static inline unsigned bb_top_mode_step(unsigned mode, int delta)
{
    if (mode >= BB_TOP_MODES) mode = BB_TOP_CONDUCTOR;
    if (delta > 0) {
        unsigned d = (unsigned)(delta > 127 ? 127 : delta);
        return mode + d >= BB_TOP_MODES ? BB_TOP_MODES - 1u : mode + d;
    }
    if (delta < 0) {
        unsigned d = (unsigned)(delta < -127 ? 127 : -delta);
        return d > mode ? BB_TOP_CONDUCTOR : mode - d;
    }
    return mode;
}

static inline const char *bb_top_mode_name(unsigned mode)
{
    static const char *const names[BB_TOP_MODES] = {
        "RHYTHM CONDUCTOR", "BIG BEAT", "DILLO"
    };
    return names[mode < BB_TOP_MODES ? mode : BB_TOP_CONDUCTOR];
}

/* Page values are supplied by the caller so this tiny policy stays usable by
 * both the target and the browser emulator without importing either UI. */
static inline unsigned bb_top_mode_home(unsigned mode, unsigned conductor_page,
                                        unsigned play_page)
{
    return mode == BB_TOP_CONDUCTOR ? conductor_page : play_page;
}

/* Entering Big Beat restores its A/B layer; Dillo is always the Dillo row.
 * Conductor local focus is not derived from SELECT. */
static inline unsigned bb_top_mode_row(unsigned mode, unsigned conductor_focus,
                                       unsigned big_beat_layer, unsigned dillo_row)
{
    if (mode == BB_TOP_BIG_BEAT) return big_beat_layer > 1u ? 0u : big_beat_layer;
    if (mode == BB_TOP_DILLO) return dillo_row;
    return conductor_focus > 2u ? 0u : conductor_focus;
}

#endif
