/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef BB_MASTER_PATTERN_ROUTE_H
#define BB_MASTER_PATTERN_ROUTE_H
#include "player_binding.h"
/* UI-thread only. Existing player inboxes retain ownership, busy feedback and
 * audio acknowledgement. Never enables a drum layer or changes transport. */
static inline bb_player *hm_ready_player(const hm_state *s, bb_player *drums,
                                         bb_player *synths, unsigned layer) {
    bb_player *p = hm_player(s, drums, synths, layer);
    if (!p || !p->ready) return 0;
#if defined(BB_TWO_DRUM_HOST_EXPERIMENT)
    return p; /* st_ready_player checked row domain, target and readiness. */
#else
    unsigned drum = s->selected == HM_DRUMS;
    unsigned target = drum ? layer : s->selected - HM_DILLO;
    return p->is_drum == drum && p->target == target ? p : 0;
#endif
}
static inline int hm_pattern_move(const hm_state *s, bb_player *drums,
                                  bb_player *synths, unsigned layer,
                                  bb_audio *audio, int delta) {
    bb_player *p = hm_ready_player(s, drums, synths, layer);
    if (!audio || !p || !delta) return BB_FACTORY_PATTERN_INVALID;
    /* Same bounded input as HOME encoders, including INT_MIN/MAX. */
    delta = delta > 127 ? 127 : delta < -127 ? -127 : delta;
    return bb_player_move(p, audio, delta);
}
static inline int hm_variation(const hm_state *s, bb_player *drums,
                               bb_player *synths, unsigned layer,
                               bb_audio *audio, unsigned key) {
    bb_player *p = hm_ready_player(s, drums, synths, layer);
    if (!audio || !p || key >= BB_BROWSER_PART_KEYS)
        return BB_FACTORY_PATTERN_INVALID;
    return bb_player_part_key(p, audio, key);
}
#endif
