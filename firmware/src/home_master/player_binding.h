/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef BB_MASTER_PLAYER_BINDING_H
#define BB_MASTER_PLAYER_BINDING_H
#include "control.h"
#include "bb_player.h"
#if defined(BB_TWO_DRUM_HOST_EXPERIMENT)
#include "studio_rows.h"
#endif
/* Borrow existing validated players. No extra catalogues, inboxes or DSP state.
 * Caller owns arrays and must synchronize this UI-only lookup with page edits.
 * Explicit drum layer avoids silently redirecting B edits to audible A. */
static inline bb_player *hm_player(const hm_state *state,
                                  bb_player drums[BB_MACHINES],
                                  bb_player synths[BB_SYNTHS],
                                  unsigned drum_layer) {
    if (!state || !drums || !synths || state->selected >= HM_INSTRUMENTS)
        return 0;
#if defined(BB_TWO_DRUM_HOST_EXPERIMENT)
    (void)drum_layer;
    return st_ready_player(state->selected,drums,synths);
#else
    if (state->selected == HM_DRUMS)
        return drum_layer < BB_MACHINES ? &drums[drum_layer] : 0;
    unsigned synth = state->selected - HM_DILLO;
    return synth < BB_SYNTHS ? &synths[synth] : 0;
#endif
}
#endif
